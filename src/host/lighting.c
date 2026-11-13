/* Presentation-only point lighting on original BREW texels. Surface positions
 * come from the software rasterizer. Shadow rays intersect current level
 * segments, so opening a door changes light visibility. No game state or RNG
 * is modified. This is a first software ray-shadow pass, not global GI/RTX. */
#include "lighting.h"
#include <math.h>
#include <string.h>
#include <limits.h>
#ifdef OE2_NO_SPRITE_SHADOWS
#include <stdatomic.h>
#ifdef __vita__
#define OE2_LIGHT_MT 1
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#define LIGHT_THREADS 3
#else
#define LIGHT_THREADS 1
#endif
#endif
#ifdef OE2_QA_INTERACTIONS
#include <emscripten.h>
static double profile[8];
static int costMode;
void BrewLight_qaCostMode(int mode){costMode=mode;}
double BrewLight_qaProfile(int field){return field>=0&&field<8?profile[field]:0;}
#endif
#define MAX_PIXELS (704*880)
#define GRID_CELLS (128*128*24)
#define HIDDEN 0x40000u
#define NO_DEPTH 0x200000u
#ifdef OE2_NO_SPRITE_SHADOWS
/* Compact 8-byte Surface so 352x166 (~467 KB) fits inside PS Vita's 512 KB L2 cache.
 * x, y and z (bits 3..15 of zKind) are Q2 signed fixed-point (1/4 world unit).
 * rawKind (bits 0..2 of zKind): 0=none, 1=floor (nz=+127), 2=wall, 3=sprite, 4=weapon, 5=ceil (nz=-127). */
typedef struct { int16_t x,y,zKind; signed char nx,ny; } Surface;
static inline int surfRawKind(const Surface *s){return s->zKind&7;}
static inline int surfKind(const Surface *s){int k=s->zKind&7;return k==5?1:k;}
static inline int surfNz(const Surface *s){int k=s->zKind&7;return k==1?127:(k==5?-127:0);}
static inline float surfX(const Surface *s){return s->x*.25f;}
static inline float surfY(const Surface *s){return s->y*.25f;}
static inline float surfZ(const Surface *s){return (s->zKind>>3)*.25f;}
static inline int16_t clampQ2(int v){return (int16_t)(v<-32768?-32768:(v>32767?32767:v));}
static inline void surfSet(Surface *s,float px,float py,float pz,signed char nx,signed char ny,signed char nz,unsigned char kind){
 int k=(kind==1&&nz<0)?5:(int)kind;
 s->x=clampQ2((int)lrintf(px*4.f));
 s->y=clampQ2((int)lrintf(py*4.f));
 int iz=(int)lrintf(pz*4.f);if(iz<-4096)iz=-4096;else if(iz>4095)iz=4095;
 s->zKind=(int16_t)((iz<<3)|(k&7));
 s->nx=nx;s->ny=ny;
}
#else
typedef struct { float x,y,z; signed char nx,ny,nz; unsigned char kind; } Surface;
static inline int surfKind(const Surface *s){return s->kind;}
static inline int surfNz(const Surface *s){return s->nz;}
static inline float surfX(const Surface *s){return s->x;}
static inline float surfY(const Surface *s){return s->y;}
static inline float surfZ(const Surface *s){return s->z;}
static inline void surfSet(Surface *s,float px,float py,float pz,signed char nx,signed char ny,signed char nz,unsigned char kind){
 s->x=px;s->y=py;s->z=pz;s->nx=nx;s->ny=ny;s->nz=nz;s->kind=kind;
}
#endif
typedef struct { float x,y,z,r,g,b,radius,power; int flicker,key; } Light;
typedef struct { int tile; float r,g,b,radius,power,height; int flicker; } Source;
typedef struct {float x,y,z,size;int frame;uint32_t flags;int active;} Caster;
/* IDs visually verified against all 205 sprite identities in catalog_textures.
 * Doors, ordinary bottles and bright painted decorations are not emitters. */
static const Source sources[]={
 {116,1,.42f,.10f,230,2.2f,-20,1},
 {130,1,.40f,.10f,240,2.2f,0,1}, {136,1,.52f,.19f,240,2.0f,24,1},
 {138,1,.42f,.12f,240,2.0f,12,1}, {143,1,.45f,.12f,210,1.8f,16,1},
 {145,1,.40f,.15f,220,2.0f,16,1},
 {149,1,.30f,.06f,150,1.6f,0,1}, {150,1,.62f,.30f,210,1.6f,18,1},
 {161,1,.25f,.10f,220,1.8f,16,1},
 {181,1,.68f,.37f,150,1.2f,14,1}, {234,1,.40f,.10f,240,2.2f,0,1},
 {14,.35f,.65f,1,150,1.2f,0,0}, {15,.25f,1,.35f,150,1.2f,0,0}, {16,1,.18f,.10f,150,1.2f,0,0},
 {224,.25f,.55f,1,170,1.3f,0,0}, {230,1,.75f,.20f,150,1.3f,0,0}, {235,.6f,.2f,1,180,1.3f,0,0}, {238,.25f,.55f,1,170,1.3f,0,0},
 {220,1,.18f,.10f,140,1.1f,0,0}, {221,.25f,1,.35f,140,1.1f,0,0},
 {222,.35f,.65f,1,150,1.2f,0,0}, {223,1,.35f,.08f,170,1.6f,0,1},
 {225,.25f,.30f,1,110,.8f,0,0}, {227,1,.25f,.05f,170,1.5f,0,1},
 {242,1,.65f,.13f,130,1.0f,0,0}, {243,.14f,.30f,1,130,1.0f,0,0},
 {245,.2f,1,.25f,130,1.0f,0,0}, {252,.55f,.65f,1,170,1.4f,0,1}
};
typedef struct {float x,y,ratio,step;int axis,valid;} FloorDirection;
static FloorDirection floorDirections[880];
static Surface surfaces[MAX_PIXELS];
static uint32_t lit[MAX_PIXELS],cachedHash,geometryHash;
static unsigned char checked[GRID_CELLS];
static uint16_t visibility[GRID_CELLS];
static uint16_t wallVisibility[GRID_CELLS];
/* Static walls do not change when a billboard animation advances. Retain the
 * three exact ray results separately from the complete shadow-count cache. */
static uint32_t wallSamples[GRID_CELLS],wallLightHash[8],lightCacheHash[8];
static uint8_t wallSampleReady[8][(GRID_CELLS+7)/8];
#define MAX_DIRTY_CELLS 32768
#define MAX_DIRTY_WALL_BYTES 8192
static int dirtyCheckedCells[MAX_DIRTY_CELLS],numDirtyChecked;
static int dirtyWallBytes[8][MAX_DIRTY_WALL_BYTES],numDirtyWall[8];
#ifdef OE2_NO_SPRITE_SHADOWS
static int lightWallSlot[8];
static uint32_t shadowFrameGen=1;
static _Atomic uint16_t wallVisState[GRID_CELLS];
#define MAX_DIRTY_WALL_CELLS 16384
static int dirtyWallCells[8][MAX_DIRTY_WALL_CELLS];
static _Atomic uint32_t numDirtyWallAtomic[8];
static _Atomic uint8_t wallCheckedAtomic[3][129*129];
#endif
static uint32_t floatBits(float value){uint32_t bits;memcpy(&bits,&value,sizeof(bits));return bits;}
static Caster casters[800],posedCasters[800],nativePoses[800];
static int casterTag=-1,lightCasters[8][800],casterCount[8];
static Surface *underWeapon;
static uint32_t *weaponBackground;
static size_t weaponCapacity;
static int weaponCopied;
/* A small, geometry-only distance field supplies smooth floor contact shade. */
static float wallDistance[3][129*129],wallAxisDistance[3][2][129*129];
#ifndef OE2_NO_SPRITE_SHADOWS
static unsigned char wallChecked[3][129*129];
#endif
#ifdef OE2_NO_SPRITE_SHADOWS
typedef struct {int16_t x,y;float distance;int axis;uint32_t validGen;} CornerCache;
#else
typedef struct {float x,y,distance;int axis,valid;} CornerCache;
#endif
static CornerCache cornerCache[704];
static Light lights[8];
/* Arrival events outlive the retired missile, independently of sprite slots. */
typedef struct {Light light;int time,map,active;} Impact;
static Impact impacts[8];
static unsigned nextImpact;
static int width,height,numLights,fadeLevel=255;
static Render *renderer;
/* One native walking step is 64 world units. Processing distance is entirely
 * separate from the fixed source falloff that matches the former 120% look. */
#define LIGHT_REFERENCE_SCALE 1.2f
#define WALK_STEP 64.f
static int processingSteps=5;
static float processingX,processingY;
static int retainedKeys[7],retainedCount,retainedMap=-1;
void BrewLight_setRange(int steps){processingSteps=steps<5?5:steps>20?20:steps;}
static float fx,fy,rx,ry;
static float clampf(float x,float a,float b){return x<a?a:x>b?b:x;}
static uint32_t hashWord(uint32_t h,uint32_t n){return (h^n)*16777619u;}
static const Source *sourceByTile[256];
static unsigned char casterEligibleTile[256];
static int tablesReady;
static void initSourceTables(void){
 if(tablesReady)return;
 for(unsigned i=0;i<sizeof(sources)/sizeof(sources[0]);i++){
  int t=sources[i].tile;
  if(t>=0&&t<256)sourceByTile[t]=&sources[i];
 }
 for(int tile=17;tile<210;tile++){
  if(tile==137||tile==139||tile==161||tile==192||tile==193||tile==198||tile==206||tile==208)continue;
  if(sourceByTile[tile]&&tile!=116)continue;
  casterEligibleTile[tile]=1;
 }
 tablesReady=1;
}
static const Source *sourceForSprite(const Sprite *s){
 if(s->info&(0x10000u|0x40000u))return NULL;
 initSourceTables();
 int tile=s->info&255;
 /* Their fifth native frame contains an extinguished wick/empty lamp.
  * Script opcode 0x62 fixes that frame with the high bit; normal flames loop 0..3. */
 if((tile==150||tile==181)&&(s->info&0x80000000u)&&((s->info>>8)&15)>=4)return NULL;
 /* Native tile 116 draws its flaming meat only while this entity bit is set. */
 if(tile==116&&(!s->ent||!(s->ent->info&0x4000000u)))return NULL;
 return sourceByTile[tile];
}
int BrewLight_spriteEmits(const Sprite *s){return sourceForSprite(s)!=NULL;}
void BrewLight_impact(const GameSprite *missile,int flaming){
 if(!missile||!missile->sprite||!g_app)return;
 initSourceTables();
 const Source *p=flaming?sourceByTile[224]:sourceForSprite(missile->sprite);
 if(!p)return; /* Physical arrows, blood, melee and debris do not emit. */
 unsigned slot=nextImpact++%8;
 float dx=missile->dest[0]-missile->start[0],dy=missile->dest[1]-missile->start[1],d=sqrtf(dx*dx+dy*dy);
 /* Keep a wall hit on the incoming side of its opaque surface. */
 float bias=d>.01f?fminf(4.f,d*.25f)/d:0;
 Light l={missile->dest[0]-dx*bias,missile->dest[1]-dy*bias,(float)missile->dest[2],
  p->r,p->g,p->b,fmaxf(190,p->radius*1.3f),p->power*3.4f,0,3000+(int)slot};
 impacts[slot]=(Impact){l,g_app->time,g_app->canvas->mapId,1};
}
static void put(int x,int y,float px,float py,float pz,float nx,float ny,float nz,int kind){
 if(x<0||y<0||x>=width||y>=height)return;
 surfSet(&surfaces[y*width+x],px,py,pz,(signed char)(nx*127),(signed char)(ny*127),(signed char)(nz*127),(unsigned char)kind);
}
static int cachedSpanLineIdx=-1;
static float cachedSpanNx,cachedSpanNy;
void BrewLight_begin(Render *r){
 initSourceTables();
 renderer=r;width=r->width;height=r->height;
 if(width<1||height<1||width>704||height>880){width=height=0;return;}
 memset(surfaces,0,(size_t)width*height*sizeof(Surface));
#ifndef OE2_NO_SPRITE_SHADOWS
 int nSprites=r->numSprites<0?0:(r->numSprites>800?800:r->numSprites);
 if(r==g_app->render){memset(nativePoses,0,(size_t)nSprites*sizeof(nativePoses[0]));memset(posedCasters,0,(size_t)nSprites*sizeof(posedCasters[0]));}
 else memcpy(posedCasters,nativePoses,(size_t)nSprites*sizeof(posedCasters[0]));
 memset(floorDirections,0,(size_t)height*sizeof(floorDirections[0]));
#else
 if(width>400||height>240)memset(floorDirections,0,(size_t)height*sizeof(floorDirections[0]));
#endif
 casterTag=-1;cachedSpanLineIdx=-1;weaponCopied=0;fadeLevel=255;
 fx=r->viewCos/65536.f;fy=-r->viewSin/65536.f;rx=-fy;ry=fx;
}
int BrewLight_nativeTarget(void){return g_app&&renderer==g_app->render;}
void BrewLight_fade(int level){fadeLevel=level<fadeLevel?level:fadeLevel;}
void BrewLight_floor(Render *r,int x,int y,int u,int v,int du,int dv,int count,int ceiling){
 if(r!=renderer||y<0||y>=height||count<=0)return;
#ifdef OE2_NO_SPRITE_SHADOWS
 if(width>400||height>240)
#endif
 if(y<880){
  int axis=abs(du)>=abs(dv);int dominant=axis?du:dv;
  floorDirections[y]=(FloorDirection){u/65536.f,v/65536.f,dominant?(axis?(float)dv/du:(float)du/dv):0,fmaxf(fabsf((float)du),fabsf((float)dv))/65536.f,axis,dominant!=0};
 }
 int i0=x<0?-x:0,i1=(x+count>width)?(width-x):count;
 if(i0>=i1)return;
 uint32_t uu=(uint32_t)u+(uint32_t)du*(uint32_t)i0;
 uint32_t vv=(uint32_t)v+(uint32_t)dv*(uint32_t)i0;
 Surface *dst=&surfaces[y*width+x+i0];
 int n=i1-i0;
#ifdef OE2_NO_SPRITE_SHADOWS
 int iz=(ceiling?r->effCeil:r->effFloor)*4;
 if(iz<-4096)iz=-4096;else if(iz>4095)iz=4095;
 int16_t zk=(int16_t)((iz<<3)|(ceiling?5:1));
 for(int i=0;i<n;i++,uu+=(uint32_t)du,vv+=(uint32_t)dv){
  int qx=(int32_t)uu>>14,qy=(int32_t)vv>>14;
  dst[i].x=clampQ2(qx);
  dst[i].y=clampQ2(qy);
  dst[i].zKind=zk;
  dst[i].nx=0;
  dst[i].ny=0;
 }
#else
 float pz=(float)(ceiling?r->effCeil:r->effFloor);
 signed char snz=ceiling?-127:127;
 for(int i=0;i<n;i++,uu+=(uint32_t)du,vv+=(uint32_t)dv){
  dst[i].x=(int32_t)uu*(1.f/65536.f);dst[i].y=(int32_t)vv*(1.f/65536.f);dst[i].z=pz;
  dst[i].nx=0;dst[i].ny=0;dst[i].nz=snz;dst[i].kind=1;
 }
#endif
}
void BrewLight_span(Render *r,int x,int y,int count,int projection,int sprite){
 if(r!=renderer||projection<=0||r->shade>=4||(sprite&&r->shade!=0)||(unsigned)x>=(unsigned)width||count<=0)return;
 float invFocalW=r->projH>0?65536.f/(float)r->projH:1.f/width;
 float t=(float)(sprite?r->projH:r->projW)/projection;
 float lateral=(2*x+1.f-width)*invFocalW;
 float px=r->viewX+(fx+rx*lateral)*t,py=r->viewY+(fy+ry*lateral)*t;
 float nx=-fx,ny=-fy;
 if(!sprite&&r->curLineIdx>=0&&r->curLineIdx<r->numLines){
  if(r->curLineIdx!=cachedSpanLineIdx){
   Line *l=&r->lines[r->curLineIdx];nx=(float)(l->y2-l->y1);ny=(float)(l->x1-l->x2);
   float n=sqrtf(nx*nx+ny*ny);if(n>0){nx/=n;ny/=n;}
   cachedSpanLineIdx=r->curLineIdx;cachedSpanNx=nx;cachedSpanNy=ny;
  }else{nx=cachedSpanNx;ny=cachedSpanNy;}
  if(nx*(r->viewX-px)+ny*(r->viewY-py)<0){nx=-nx;ny=-ny;}
 }
 signed char snx=(signed char)(nx*127),sny=(signed char)(ny*127);
 unsigned char kind=sprite?3:2;
 float dz=-2.f*t*invFocalW;
 float z=r->viewZ+(height-2*y-1.f)*t*invFocalW;
 int i0=y<0?-y:0,i1=(y+count>height)?(height-y):count;
 if(i0>=i1)return;
 z+=dz*i0;
#ifdef OE2_NO_SPRITE_SHADOWS
 int16_t qx=clampQ2((int)lrintf(px*4.f)),qy=clampQ2((int)lrintf(py*4.f));
 int32_t zz=(int32_t)lrintf(z*65536.f),dz16=(int32_t)lrintf(dz*65536.f);
 Surface *s=&surfaces[(y+i0)*width+x];
 for(int i=i0;i<i1;i++,zz+=dz16,s+=width){
  int iz=zz>>14;
  if(iz<-4096)iz=-4096;else if(iz>4095)iz=4095;
  s->x=qx;s->y=qy;s->zKind=(int16_t)((iz<<3)|kind);s->nx=snx;s->ny=sny;
 }
#else
 for(int i=i0;i<i1;i++,z+=dz){
  Surface *s=&surfaces[(y+i)*width+x];
  s->x=px;s->y=py;s->z=z;s->nx=snx;s->ny=sny;s->nz=0;s->kind=kind;
 }
#endif
}
void BrewLight_weapon(Render *r,int x,int y,int count,int blend){
 /* Blended glows contain the background too. Keep its surface/normal so the
  * old bright background is not relit as a solid weapon-shaped overlay. */
 if(r!=renderer||count<=0||(blend>=1&&blend<=7))return;
 if(!weaponCopied){
  size_t pixels=(size_t)width*height;
  if(pixels>weaponCapacity){
   Surface *s=malloc(pixels*sizeof(Surface));uint32_t *b=malloc(pixels*sizeof(uint32_t));
   if(!s||!b){free(s);free(b);return;}
   free(underWeapon);free(weaponBackground);underWeapon=s;weaponBackground=b;weaponCapacity=pixels;
  }
  memcpy(underWeapon,surfaces,pixels*sizeof(Surface));
  for(int yy=0;yy<height;yy++)for(int xx=0;xx<width;xx++){
   uint16_t c=r->framebuffer[yy*(r->fbDib->nPitch/2)+xx];
   uint32_t r5=(c>>11)&31u,g6=(c>>5)&63u,b5=c&31u;
   weaponBackground[yy*width+xx]=((r5<<3)|(r5>>2))|(((g6<<2)|(g6>>4))<<8)|(((b5<<3)|(b5>>2))<<16)|0xff000000u;
  }
  weaponCopied=1;
 }
 for(int i=0;i<count;i++)put(x,y+i,r->viewX+fx*20,r->viewY+fy*20,r->viewZ-8,-fx,-fy,0,4);
}
void BrewLight_tagCaster(Render *r,Sprite *s){
#ifdef OE2_NO_SPRITE_SHADOWS
 (void)r;(void)s;
#else
 casterTag=s&&r==renderer?(int)(s-r->sprites):-1;
#endif
}
void BrewLight_poseCaster(Render *r,int x,int y,int z,int tile,int frame,int flags,int mode,int scale){
#ifdef OE2_NO_SPRITE_SHADOWS
 (void)r;(void)x;(void)y;(void)z;(void)tile;(void)frame;(void)flags;(void)mode;(void)scale;
#else
 if(r!=renderer||casterTag<0||casterTag>=800||mode!=0)return;
 int id=r->sprites[casterTag].info&255;
 if(id==116&&tile!=r->spriteRemap[118])return;
 if(tile!=r->spriteRemap[id]&&!(id==153&&tile==r->spriteRemap[152])&&!(id==116&&tile==r->spriteRemap[118]))return;
 posedCasters[casterTag]=(Caster){x,y,z,64.f*scale/65536.f,tile+frame,(uint32_t)flags,1};
 if(r==g_app->render)nativePoses[casterTag]=posedCasters[casterTag];
#endif
}
static int casterEligible(const Sprite *s){
 initSourceTables();
 int tile=s->info&255;
 if((s->info&(0x10000u|0x40000u))||s->renderMode==4)return 0;
 return casterEligibleTile[tile]!=0;
}
static Caster spriteCaster(const Render *r,const Sprite *s){
 int tile=s->info&255;
 return (Caster){s->x,s->y,s->z,(float)s->scale,r->spriteRemap[tile==116?118:tile]+((s->info>>8)&15),s->info,casterEligible(s)};
}
static int casterBlocks(const Render *r,const Caster *s,float ax,float ay,float az,float bx,float by,float bz){
 if(!s->active||s->size<=0||s->frame<0||s->frame>=1500)return 0;
 float nx=bx-s->x,ny=by-s->y;
 if(s->flags&0x180000u){nx=0;ny=1;}else if(s->flags&0x600000u){nx=1;ny=0;}
 float n=sqrtf(nx*nx+ny*ny);if(n<.01f)return 0;nx/=n;ny/=n;
 float dx=bx-ax,dy=by-ay,den=dx*nx+dy*ny;if(fabsf(den)<.0001f)return 0;
 float t=((s->x-ax)*nx+(s->y-ay)*ny)/den;if(t<=.025f||t>=.975f)return 0;
 float lateral=-(ax+t*dx-s->x)*ny+(ay+t*dy-s->y)*nx,z=az+(bz-az)*t;
 int column=(int)floorf((lateral/s->size+.5f)*88),row=(int)floorf((.5f-(z-s->z)/s->size)*88);
 if(column<0||column>=88||row<0||row>=88)return 0;
 if(s->flags&0x1000000u)column=87-column;if(s->flags&0x20000000u)row=87-row;
 int ofs=r->spriteMap[s->frame].ofs;const uint16_t *shape=r->shapeSpans;
 if(ofs<0||ofs+4>=0x16800||column<shape[ofs+2]||column>shape[ofs+3])return 0;
 int at=ofs+4+column-shape[ofs+2];if(at>=0x16800)return 0;
 int run=ofs+2+(int16_t)shape[at];
 for(int i=0;i<90&&run>=0&&run+1<0x16800;i++,run+=2){
  unsigned word=shape[run],start=word&255;if(start==127)return 0;
  if(row>=(int)start&&row<(int)(start+(word>>8)))return 1;
 }
 return 0;
}
int BrewLight_spriteBlocks(const Render *r,const Sprite *s,float ax,float ay,float az,float bx,float by,float bz){
 Caster c=spriteCaster(r,s);return casterBlocks(r,&c,ax,ay,az,bx,by,bz);
}
/* Broad phase only: the exact intersection test below is unchanged. */
#define TRACE_SIDE 64
#define TRACE_ORIGIN (-1024.f)
#define TRACE_REFS 32768
static int traceHead[TRACE_SIDE*TRACE_SIDE],traceNext[TRACE_REFS];
static uint16_t traceLine[TRACE_REFS];
static int traceReady,traceActive;
static void buildTraceGrid(const Render *r){
 memset(traceHead,0xff,sizeof(traceHead));int used=0;traceReady=0;
 for(int i=0;i<r->numLines;i++){
  const Line *l=&r->lines[i];if(l->flags&(HIDDEN|NO_DEPTH))continue;
  int x0=(int)floorf((fminf(l->x1,l->x2)-.01f-TRACE_ORIGIN)/64),x1=(int)floorf((fmaxf(l->x1,l->x2)+.01f-TRACE_ORIGIN)/64);
  int y0=(int)floorf((fminf(l->y1,l->y2)-.01f-TRACE_ORIGIN)/64),y1=(int)floorf((fmaxf(l->y1,l->y2)+.01f-TRACE_ORIGIN)/64);
  if(x0<0)x0=0;if(y0<0)y0=0;if(x1>=TRACE_SIDE)x1=TRACE_SIDE-1;if(y1>=TRACE_SIDE)y1=TRACE_SIDE-1;
  for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
   if(used==TRACE_REFS)return;
   int cell=y*TRACE_SIDE+x;traceLine[used]=(uint16_t)i;traceNext[used]=traceHead[cell];traceHead[cell]=used++;
  }
 }
 traceReady=1;
}
static int lineOccludes(const Render *r,const Line *l,float ax,float ay,float az,float bx,float by,float bz){
 float dx=bx-ax,dy=by-ay,ex=(float)(l->x2-l->x1),ey=(float)(l->y2-l->y1),den=dx*ey-dy*ex;
 if(fabsf(den)<.0001f)return 0;
 float qx=l->x1-ax,qy=l->y1-ay,t=(qx*ey-qy*ex)/den,u=(qx*dy-qy*dx)/den;
 if(t>.015f&&t<.985f&&u>=0&&u<=1){
  float z=az+(bz-az)*t,bottom=r->effFloor,top=64;
  if(l->flags&0xc0010000u){top=r->ceilHeight;bottom=(l->flags&0x10000u)?r->floorHeight:64;}
  if(z>bottom-.5f&&z<top+.5f)return 1;
 }
 return 0;
}
static int visibleReference(const Render *r,float ax,float ay,float az,float bx,float by,float bz){
 for(int i=0;i<r->numLines;i++){
  const Line *l=&r->lines[i];if(l->flags&(HIDDEN|NO_DEPTH))continue;
  if(lineOccludes(r,l,ax,ay,az,bx,by,bz))return 0;
 }
 return 1;
}
int BrewLight_visible(const Render *r,float ax,float ay,float az,float bx,float by,float bz){
 int x=(int)floorf((ax-TRACE_ORIGIN)/64),y=(int)floorf((ay-TRACE_ORIGIN)/64);
 int endX=(int)floorf((bx-TRACE_ORIGIN)/64),endY=(int)floorf((by-TRACE_ORIGIN)/64);
 if(!traceActive||!traceReady||x<0||y<0||endX<0||endY<0||x>=TRACE_SIDE||y>=TRACE_SIDE||endX>=TRACE_SIDE||endY>=TRACE_SIDE)return visibleReference(r,ax,ay,az,bx,by,bz);
 float dx=bx-ax,dy=by-ay;int sx=dx>0?1:-1,sy=dy>0?1:-1;
 float tx=dx==0?INFINITY:(TRACE_ORIGIN+(x+(sx>0))*64-ax)/dx,ty=dy==0?INFINITY:(TRACE_ORIGIN+(y+(sy>0))*64-ay)/dy;
 float stepX=dx==0?INFINITY:64/fabsf(dx),stepY=dy==0?INFINITY:64/fabsf(dy);
 uint32_t visited[24]={0};
 for(int step=0;step<TRACE_SIDE*2+2;step++){
  for(int ref=traceHead[y*TRACE_SIDE+x];ref>=0;ref=traceNext[ref]){
   int i=traceLine[ref];uint32_t bit=1u<<(i&31);if(visited[i>>5]&bit)continue;visited[i>>5]|=bit;
   if(lineOccludes(r,&r->lines[i],ax,ay,az,bx,by,bz))return 0;
  }
  if(x==endX&&y==endY)return 1;
  if(tx<ty){x+=sx;tx+=stepX;}else{y+=sy;ty+=stepY;}
  if(x<0||y<0||x>=TRACE_SIDE||y>=TRACE_SIDE)break;
 }
 return visibleReference(r,ax,ay,az,bx,by,bz);
}
#ifdef OE2_QA_INTERACTIONS
int BrewLight_qaTraceCheck(void){
 Render *copy=malloc(sizeof(Render));if(!copy)return 0;uint32_t random=0x81c2a34u;int valid=1;
 for(int pass=0;pass<2&&valid;pass++){
  memcpy(copy,g_app->render,sizeof(Render));
  if(pass){for(int i=0;i<copy->numLines;i+=11){copy->lines[i].flags^=HIDDEN;copy->lines[i].x1+=2;}copy->floorHeight=copy->effFloor=-16;copy->ceilHeight=copy->effCeil=128;}
  buildTraceGrid(copy);traceActive=1;
  for(int n=0;n<24000;n++){
   float point[6];for(int i=0;i<6;i++){random=random*1664525u+1013904223u;point[i]=i%3==2?(int)(random%256)-32:(int)(random%2304)-128+.125f;}
   if(n%4==0){point[0]=(n%33)*64;point[1]=((n/33)%33)*64;}
   if(n%8==1)point[3]=point[0];if(n%8==2)point[4]=point[1];
   if(n%8==3&&copy->numLines){Line *l=&copy->lines[n%copy->numLines];point[3]=l->x1;point[4]=l->y1;}
   if(n%64==0)point[0]=-2048;
   if(BrewLight_visible(copy,point[0],point[1],point[2],point[3],point[4],point[5])!=visibleReference(copy,point[0],point[1],point[2],point[3],point[4],point[5])){valid=0;break;}
  }
 }
 traceActive=0;geometryHash=0;traceReady=0;free(copy);return valid;
}
#endif
/* Only direct lighting/shadows are evaluated inside the processing region.
 * The final step fades to ambient; source powers and physical falloff stay fixed. */
static float worldLightWeight(float x,float y){
 float dx=x-processingX,dy=y-processingY,d2=dx*dx+dy*dy;
 float reach=processingSteps*WALK_STEP,start=reach-WALK_STEP;
 if(d2>=reach*reach)return 0;
 if(d2<=start*start)return 1;
 float t=(reach-sqrtf(d2))/WALK_STEP;return t*t*(3-2*t);
}
/* Processing is a full circle around the controlled player. Retain a source
 * whose influence disk intersects it, regardless of camera direction. */
static int lightInRegion(float distanceSquared,float radius){
 float reach=processingSteps*WALK_STEP+radius;return distanceSquared<=reach*reach;
}
static float selectionScore(int key,float distanceSquared){
 /* A challenger must be meaningfully closer before displacing an incumbent.
  * This prevents near-equal sources from swapping every movement substep. */
 for(int i=0;i<retainedCount;i++)if(retainedKeys[i]==key)return distanceSquared*.8f;
 return distanceSquared;
}
#ifdef OE2_QA_INTERACTIONS
int BrewLight_qaRadiusCheck(void){
 int saved=processingSteps,valid=1;const int levels[]={5,15,20};
 for(int level=0;level<3;level++){
  processingSteps=levels[level];float reach=processingSteps*WALK_STEP;
  for(int angle=0;angle<64;angle++){
   float a=angle*6.28318530718f/64,x=cosf(a)*(reach+40),y=sinf(a)*(reach+40);
   if(!lightInRegion(x*x+y*y,50))valid=0;
   x=cosf(a)*(reach+60);y=sinf(a)*(reach+60);if(lightInRegion(x*x+y*y,50))valid=0;
  }
 }
 processingSteps=saved;return valid;
}
int BrewLight_qaSelectedKey(int index){return index>=0&&index<numLights?lights[index].key:-1;}
#endif
static void candidate(Light value,float cx,float cy){
 value.radius*=LIGHT_REFERENCE_SCALE;
 float d=(value.x-cx)*(value.x-cx)+(value.y-cy)*(value.y-cy);
#ifdef OE2_QA_INTERACTIONS
 profile[5]++;
#endif
 if(!lightInRegion(d,value.radius)){
#ifdef OE2_QA_INTERACTIONS
  profile[6]++;
#endif
  return;
 }
 if(numLights<7){lights[numLights++]=value;return;}
 int worst=0;float far=-1;
 for(int i=0;i<7;i++){
  if(lights[i].key>=3000&&lights[i].key<3008&&!(value.key>=3000&&value.key<3008))continue;
  float a=lights[i].x-cx,b=lights[i].y-cy,n=selectionScore(lights[i].key,a*a+b*b);if(n>far){far=n;worst=i;}
 }
 if(far>=0&&((value.key>=3000&&value.key<3008)||selectionScore(value.key,d)<far))lights[worst]=value;
}
/* Wall identities 61/69 contain the small candle in the skull-and-bars niche.
 * The flame is at texel (79,91) of the original 117-square artwork. z1/z2
 * preserve its texture coordinate even on mirrored and repeated segments. */
static void wallCandles(Render *r,const Line *line,int key,float zOffset){
 if(line->flags&HIDDEN)return;
 float dx=line->x2-line->x1,dy=line->y2-line->y1,len=sqrtf(dx*dx+dy*dy),du=line->z2-line->z1;
 if(len<.01f||fabsf(du)<.01f)return;
 int multi=(line->flags&0xc0010000u)!=0;
 float top=multi?r->ceilHeight:64,bottom=multi?((line->flags&0x10000u)?r->floorHeight:64):r->effFloor;
 int dual=multi&&r->ceilHeight>64&&line->texture!=line->texture2,piece=0;
 for(;top>bottom;top-=64,piece++){
  int tile=dual&&piece%2==0?line->texture2:line->texture;
  float z=top-91.f*64/117+zOffset;
  if((tile!=61&&tile!=69)||z<bottom||z>top+zOffset||((line->flags&0x20000u)&&z>r->effCeil))continue;
  float low=fminf(line->z1,line->z2),high=fmaxf(line->z1,line->z2),flame=79.f*64/117;
  int first=(int)ceilf((low-flame)/64),last=(int)floorf((high-flame)/64);
  for(int repeat=first;repeat<=last;repeat++){
   float t=(flame+repeat*64-line->z1)/du;
   for(int side=0;side<((line->flags&1)?2:1);side++){
    float sign=side?-1:1;
    Light l={line->x1+t*dx-sign*dy/len*4,line->y1+t*dy+sign*dx/len*4,z,
     1,.63f,.24f,155,1.4f,1,key+piece*64+(repeat-first)*2+side};
    candidate(l,processingX,processingY);
   }
  }
  if(!multi)break;
 }
}
/* The same niche also appears as five wall-aligned decorations in map 6.
 * Match Render_drawSpriteAt's orientation, mirroring, width and vertical origin. */
static void spriteWallCandle(Render *r,const Sprite *s,int key){
 int tile=s->info&255;
 if((s->info&0x10000u)||!(s->info&0x40000u)||(tile!=61&&tile!=69)||((s->info&0x80000000u)&&((s->info>>8)&15)>=4))return;
 int half=s->scale/2,reverse=(s->info&0x1000000u)!=0;uint32_t f=s->info;
 Line l={.z1=0,.z2=half*2,.texture=tile,.texture2=tile,.flags=NO_DEPTH|0x20000u};
 if(f&0x4000000u)l.flags|=1;
 if(f&0x180000u){
  int sign=((f&0x80000u)?1:-1)*(reverse?-1:1);
  l.x1=s->x-sign*half;l.x2=s->x+sign*half;l.y1=l.y2=s->y;
 }else if(f&0x600000u){
  int sign=((f&0x200000u)?1:-1)*(reverse?-1:1);
  l.y1=s->y-sign*half;l.y2=s->y+sign*half;l.x1=l.x2=s->x;
 }else{
  float rx=r->viewSin/65536.f,ry=r->viewCos/65536.f,sign=reverse?-1:1;
  l.x1=(int)(s->x-sign*rx*half);l.x2=(int)(s->x+sign*rx*half);
  l.y1=(int)(s->y-sign*ry*half);l.y2=(int)(s->y+sign*ry*half);
 }
 wallCandles(r,&l,key,s->z-32.f);
}
#ifdef OE2_QA_INTERACTIONS
static int wallCandlesEnabled=1;
void BrewLight_qaWallCandles(int enabled){wallCandlesEnabled=enabled!=0;}
int BrewLight_qaCandleGeometry(void){
 Light saved[8];memcpy(saved,lights,sizeof(saved));int oldCount=numLights;float oldX=processingX,oldY=processingY;
 Render *r=calloc(1,sizeof(Render));r->viewX=64;r->viewY=32;r->ceilHeight=64;processingX=64;processingY=32;
 Line l={.x1=0,.y1=0,.x2=128,.y2=0,.z1=0,.z2=128,.texture=61};
 float flame=79.f*64/117;
 numLights=0;wallCandles(r,&l,10000,0);
 int ok=numLights==2&&fabsf(lights[0].x-flame)<.01f&&lights[0].y==4;
 l.z1=128;l.z2=0;numLights=0;wallCandles(r,&l,10000,0);
 ok&=numLights==2&&fabsf(lights[0].x-(128-flame))<.01f;
 l.flags=1;numLights=0;wallCandles(r,&l,10000,0);ok&=numLights==4&&lights[1].y==-4;
 l.flags=HIDDEN;numLights=0;wallCandles(r,&l,10000,0);ok&=numLights==0;
 l.flags=0;r->effFloor=30;wallCandles(r,&l,10000,0);ok&=numLights==0;
 l.flags=0x10000;l.texture2=69;r->ceilHeight=128;numLights=0;wallCandles(r,&l,10000,0);ok&=numLights==4;
 free(r);memcpy(lights,saved,sizeof(saved));numLights=oldCount;processingX=oldX;processingY=oldY;return ok;
}
#endif
#ifdef OE2_QA_INTERACTIONS
int BrewLight_qaSourcePoint(int index,int field){
 Render *r=g_app->render;
 for(int i=0;i<r->numMapSprites;i++){
  Sprite *s=&r->sprites[i];int tile=s->info&255;
  if(!sourceForSprite(s)&&!((s->info&0x40000u)&&!(s->info&0x10000u)&&(tile==61||tile==69)))continue;
  if(index--==0)return field==0?s->x:field==1?s->y:s->z;
 }
 for(int i=0;i<r->numLines;i++){
  Line *l=&r->lines[i];if((l->flags&HIDDEN)||(l->texture!=61&&l->texture!=69))continue;
  if(index--==0)return field==0?(l->x1+l->x2)/2:field==1?(l->y1+l->y2)/2:32;
 }
 return -1;
}
#endif
#ifdef OE2_NO_SPRITE_SHADOWS
typedef struct {uint32_t validGen;int u,v,iz,sprites;float c00,c10,c01,c11;} FloorShadowCache;
typedef struct {uint32_t validGen;int ix,iy,axis,layer;float d00,d10,d01,d11;} RoomDistCache;
typedef struct {
 FloorShadowCache floorShadowCache[8];
 RoomDistCache roomDistCache;
 uint16_t weaponEnergyQ10[3];
 uint32_t weaponValidGen;
} LightWorkerCtx;
static LightWorkerCtx lightWorkerCtx[LIGHT_THREADS];
static int staticCacheMap=-1,staticCacheLines=-1,staticCacheMapSprites=-1,staticCacheFlats=-1;
static const Render *staticCacheRender=NULL;
static int wallCandleLines[256],numWallCandleLines,wallCandleOverflow;
static int spriteCandles[64],numSpriteCandles,spriteCandleOverflow;
static uint16_t lavaTiles[1024],numLavaTiles;
#else
typedef struct {int valid,u,v,iz,sprites;float c00,c10,c01,c11;} FloorShadowCache;
static FloorShadowCache floorShadowCache[8];
typedef struct {int valid,ix,iy,axis,layer;float d00,d10,d01,d11;} RoomDistCache;
static RoomDistCache roomDistCache;
#endif
static void gather(Render *r){
#ifdef OE2_QA_INTERACTIONS
 profile[5]=profile[6]=0;
#endif
 Canvas *view=g_app->canvas;
 processingX=view->viewX;processingY=view->viewY;
 if(retainedMap!=view->mapId){retainedMap=view->mapId;retainedCount=0;}
 numLights=0;
 for(int i=0;i<8;i++){
  const Impact *impact=&impacts[i];int age=g_app->time-impact->time;
  if(!impact->active||impact->map!=g_app->canvas->mapId||age<0||age>=320)continue;
  Light l=impact->light;float decay=1-age/320.f;l.power*=decay*decay;
  candidate(l,processingX,processingY);
 }
 for(int i=0;i<r->numSprites&&i<800;i++){
  Sprite *s=&r->sprites[i];const Source *p=sourceForSprite(s);if(!p)continue;
  Light l={(float)s->x,(float)s->y,s->z+p->height,p->r,p->g,p->b,p->radius,p->power,p->flicker,i};candidate(l,processingX,processingY);
 }
#ifdef OE2_NO_SPRITE_SHADOWS
 if(staticCacheMap!=view->mapId||staticCacheLines!=r->numLines||staticCacheMapSprites!=r->numMapSprites||staticCacheFlats!=r->numFlats||staticCacheRender!=r){
  staticCacheMap=view->mapId;staticCacheLines=r->numLines;staticCacheMapSprites=r->numMapSprites;staticCacheFlats=r->numFlats;staticCacheRender=r;
  numWallCandleLines=0;wallCandleOverflow=0;
  for(int i=0;i<r->numLines;i++){
   const Line *l=&r->lines[i];
   if(l->texture==61||l->texture==69||l->texture2==61||l->texture2==69){
    if(numWallCandleLines<256)wallCandleLines[numWallCandleLines++]=i;
    else wallCandleOverflow=1;
   }
  }
  numSpriteCandles=0;spriteCandleOverflow=0;
  for(int i=0;i<r->numMapSprites;i++){
   int tile=r->sprites[i].info&255;
   if(tile==61||tile==69){
    if(numSpriteCandles<64)spriteCandles[numSpriteCandles++]=i;
    else spriteCandleOverflow=1;
   }
  }
  numLavaTiles=0;
  const uint16_t *lavaFlat=NULL;
  for(int slot=0;slot<r->numFlats;slot++)if(r->flatIds[slot]==199){lavaFlat=(const uint16_t *)((uintptr_t)r->flats+(size_t)slot*4096*sizeof(uint16_t));break;}
  if(lavaFlat)for(int i=0;i<1024;i++)if(r->floorTiles[i]==lavaFlat)lavaTiles[numLavaTiles++]=(uint16_t)i;
 }
 if(!wallCandleOverflow){
  for(int k=0;k<numWallCandleLines;k++){int i=wallCandleLines[k];wallCandles(r,&r->lines[i],10000+i*256,0);}
 }else{
  for(int i=0;i<r->numLines;i++){
   const Line *l=&r->lines[i];
   if(l->texture==61||l->texture==69||l->texture2==61||l->texture2==69)wallCandles(r,l,10000+i*256,0);
  }
 }
 if(!spriteCandleOverflow){
  for(int k=0;k<numSpriteCandles;k++){int i=spriteCandles[k];spriteWallCandle(r,&r->sprites[i],300000+i*256);}
 }else{
  for(int i=0;i<r->numMapSprites;i++)spriteWallCandle(r,&r->sprites[i],300000+i*256);
 }
 for(int k=0;k<numLavaTiles;k++){
  int i=lavaTiles[k];
  Light l={(i%32)*64.f+32,(i/32)*64.f+32,r->effFloor+4.f,1,.25f,.04f,160,1.0f,1,1000+i};candidate(l,processingX,processingY);
 }
#else
 #ifdef OE2_QA_INTERACTIONS
 if(wallCandlesEnabled)
#endif
 {
 for(int i=0;i<r->numLines;i++){
  const Line *l=&r->lines[i];
  if(l->texture==61||l->texture==69||l->texture2==61||l->texture2==69)wallCandles(r,l,10000+i*256,0);
 }
 for(int i=0;i<r->numMapSprites;i++)spriteWallCandle(r,&r->sprites[i],300000+i*256);
 }
 /* Original molten-crack flat 199 is emissive only where that material occurs. */
 const uint16_t *lavaFlat=NULL;
 for(int slot=0;slot<r->numFlats;slot++)if(r->flatIds[slot]==199){lavaFlat=(const uint16_t *)((uintptr_t)r->flats+(size_t)slot*4096*sizeof(uint16_t));break;}
 if(lavaFlat)for(int i=0;i<1024;i++)if(r->floorTiles[i]==lavaFlat){
  Light l={(i%32)*64.f+32,(i/32)*64.f+32,r->effFloor+4.f,1,.25f,.04f,160,1.0f,1,1000+i};candidate(l,processingX,processingY);
 }
#endif
 for(int i=0;i<numLights;i++)for(int j=i+1;j<numLights;j++)if(lights[j].key<lights[i].key){Light a=lights[i];lights[i]=lights[j];lights[j]=a;}
 retainedCount=numLights;for(int i=0;i<retainedCount;i++)retainedKeys[i]=lights[i].key;
 Player *p=g_app->player;Canvas *c=g_app->canvas;
 float a=(c->viewAngle&1023)*(6.28318530718f/1024);
 Light l={c->viewX+cosf(a)*14,c->viewY-sinf(a)*14,c->viewZ-5.f,1,.62f,.28f,105,.28f,0,2000};
 if(p&&p->weapons){
  int w=p->ce.weapon;
  if(w==5||w==6||w==7){
   l.r=1;l.g=.38f;l.b=.08f;l.radius=210;l.power=1.15f;
  }else if(w>=8){
   l.radius=210;l.power=.86f;
   if(w==8){l.r=1;l.g=.18f;l.b=.10f;}
   else if(w==9){l.r=.25f;l.g=1;l.b=.35f;}
   else if(w==10){l.r=.35f;l.g=.65f;l.b=1;}
  }
 }
 l.radius*=LIGHT_REFERENCE_SCALE;lights[numLights++]=l;
 uint32_t h=2166136261u;
 for(int i=0;i<r->numLines;i++){
  Line *l=&r->lines[i];h=hashWord(h,(uint32_t)l->x1);h=hashWord(h,(uint32_t)l->y1);h=hashWord(h,(uint32_t)l->x2);h=hashWord(h,(uint32_t)l->y2);h=hashWord(h,l->flags&(HIDDEN|NO_DEPTH|0xc0010000u));
 }
 h=hashWord(h,(uint32_t)r->effFloor);h=hashWord(h,(uint32_t)r->effCeil);
 h=hashWord(h,(uint32_t)r->floorHeight);h=hashWord(h,(uint32_t)r->ceilHeight);
 if(h!=geometryHash){
  geometryHash=h;
#ifdef OE2_NO_SPRITE_SHADOWS
  memset((void *)wallCheckedAtomic,0,sizeof(wallCheckedAtomic));
  memset(wallLightHash,0,sizeof(wallLightHash));
#else
  roomDistCache.valid=0;memset(wallChecked,0,sizeof(wallChecked));
#endif
  buildTraceGrid(r);
 }
#ifdef OE2_NO_SPRITE_SHADOWS
 uint32_t targetWallHash[8]={0};
 unsigned char slotUsed[8]={0};
 for(int j=0;j<numLights;j++){
  casterCount[j]=0;lightWallSlot[j]=-1;
  if(lights[j].key==2000){lightWallSlot[j]=7;continue;}
  uint32_t wallHash=hashWord(geometryHash,(uint32_t)lights[j].key);
  wallHash=hashWord(wallHash,floatBits(lights[j].x));wallHash=hashWord(wallHash,floatBits(lights[j].y));wallHash=hashWord(wallHash,floatBits(lights[j].z));
  targetWallHash[j]=wallHash;
  for(int s=0;s<8;s++){
   if(!slotUsed[s]&&wallLightHash[s]==wallHash&&wallHash!=0){
    lightWallSlot[j]=s;slotUsed[s]=1;break;
   }
  }
 }
 for(int j=0;j<numLights;j++){
  if(lightWallSlot[j]>=0)continue;
  int pick=-1;
  for(int s=0;s<8;s++)if(!slotUsed[s]&&wallLightHash[s]==0){pick=s;break;}
  if(pick<0)for(int s=0;s<8;s++)if(!slotUsed[s]){pick=s;break;}
  if(pick<0)pick=j&7;
  slotUsed[pick]=1;lightWallSlot[j]=pick;
  wallLightHash[pick]=targetWallHash[j];
  uint32_t dirtyCount=atomic_load_explicit(&numDirtyWallAtomic[pick],memory_order_relaxed);
  uint16_t clearMask=(uint16_t)~(3u<<(pick*2));
  if(dirtyCount<=MAX_DIRTY_WALL_CELLS){
   for(uint32_t k=0;k<dirtyCount;k++){
    int cell=dirtyWallCells[pick][k];
    uint16_t oldVal=atomic_load_explicit(&wallVisState[cell],memory_order_relaxed);
    atomic_store_explicit(&wallVisState[cell],(uint16_t)(oldVal&clearMask),memory_order_relaxed);
   }
  }else{
   for(int cell=0;cell<GRID_CELLS;cell++){
    uint16_t oldVal=atomic_load_explicit(&wallVisState[cell],memory_order_relaxed);
    if((oldVal&(3u<<(pick*2)))!=0)atomic_store_explicit(&wallVisState[cell],(uint16_t)(oldVal&clearMask),memory_order_relaxed);
   }
  }
  atomic_store_explicit(&numDirtyWallAtomic[pick],0u,memory_order_relaxed);
 }
 for(int i=0;i<numLights;i++){h=hashWord(h,(uint32_t)lights[i].key);h=hashWord(h,(uint32_t)(int)(lights[i].x/16));h=hashWord(h,(uint32_t)(int)(lights[i].y/16));h=hashWord(h,(uint32_t)(int)(lights[i].z/8));}
 cachedHash=h;
#else
 for(int j=0;j<numLights;j++){
#ifndef OE2_QA_INTERACTIONS
  if(lights[j].key==2000){casterCount[j]=0;continue;}
#endif
  uint32_t wallHash=hashWord(geometryHash,(uint32_t)lights[j].key);
  wallHash=hashWord(wallHash,floatBits(lights[j].x));wallHash=hashWord(wallHash,floatBits(lights[j].y));wallHash=hashWord(wallHash,floatBits(lights[j].z));
  if(wallHash!=wallLightHash[j]){
   wallLightHash[j]=wallHash;
   if(numDirtyWall[j]<=MAX_DIRTY_WALL_BYTES){for(int k=0;k<numDirtyWall[j];k++)wallSampleReady[j][dirtyWallBytes[j][k]]=0;}
   else memset(wallSampleReady[j],0,sizeof(wallSampleReady[j]));
   numDirtyWall[j]=0;
  }
 }
 static int activeCasterList[800];
 int numActiveCasters=0;
 for(int i=0;i<r->numSprites&&i<800;i++){
  casters[i]=spriteCaster(r,&r->sprites[i]);
  if(casters[i].active&&posedCasters[i].active)casters[i]=posedCasters[i];
  if(casters[i].active)activeCasterList[numActiveCasters++]=i;
 }
 unsigned invalidMask=0;
 for(int j=0;j<numLights;j++){
  casterCount[j]=0;
#ifndef OE2_QA_INTERACTIONS
  if(lights[j].key==2000)continue;
#endif
  uint32_t lh=wallLightHash[j];
  lh=hashWord(lh,(uint32_t)(int)(lights[j].x/16));lh=hashWord(lh,(uint32_t)(int)(lights[j].y/16));lh=hashWord(lh,(uint32_t)(int)(lights[j].z/8));
  for(int k=0;k<numActiveCasters;k++){
   int i=activeCasterList[k];
   Caster *c=&casters[i];float dx=c->x-lights[j].x,dy=c->y-lights[j].y,range=lights[j].radius+c->size;
   if(dx*dx+dy*dy>range*range)continue;
   lightCasters[j][casterCount[j]++]=i;
   lh=hashWord(lh,(unsigned)i);lh=hashWord(lh,(unsigned)(int)c->x);lh=hashWord(lh,(unsigned)(int)c->y);lh=hashWord(lh,(unsigned)(int)c->z);lh=hashWord(lh,(unsigned)c->frame);lh=hashWord(lh,(unsigned)(int)(c->size*16));lh=hashWord(lh,c->flags);
   h=hashWord(h,(unsigned)i);h=hashWord(h,(unsigned)(int)c->x);h=hashWord(h,(unsigned)(int)c->y);h=hashWord(h,(unsigned)(int)c->z);h=hashWord(h,(unsigned)c->frame);h=hashWord(h,(unsigned)(int)(c->size*16));h=hashWord(h,c->flags);
  }
  if(lh!=lightCacheHash[j]){lightCacheHash[j]=lh;invalidMask|=1u<<j;}
 }
 for(int j=numLights;j<8;j++)if(lightCacheHash[j]){lightCacheHash[j]=0;invalidMask|=1u<<j;}
 for(int i=0;i<numLights;i++){h=hashWord(h,(uint32_t)lights[i].key);h=hashWord(h,(uint32_t)(int)(lights[i].x/16));h=hashWord(h,(uint32_t)(int)(lights[i].y/16));h=hashWord(h,(uint32_t)(int)(lights[i].z/8));}
 cachedHash=h;
 if(invalidMask){
  if(numDirtyChecked<=MAX_DIRTY_CELLS){
   int kept=0;unsigned char keepMask=(unsigned char)~invalidMask;
   for(int k=0;k<numDirtyChecked;k++){
    int cell=dirtyCheckedCells[k];unsigned char rem=checked[cell]&keepMask;
    checked[cell]=rem;if(rem)dirtyCheckedCells[kept++]=cell;
   }
   numDirtyChecked=kept;
  }else{
   memset(checked,0,sizeof(checked));numDirtyChecked=0;memset(lightCacheHash,0,sizeof(lightCacheHash));
  }
 }
#endif
}
static float visibilityAt(int ix,int iy,int iz,int light,int sprites){
#ifdef OE2_NO_SPRITE_SHADOWS
 (void)sprites;
 const Light *l=&lights[light];
 if(ix<0||ix>=128||iy<0||iy>=128||iz<0||iz>=24)return 1.f;
 int cell=(iz*128+iy)*128+ix,slot=lightWallSlot[light];
 unsigned shift=(unsigned)(slot*2);
 uint16_t bits=atomic_load_explicit(&wallVisState[cell],memory_order_relaxed);
 uint16_t state=(bits>>shift)&3u;
 if(state)return state==2u?1.f:0.f;
 uint16_t vis=BrewLight_visible(renderer,ix*16+8.f,iy*16+8.f,iz*8+4.f,l->x,l->y,l->z)?2u:1u;
 uint16_t mask=(uint16_t)(3u<<shift),newBits=(uint16_t)(vis<<shift);
 uint16_t expected=bits;
 while(!atomic_compare_exchange_weak_explicit(&wallVisState[cell],&expected,(uint16_t)((expected&~mask)|newBits),memory_order_relaxed,memory_order_relaxed)){}
 if((expected&mask)==0u){
  uint32_t idx=atomic_fetch_add_explicit(&numDirtyWallAtomic[slot],1u,memory_order_relaxed);
  if(idx<MAX_DIRTY_WALL_CELLS)dirtyWallCells[slot][idx]=cell;
 }
 return vis==2u?1.f:0.f;
#else
 const Light *l=&lights[light];
 if(ix<0||ix>=128||iy<0||iy>=128||iz<0||iz>=24)return 1;
 int cell=(iz*128+iy)*128+ix;unsigned bit=1u<<light;
 if(!(checked[cell]&bit)){
  float x=ix*16+8.f,y=iy*16+8.f,z=iz*8+4.f;int hits=0,wallHits=0;
  unsigned shift=light*3,mask=(wallSamples[cell]>>shift)&7u,readyBit=1u<<(cell&7);
  int byteIdx=cell>>3;
#ifdef OE2_QA_INTERACTIONS
  if(!(wallSampleReady[light][byteIdx]&readyBit)){
   mask=0;
   for(int sample=-1;sample<=1;sample++)if(BrewLight_visible(renderer,x,y,z,l->x+sample*3,l->y-sample*2,l->z))mask|=1u<<(sample+1);
   wallSamples[cell]=(wallSamples[cell]&~(7u<<shift))|(mask<<shift);
   if(!wallSampleReady[light][byteIdx]){
    if(numDirtyWall[light]<MAX_DIRTY_WALL_BYTES)dirtyWallBytes[light][numDirtyWall[light]++]=byteIdx;
    else numDirtyWall[light]=MAX_DIRTY_WALL_BYTES+1;
   }
   wallSampleReady[light][byteIdx]|=(uint8_t)readyBit;
  }
  for(int sample=-1;sample<=1;sample++){
   float bx=l->x+sample*3,by=l->y-sample*2;int visible=(mask>>(sample+1))&1;wallHits+=visible;
   if(visible)for(int i=0;i<casterCount[light];i++)if(casterBlocks(renderer,&casters[lightCasters[light][i]],x,y,z,bx,by,l->z)){visible=0;break;}
   hits+=visible;
  }
#else
  if(!(wallSampleReady[light][byteIdx]&readyBit)){
   mask=BrewLight_visible(renderer,x,y,z,l->x,l->y,l->z)?7u:0u;
   wallSamples[cell]=(wallSamples[cell]&~(7u<<shift))|(mask<<shift);
   if(!wallSampleReady[light][byteIdx]){
    if(numDirtyWall[light]<MAX_DIRTY_WALL_BYTES)dirtyWallBytes[light][numDirtyWall[light]++]=byteIdx;
    else numDirtyWall[light]=MAX_DIRTY_WALL_BYTES+1;
   }
   wallSampleReady[light][byteIdx]|=(uint8_t)readyBit;
  }
  int visible=(mask>>1)&1;
  wallHits=visible?3:0;
  if(visible)for(int i=0;i<casterCount[light];i++)if(casterBlocks(renderer,&casters[lightCasters[light][i]],x,y,z,l->x,l->y,l->z)){visible=0;break;}
  hits=visible?3:0;
#endif
  if(!checked[cell]){
   if(numDirtyChecked<MAX_DIRTY_CELLS)dirtyCheckedCells[numDirtyChecked++]=cell;
   else numDirtyChecked=MAX_DIRTY_CELLS+1;
  }
  visibility[cell]=(uint16_t)((visibility[cell]&~(3u<<(light*2)))|((unsigned)hits<<(light*2)));checked[cell]|=(unsigned char)bit;
  wallVisibility[cell]=(uint16_t)((wallVisibility[cell]&~(3u<<(light*2)))|((unsigned)wallHits<<(light*2)));
 }
 return (((sprites?visibility[cell]:wallVisibility[cell])>>(light*2))&3)/3.f;
#endif
}
static float mixf(float a,float b,float t){return a+(b-a)*t;}
#ifdef OE2_NO_SPRITE_SHADOWS
typedef struct {uint32_t validGen;int axis,fixed,u,v,sprites;float weight,low,high;} ShadowColumn;
#else
typedef struct {int valid,axis,fixed,u,v,sprites;float weight,low,high;} ShadowColumn;
#endif
static ShadowColumn shadowColumns[704][8];
#ifdef OE2_NO_SPRITE_SHADOWS
static float shadowCtx(const Surface *s,int light,int column,LightWorkerCtx *ctx){
#else
static float shadow(const Surface *s,int light,int column){
#endif
#ifdef OE2_QA_INTERACTIONS
 if(costMode==1)return 1;
#endif
 float sx=surfX(s),sy=surfY(s),sz=surfZ(s);int snz=surfNz(s);
 float x=(sx+s->nx/127.f*2)/16,y=(sy+s->ny/127.f*2)/16,z=(sz+snz/127.f*2)/8;
 int ix=(int)floorf(x),iy=(int)floorf(y),iz=(int)floorf(z);
 /* Interpolate along the receiver, never through its own wall plane. */
 float a,b;int u,v;
#ifdef OE2_NO_SPRITE_SHADOWS
 int sprites=0;
#else
 int sprites=surfKind(s)!=3;
#endif
 if(snz){a=x-.5f;b=y-.5f;u=(int)floorf(a);v=(int)floorf(b);
#ifdef OE2_NO_SPRITE_SHADOWS
  FloorShadowCache *fc=&ctx->floorShadowCache[light];
  if(fc->validGen!=shadowFrameGen||fc->u!=u||fc->v!=v||fc->iz!=iz||fc->sprites!=sprites){
   fc->validGen=shadowFrameGen;fc->u=u;fc->v=v;fc->iz=iz;fc->sprites=sprites;
#else
  FloorShadowCache *fc=&floorShadowCache[light];
  if(!fc->valid||fc->u!=u||fc->v!=v||fc->iz!=iz||fc->sprites!=sprites){
   fc->valid=1;fc->u=u;fc->v=v;fc->iz=iz;fc->sprites=sprites;
#endif
   fc->c00=visibilityAt(u,v,iz,light,sprites);fc->c10=visibilityAt(u+1,v,iz,light,sprites);
   fc->c01=visibilityAt(u,v+1,iz,light,sprites);fc->c11=visibilityAt(u+1,v+1,iz,light,sprites);
  }
  return mixf(mixf(fc->c00,fc->c10,a-u),mixf(fc->c01,fc->c11,a-u),b-v);
 }
 int axis=abs(s->nx)>=abs(s->ny),fixed=axis?ix:iy;
 a=(axis?y:x)-.5f;b=z-.5f;u=(int)floorf(a);v=(int)floorf(b);float weight=a-u;
 ShadowColumn *cache=column>=0&&column<704?&shadowColumns[column][light]:NULL;
#ifdef OE2_NO_SPRITE_SHADOWS
 if(cache&&cache->validGen==shadowFrameGen&&cache->axis==axis&&cache->fixed==fixed&&cache->u==u&&cache->v==v&&cache->weight==weight&&cache->sprites==sprites)return mixf(cache->low,cache->high,b-v);
#else
 if(cache&&cache->valid&&cache->axis==axis&&cache->fixed==fixed&&cache->u==u&&cache->v==v&&cache->weight==weight&&cache->sprites==sprites)return mixf(cache->low,cache->high,b-v);
#endif
 float low,high;
 if(axis){low=mixf(visibilityAt(ix,u,v,light,sprites),visibilityAt(ix,u+1,v,light,sprites),weight);high=mixf(visibilityAt(ix,u,v+1,light,sprites),visibilityAt(ix,u+1,v+1,light,sprites),weight);}
 else{low=mixf(visibilityAt(u,iy,v,light,sprites),visibilityAt(u+1,iy,v,light,sprites),weight);high=mixf(visibilityAt(u,iy,v+1,light,sprites),visibilityAt(u+1,iy,v+1,light,sprites),weight);}
#ifdef OE2_NO_SPRITE_SHADOWS
 if(cache)*cache=(ShadowColumn){shadowFrameGen,axis,fixed,u,v,sprites,weight,low,high};
#else
 if(cache)*cache=(ShadowColumn){1,axis,fixed,u,v,sprites,weight,low,high};
#endif
 return mixf(low,high,b-v);
}
static float wallDistanceAt(int x,int y,int axis,int layer){
 if(x<0||y<0||x>128||y>128)return 40;
 int cell=y*129+x;
#ifdef OE2_NO_SPRITE_SHADOWS
 if(atomic_load_explicit(&wallCheckedAtomic[layer][cell],memory_order_acquire)!=2u){
#else
 if(!wallChecked[layer][cell]){
#endif
  float px=x*16.f,py=y*16.f,best[2]={40*40,40*40};
  int gx0=(int)floorf((px-40.5f-TRACE_ORIGIN)/64),gx1=(int)floorf((px+40.5f-TRACE_ORIGIN)/64);
  int gy0=(int)floorf((py-40.5f-TRACE_ORIGIN)/64),gy1=(int)floorf((py+40.5f-TRACE_ORIGIN)/64);
  if(traceReady&&gx0>=0&&gy0>=0&&gx1<TRACE_SIDE&&gy1<TRACE_SIDE){
   for(int gy=gy0;gy<=gy1;gy++)for(int gx=gx0;gx<=gx1;gx++)for(int ref=traceHead[gy*TRACE_SIDE+gx];ref>=0;ref=traceNext[ref]){
    const Line *l=&renderer->lines[traceLine[ref]];
    if(layer<2){
     float z=layer==0?renderer->effFloor:renderer->effCeil,bottom=renderer->effFloor,top=64;
     if(l->flags&0xc0010000u){top=renderer->ceilHeight;bottom=(l->flags&0x10000u)?renderer->floorHeight:64;}
     if(z<bottom-.5f||z>top+.5f)continue;
    }
    float dx=l->x2-l->x1,dy=l->y2-l->y1,den=dx*dx+dy*dy;
    if(den<1)continue;
    float t=clampf(((px-l->x1)*dx+(py-l->y1)*dy)/den,0,1);
    float qx=px-l->x1-dx*t,qy=py-l->y1-dy*t,d=qx*qx+qy*qy;
    int normal=fabsf(dx)>fabsf(dy)?1:0;if(d<best[normal])best[normal]=d;
   }
  }else{
   for(int i=0;i<renderer->numLines;i++){
    const Line *l=&renderer->lines[i];if(l->flags&(HIDDEN|NO_DEPTH))continue;
    /* Upper wall pieces do not touch the floor beneath them. */
    if(layer<2){
     float z=layer==0?renderer->effFloor:renderer->effCeil,bottom=renderer->effFloor,top=64;
     if(l->flags&0xc0010000u){top=renderer->ceilHeight;bottom=(l->flags&0x10000u)?renderer->floorHeight:64;}
     if(z<bottom-.5f||z>top+.5f)continue;
    }
    float dx=l->x2-l->x1,dy=l->y2-l->y1,den=dx*dx+dy*dy;
    if(den<1)continue;
    float t=clampf(((px-l->x1)*dx+(py-l->y1)*dy)/den,0,1);
    float qx=px-l->x1-dx*t,qy=py-l->y1-dy*t,d=qx*qx+qy*qy;
    int normal=fabsf(dx)>fabsf(dy)?1:0;if(d<best[normal])best[normal]=d;
   }
  }
#ifdef OE2_NO_SPRITE_SHADOWS
  float d0=sqrtf(best[0]),d1=sqrtf(best[1]),dMin=fminf(d0,d1);
  uint8_t exp=0;
  if(atomic_compare_exchange_strong_explicit(&wallCheckedAtomic[layer][cell],&exp,1u,memory_order_relaxed,memory_order_relaxed)){
   wallAxisDistance[layer][0][cell]=d0;wallAxisDistance[layer][1][cell]=d1;wallDistance[layer][cell]=dMin;
   atomic_store_explicit(&wallCheckedAtomic[layer][cell],2u,memory_order_release);
  }
  return axis<0?dMin:(axis==0?d0:d1);
#else
  wallAxisDistance[layer][0][cell]=sqrtf(best[0]);wallAxisDistance[layer][1][cell]=sqrtf(best[1]);
  wallDistance[layer][cell]=fminf(wallAxisDistance[layer][0][cell],wallAxisDistance[layer][1][cell]);wallChecked[layer][cell]=1;
#endif
 }
 return axis<0?wallDistance[layer][cell]:wallAxisDistance[layer][axis][cell];
}
#ifdef OE2_NO_SPRITE_SHADOWS
static float roomDistanceCtx(const Surface *s,int axis,LightWorkerCtx *ctx){
 float x=surfX(s)/16,y=surfY(s)/16;int ix=(int)floorf(x),iy=(int)floorf(y),snz=surfNz(s);
 int layer=snz?(snz>0?0:1):2;
 RoomDistCache *rc=&ctx->roomDistCache;
 if(rc->validGen!=shadowFrameGen||rc->ix!=ix||rc->iy!=iy||rc->axis!=axis||rc->layer!=layer){
  rc->validGen=shadowFrameGen;rc->ix=ix;rc->iy=iy;rc->axis=axis;rc->layer=layer;
  rc->d00=wallDistanceAt(ix,iy,axis,layer);rc->d10=wallDistanceAt(ix+1,iy,axis,layer);
  rc->d01=wallDistanceAt(ix,iy+1,axis,layer);rc->d11=wallDistanceAt(ix+1,iy+1,axis,layer);
 }
 return mixf(mixf(rc->d00,rc->d10,x-ix),mixf(rc->d01,rc->d11,x-ix),y-iy);
}
static float contactShadeCtx(const Surface *s,int column,LightWorkerCtx *ctx){
#else
static float roomDistance(const Surface *s,int axis){
 float x=surfX(s)/16,y=surfY(s)/16;int ix=(int)floorf(x),iy=(int)floorf(y),snz=surfNz(s);
 int layer=snz?(snz>0?0:1):2;
 if(!roomDistCache.valid||roomDistCache.ix!=ix||roomDistCache.iy!=iy||roomDistCache.axis!=axis||roomDistCache.layer!=layer){
  roomDistCache.valid=1;roomDistCache.ix=ix;roomDistCache.iy=iy;roomDistCache.axis=axis;roomDistCache.layer=layer;
  roomDistCache.d00=wallDistanceAt(ix,iy,axis,layer);roomDistCache.d10=wallDistanceAt(ix+1,iy,axis,layer);
  roomDistCache.d01=wallDistanceAt(ix,iy+1,axis,layer);roomDistCache.d11=wallDistanceAt(ix+1,iy+1,axis,layer);
 }
 return mixf(mixf(roomDistCache.d00,roomDistCache.d10,x-ix),mixf(roomDistCache.d01,roomDistCache.d11,x-ix),y-iy);
}
static float contactShade(const Surface *s,int column){
#endif
#ifdef OE2_QA_INTERACTIONS
 if(costMode==2)return 1;
#endif
 int kind=surfKind(s);
 if(kind==4)return 1;
 float distance;
#ifdef OE2_NO_SPRITE_SHADOWS
 if(surfNz(s))distance=roomDistanceCtx(s,-1,ctx);
#else
 if(surfNz(s))distance=roomDistance(s,-1);
#endif
 else{
  float sz=surfZ(s);
  distance=fminf(fabsf(sz-renderer->effFloor),fabsf(renderer->effCeil-sz));
  if(kind==2){
   int axis=abs(s->nx)>=abs(s->ny)?1:0;float corner;
   /* Wall x/y and normal are constant down a column. Reuse their distance
    * across rows; floor and ceiling receivers still sample in world space. */
   if(column>=0){
    CornerCache *c=&cornerCache[column];
#ifdef OE2_NO_SPRITE_SHADOWS
    if(c->validGen!=shadowFrameGen||c->x!=s->x||c->y!=s->y||c->axis!=axis){
     c->x=s->x;c->y=s->y;c->axis=axis;c->distance=roomDistanceCtx(s,axis,ctx);c->validGen=shadowFrameGen;
    }
#else
    if(!c->valid||c->x!=s->x||c->y!=s->y||c->axis!=axis){
     c->x=s->x;c->y=s->y;c->axis=axis;c->distance=roomDistance(s,axis);c->valid=1;
    }
#endif
    corner=c->distance;
#ifdef OE2_NO_SPRITE_SHADOWS
   }else corner=roomDistanceCtx(s,axis,ctx);
#else
   }else corner=roomDistance(s,axis);
#endif
   distance=fminf(distance,corner);
  }
 }
 float t=clampf(distance/20,0,1);t=t*t*(3-2*t);
 return .35f+.65f*t;
}
#ifdef OE2_NO_SPRITE_SHADOWS
static inline float contactShade(const Surface *s,int column){return contactShadeCtx(s,column,&lightWorkerCtx[0]);}
#endif
#ifdef OE2_LIGHTING_TEST
int BrewLight_testContact(Render *r){
#ifdef OE2_NO_SPRITE_SHADOWS
 Render *saved=renderer;renderer=r;if(++shadowFrameGen==0){memset(lightWorkerCtx,0,sizeof(lightWorkerCtx));shadowFrameGen=1;}
 memset((void *)wallCheckedAtomic,0,sizeof(wallCheckedAtomic));
#else
 Render *saved=renderer;renderer=r;roomDistCache.valid=0;memset(wallChecked,0,sizeof(wallChecked));
#endif
 Surface floor,far,ceiling,wall,middle,corner;
 surfSet(&floor,64,64,0,0,0,127,1);far=floor;surfSet(&far,96,64,0,0,0,127,1);
 surfSet(&ceiling,64,64,64,0,0,-127,1);
 surfSet(&wall,64,64,0,127,0,0,2);
 surfSet(&middle,64,64,32,127,0,0,2);
 surfSet(&corner,64,0,32,127,0,0,2);
 r->numLines=2;r->lines[1]=(Line){.x1=64,.y1=0,.x2=128,.y2=0,.flags=0x10000};
 int valid=contactShade(&floor,-1)<.36f&&contactShade(&ceiling,-1)<.36f&&contactShade(&far,-1)>.99f;
 valid=valid&&contactShade(&wall,-1)<.36f&&contactShade(&middle,-1)>.99f&&contactShade(&corner,-1)<.36f;
#ifdef OE2_NO_SPRITE_SHADOWS
 r->numLines=1;r->lines[0].flags=0x40000000u;r->ceilHeight=r->effCeil=128;if(++shadowFrameGen==0){memset(lightWorkerCtx,0,sizeof(lightWorkerCtx));shadowFrameGen=1;}
 memset((void *)wallCheckedAtomic,0,sizeof(wallCheckedAtomic));
 valid=valid&&contactShade(&floor,-1)>.99f;
 renderer=saved;if(++shadowFrameGen==0){memset(lightWorkerCtx,0,sizeof(lightWorkerCtx));shadowFrameGen=1;}
 memset((void *)wallCheckedAtomic,0,sizeof(wallCheckedAtomic));return valid;
#else
 r->numLines=1;r->lines[0].flags=0x40000000u;r->ceilHeight=r->effCeil=128;roomDistCache.valid=0;memset(wallChecked,0,sizeof(wallChecked));
 valid=valid&&contactShade(&floor,-1)>.99f;
 renderer=saved;roomDistCache.valid=0;memset(wallChecked,0,sizeof(wallChecked));return valid;
#endif
}
#endif
static void glow(const Light *l,float flicker){
 /* Screen-space halo is depth-tested against real walls. Held light is lit
  * in weapon space instead, avoiding a floating glow in the camera centre. */
 if(l->key==2000)return;
 float weight=worldLightWeight(l->x,l->y);if(weight<=0)return;
 float dx=l->x-renderer->viewX,dy=l->y-renderer->viewY,d=dx*fx+dy*fy;
 if(d<12||!BrewLight_visible(renderer,renderer->viewX,renderer->viewY,renderer->viewZ,l->x,l->y,l->z))return;
 float focalW=renderer->projH>0?(float)(renderer->projH>>16):(float)width;
 float cx=width*.5f+(dx*rx+dy*ry)*focalW/(2*d),cy=height*.5f-(l->z-renderer->viewZ)*focalW/(2*d);
 float scale=focalW/176.f;
 float radius=clampf(focalW*5.5f/d,1.5f*scale,12*scale),colour[3]={l->r,l->g,l->b};
 int x0=(int)fmaxf(0,cx-radius),x1=(int)fminf(width-1,cx+radius),y0=(int)fmaxf(0,cy-radius),y1=(int)fminf(height-1,cy+radius);
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
  float ax=(x-cx)/radius,ay=(y-cy)/radius,r2=ax*ax+ay*ay;if(r2>1)continue;
  const Surface *s=&surfaces[y*width+x];int kind=surfKind(s);
  float receiver=(surfX(s)-renderer->viewX)*fx+(surfY(s)-renderer->viewY)*fy;
  if(kind==4||(kind&&receiver+12<d))continue;
  float strength=.008f*l->power*flicker*weight*expf(-r2*4)*(fadeLevel/255.f)*(fadeLevel/255.f),out=0;
  uint32_t c=lit[y*width+x],pixel=0xff000000u;
  for(int ch=0;ch<3;ch++){float v=((c>>(ch*8))&255)/255.f;out=sqrtf(fminf(1,v*v+colour[ch]*strength));pixel|=(uint32_t)(out*255+.5f)<<(ch*8);}
  lit[y*width+x]=pixel;
 }
}
static int weaponEnergyValid;
static float weaponEnergy[3];
#ifdef OE2_NO_SPRITE_SHADOWS
static void incidentEnergyCtx(const Surface *s,const float *flicker,int column,float *energy,LightWorkerCtx *ctx){
  float ao=contactShadeCtx(s,column,ctx);energy[0]=.17f*ao;energy[1]=.20f*ao;energy[2]=.27f*ao;
  float sx=surfX(s),sy=surfY(s),sz=surfZ(s);int kind=surfKind(s),snz=surfNz(s);
  float weight=kind==4?1:worldLightWeight(sx,sy);if(weight<=0)return;
  for(int j=0;j<numLights;j++){
   const Light *l=&lights[j];float dx=l->x-sx,dy=l->y-sy,dz=l->z-sz,d2=dx*dx+dy*dy+dz*dz;
   if(d2>=l->radius*l->radius)continue;
   float d=sqrtf(d2)+.01f,lambert=clampf((s->nx*dx+s->ny*dy+snz*dz)/(127*d),0,1);
   float attenuation=1-d/l->radius;
   float e=weight*attenuation*attenuation*l->power*flicker[j]*2.1f*(.18f+.82f*lambert);
#ifdef OE2_QA_INTERACTIONS
   if(kind!=4)e*=shadowCtx(s,j,column,ctx)*(.75f+.25f*ao);
#else
   if(kind!=4&&l->key!=2000)e*=shadowCtx(s,j,column,ctx)*(.75f+.25f*ao);
#endif
   energy[0]+=l->r*e;energy[1]+=l->g*e;energy[2]+=l->b*e;
  }
  if(kind==4&&g_app&&g_app->player&&g_app->player->weapons){
   int w=g_app->player->ce.weapon;
   if(w==0||w==5||w==6||w==7||w>=8)for(int ch=0;ch<3;ch++)energy[ch]=fmaxf(energy[ch]*1.4f,1.25f);
  }
}
static inline void incidentEnergy(const Surface *s,const float *flicker,int column,float *energy){incidentEnergyCtx(s,flicker,column,energy,&lightWorkerCtx[0]);}
#else
static void incidentEnergy(const Surface *s,const float *flicker,int column,float *energy){
  float ao=contactShade(s,column);energy[0]=.17f*ao;energy[1]=.20f*ao;energy[2]=.27f*ao;
  float sx=surfX(s),sy=surfY(s),sz=surfZ(s);int kind=surfKind(s),snz=surfNz(s);
  float weight=kind==4?1:worldLightWeight(sx,sy);if(weight<=0)return;
  for(int j=0;j<numLights;j++){
   const Light *l=&lights[j];float dx=l->x-sx,dy=l->y-sy,dz=l->z-sz,d2=dx*dx+dy*dy+dz*dz;
   if(d2>=l->radius*l->radius)continue;
   float d=sqrtf(d2)+.01f,lambert=clampf((s->nx*dx+s->ny*dy+snz*dz)/(127*d),0,1);
   float attenuation=1-d/l->radius;
   float e=weight*attenuation*attenuation*l->power*flicker[j]*2.1f*(.18f+.82f*lambert);
#ifdef OE2_QA_INTERACTIONS
   if(kind!=4)e*=shadow(s,j,column)*(.75f+.25f*ao);
#else
   if(kind!=4&&l->key!=2000)e*=shadow(s,j,column)*(.75f+.25f*ao);
#endif
   energy[0]+=l->r*e;energy[1]+=l->g*e;energy[2]+=l->b*e;
  }
  if(kind==4&&g_app&&g_app->player&&g_app->player->weapons){
   int w=g_app->player->ce.weapon;
   if(w==0||w==5||w==6||w==7||w>=8)for(int ch=0;ch<3;ch++)energy[ch]=fmaxf(energy[ch]*1.4f,1.25f);
  }
}
#endif
/* Irradiance is smooth at a much finer scale than the 16-unit shadow field.
 * Reuse one-world-unit endpoints only where several full-resolution pixels
 * land between them. Material pixels and receiver boundaries stay untouched. */
typedef struct {int valid,index,kind,exact;float x,y,z;int8_t nx,ny,nz;float low[3],high[3];} EnergySpan;
static EnergySpan verticalEnergy[704],horizontalEnergy[880];
static void energyProbe(Surface *probe,float coordinate,const FloorDirection *direction){
#ifdef OE2_NO_SPRITE_SHADOWS
 float px=surfX(probe),py=surfY(probe),pz=surfZ(probe);
 int snz=surfNz(probe),kind=surfKind(probe);
 if(!direction){pz=coordinate;}
 else{
  float delta=coordinate-(direction->axis?direction->x:direction->y);
  px=direction->axis?coordinate:direction->x+delta*direction->ratio;
  py=direction->axis?direction->y+delta*direction->ratio:coordinate;
 }
 surfSet(probe,px,py,pz,probe->nx,probe->ny,(signed char)snz,(unsigned char)kind);
#else
 if(!direction){probe->z=coordinate;return;}
 float delta=coordinate-(direction->axis?direction->x:direction->y);
 probe->x=direction->axis?coordinate:direction->x+delta*direction->ratio;
 probe->y=direction->axis?direction->y+delta*direction->ratio:coordinate;
#endif
}
static void sampleEnergy(const Surface *s,const float *flicker,int column,int row,float *energy){
#ifdef OE2_QA_INTERACTIONS
 if(costMode==3){incidentEnergy(s,flicker,column,energy);return;}
#endif
 int kind=surfKind(s);
 if(width<=176||kind==4){incidentEnergy(s,flicker,column,energy);return;}
 float sx=surfX(s),sy=surfY(s),sz=surfZ(s);int snz=surfNz(s);
 float coordinate;EnergySpan *cache;FloorDirection *direction=NULL;
 if(snz){
  if(row<0||row>=880||!floorDirections[row].valid||floorDirections[row].step>=.5f){incidentEnergy(s,flicker,column,energy);return;}
  direction=&floorDirections[row];cache=&horizontalEnergy[row];coordinate=direction->axis?sx:sy;
 }else{
  float depth=(sx-renderer->viewX)*fx+(sy-renderer->viewY)*fy;
  if(depth*2/width>=.5f||column<0||column>=704){incidentEnergy(s,flicker,column,energy);return;}
  coordinate=sz;cache=&verticalEnergy[column];
 }
 float keyX=direction?direction->x:sx,keyY=direction?direction->y:sy,keyZ=direction?sz:0;
 int index=(int)floorf(coordinate);int same=cache->valid&&cache->kind==kind&&cache->x==keyX&&cache->y==keyY&&cache->z==keyZ&&cache->nx==s->nx&&cache->ny==s->ny&&cache->nz==snz;
 /* A point emitter can have a sharp normal response on its own surface.
  * Keep exact per-pixel shading along receivers within eight world units. */
 if(!same){
  int exact=0;
  for(int j=0;j<numLights&&!exact;j++){
   float dx=lights[j].x-keyX,dy=lights[j].y-keyY,distance=dx*dx+dy*dy;
   if(direction){float lateral=direction->axis?dy-dx*direction->ratio:dx-dy*direction->ratio,dz=lights[j].z-sz;distance=lateral*lateral/(1+direction->ratio*direction->ratio)+dz*dz;}
   exact=distance<64;
  }
  if(exact){*cache=(EnergySpan){.valid=1,.exact=1,.kind=kind,.x=keyX,.y=keyY,.z=keyZ,.nx=s->nx,.ny=s->ny,.nz=(int8_t)snz};incidentEnergy(s,flicker,column,energy);return;}
 }
 if(same&&cache->exact){incidentEnergy(s,flicker,column,energy);return;}
 if(!same||cache->index!=index){
  Surface probe=*s;float low[3],high[3];
  int reuseLow=same&&cache->index+1==index,reuseHigh=same&&cache->index-1==index;
  if(reuseLow)memcpy(low,cache->high,sizeof(low));
  if(reuseHigh)memcpy(high,cache->low,sizeof(high));
  if(!reuseLow){energyProbe(&probe,(float)index,direction);incidentEnergy(&probe,flicker,column,low);}
  if(!reuseHigh){energyProbe(&probe,(float)(index+1),direction);incidentEnergy(&probe,flicker,column,high);}
  *cache=(EnergySpan){.valid=1,.index=index,.kind=kind,.x=keyX,.y=keyY,.z=keyZ,.nx=s->nx,.ny=s->ny,.nz=(int8_t)snz};memcpy(cache->low,low,sizeof(low));memcpy(cache->high,high,sizeof(high));
 }
 for(int ch=0;ch<3;ch++)energy[ch]=mixf(cache->low[ch],cache->high[ch],coordinate-index);
}
static int lutReady;
static float byteToLinear[256];
#ifdef OE2_NO_SPRITE_SHADOWS
static uint16_t byteToLinearQ10[256];
#endif
static uint8_t sqrtLUT[4097];
static uint8_t toneLUT[4097];
static void initLUT(void){
 if(lutReady)return;
 for(int i=0;i<256;i++){
  float v=i/255.f;byteToLinear[i]=v*v;
#ifdef OE2_NO_SPRITE_SHADOWS
  byteToLinearQ10[i]=(uint16_t)(v*v*1024.f+.5f);
#endif
 }
 for(int i=0;i<=4096;i++){
  sqrtLUT[i]=(uint8_t)(sqrtf(i/4096.f)*255.f+.5f);
  float lin=i*(4.f/4096.f);
  float mapped=lin>1.f?lin/(1.f+(lin-1.f)*.7f):lin;
  toneLUT[i]=(uint8_t)(clampf(sqrtf(mapped),0.f,1.f)*255.f+.5f);
 }
 lutReady=1;
}
static inline uint32_t shadePixelWithEnergy(const Surface *s,uint32_t c,const float *energy){
 float sx=surfX(s),sy=surfY(s);int kind=surfKind(s);
#ifdef OE2_QA_INTERACTIONS
 uint32_t out=0xff000000u;
 float depth=(sx-renderer->viewX)*fx+(sy-renderer->viewY)*fy;
 float haze=kind==4?0:.12f*clampf((depth-128)/384,0,1)*(fadeLevel/255.f),mist[3]={.012f,.022f,.035f};
 for(int ch=0;ch<3;ch++){
  float v=((c>>(ch*8))&255)/255.f,linear=v*v*energy[ch];
  linear=mixf(linear/(1+fmaxf(0,linear-1)*.7f),mist[ch],haze);
  out|=(uint32_t)(clampf(sqrtf(linear),0,1)*255+.5f)<<(ch*8);
 }
 return out;
#else
 float depth=(sx-renderer->viewX)*fx+(sy-renderer->viewY)*fy;
 if(kind==4||depth<=128.f||fadeLevel<=0){
  int i0=(int)(byteToLinear[c&255]*energy[0]*1024.f+.5f);
  int i1=(int)(byteToLinear[(c>>8)&255]*energy[1]*1024.f+.5f);
  int i2=(int)(byteToLinear[(c>>16)&255]*energy[2]*1024.f+.5f);
  if(i0<0)i0=0;else if(i0>4096)i0=4096;
  if(i1<0)i1=0;else if(i1>4096)i1=4096;
  if(i2<0)i2=0;else if(i2>4096)i2=4096;
  return 0xff000000u|((uint32_t)toneLUT[i0])|(((uint32_t)toneLUT[i1])<<8)|(((uint32_t)toneLUT[i2])<<16);
 }
 float haze=.12f*clampf((depth-128.f)*(1.f/384.f),0,1)*(fadeLevel*(1.f/255.f));
 float invHaze=1.f-haze;
 float m0=.012f*haze,m1=.022f*haze,m2=.035f*haze;
 int i0=(int)((byteToLinear[c&255]*energy[0]*invHaze+m0)*1024.f+.5f);
 int i1=(int)((byteToLinear[(c>>8)&255]*energy[1]*invHaze+m1)*1024.f+.5f);
 int i2=(int)((byteToLinear[(c>>16)&255]*energy[2]*invHaze+m2)*1024.f+.5f);
 if(i0<0)i0=0;else if(i0>4096)i0=4096;
 if(i1<0)i1=0;else if(i1>4096)i1=4096;
 if(i2<0)i2=0;else if(i2>4096)i2=4096;
 return 0xff000000u|((uint32_t)toneLUT[i0])|(((uint32_t)toneLUT[i1])<<8)|(((uint32_t)toneLUT[i2])<<16);
#endif
}
static uint32_t shadePixel(const Surface *s,uint32_t c,const float *flicker,int column,int row){
  int kind=surfKind(s);
  if(!kind)return c;
  float energy[3];
  if(kind==4&&weaponEnergyValid)memcpy(energy,weaponEnergy,sizeof(energy));
  else{sampleEnergy(s,flicker,column,row,energy);if(kind==4){memcpy(weaponEnergy,energy,sizeof(energy));weaponEnergyValid=1;}}
  return shadePixelWithEnergy(s,c,energy);
}
int BrewLight_weaponBottom(void){
 int bottom=-1;for(int i=0;i<width*height;i++)if(surfKind(&surfaces[i])==4)bottom=i/width;
 return bottom;
}
static int staffShift(void){
 if(!weaponCopied||!g_app||g_app->player->ce.weapon!=1||g_app->player->isFamiliar||g_app->player->statusEffects[9].turns)return 0;
 int bottom=BrewLight_weaponBottom();return bottom>=0?height-1-bottom:0;
}
#ifndef OE2_QA_INTERACTIONS
#ifdef OE2_NO_SPRITE_SHADOWS
static uint16_t lowResEnergyQ10[400*240][3];
static unsigned char lowResReady[400*240];
static uint8_t lowResGen=0;
static int lowResGenW=0,lowResGenH=0;
static void samplePointQ10(const Surface *s,const float *flicker,int column,uint16_t outQ10[3],LightWorkerCtx *ctx){
 int kind=surfKind(s);
 if(kind==4&&ctx->weaponValidGen==shadowFrameGen){
  outQ10[0]=ctx->weaponEnergyQ10[0];outQ10[1]=ctx->weaponEnergyQ10[1];outQ10[2]=ctx->weaponEnergyQ10[2];
  return;
 }
 float energy[3];
 incidentEnergyCtx(s,flicker,column,energy,ctx);
 if(kind!=4&&fadeLevel>0){
  float depth=(surfX(s)-renderer->viewX)*fx+(surfY(s)-renderer->viewY)*fy;
  if(depth>128.f){
   float haze=.12f*clampf((depth-128.f)*(1.f/384.f),0.f,1.f)*(fadeLevel*(1.f/255.f));
   float invHaze=1.f-haze;
   energy[0]=energy[0]*invHaze+.035f*haze;
   energy[1]=energy[1]*invHaze+.065f*haze;
   energy[2]=energy[2]*invHaze+.105f*haze;
  }
 }
 for(int ch=0;ch<3;ch++){
  int q=(int)(energy[ch]*1024.f+.5f);
  outQ10[ch]=(uint16_t)(q<0?0:(q>8192?8192:q));
 }
 if(kind==4){
  ctx->weaponEnergyQ10[0]=outQ10[0];ctx->weaponEnergyQ10[1]=outQ10[1];ctx->weaponEnergyQ10[2]=outQ10[2];
  ctx->weaponValidGen=shadowFrameGen;
 }
}
static inline int sameSurfNormal(const Surface *a,const Surface *b){
 return surfRawKind(a)==surfRawKind(b)&&a->nx==b->nx&&a->ny==b->ny;
}
static inline int compatibleSurface(const Surface *a,const Surface *b,const Surface *c){
 int kb=surfRawKind(b);
 if(kb!=1&&kb!=2&&kb!=3&&kb!=5)return 0;
 if(surfRawKind(a)!=kb||surfRawKind(c)!=kb)return 0;
 if(a->nx!=b->nx||a->ny!=b->ny||c->nx!=b->nx||c->ny!=b->ny)return 0;
 int dx=a->x-c->x;if(dx<0)dx=-dx;
 int dy=a->y-c->y;if(dy<0)dy=-dy;
 int dz=(a->zKind>>3)-(c->zKind>>3);if(dz<0)dz=-dz;
 return (dx+dy+dz)<(48*4);
}
static inline int compatibleSpan(const Surface *s0,const Surface *sMid,const Surface *sEnd,int step){
 int k0=surfRawKind(s0);
 if(k0!=1&&k0!=2&&k0!=3&&k0!=5)return 0;
 if(surfRawKind(sMid)!=k0||surfRawKind(sEnd)!=k0)return 0;
 if(sMid->nx!=s0->nx||sMid->ny!=s0->ny||sEnd->nx!=s0->nx||sEnd->ny!=s0->ny)return 0;
 int dx=s0->x-sEnd->x;if(dx<0)dx=-dx;
 int dy=s0->y-sEnd->y;if(dy<0)dy=-dy;
 int dz=(s0->zKind>>3)-(sEnd->zKind>>3);if(dz<0)dz=-dz;
 if((dx+dy+dz)>=(step>4?128*4:64*4))return 0;
 if(k0==3){
  int mx=sMid->x-((s0->x+sEnd->x)>>1);if(mx<0)mx=-mx;
  int my=sMid->y-((s0->y+sEnd->y)>>1);if(my<0)my=-my;
  if(mx+my>16*4)return 0;
 }
 return 1;
}
static void lightRunRange(LightWorkerCtx *ctx,const uint32_t *original,const float *flicker,int w,int h,int shift,int xStep,int xShift,unsigned char readyTag,int cx0,int cx1,int phase){
 const Surface *surfBase=surfaces;
 if(phase==0){
  for(int y=0;y<h;y+=2){
   for(int x=cx0;x<cx1;x+=xStep){
    int i=y*w+x;const Surface *s=&surfBase[i];
    if(shift&&surfKind(s)==4)s=&underWeapon[i];
    if(!surfKind(s))continue;
    samplePointQ10(s,flicker,x,lowResEnergyQ10[i],ctx);
    lowResReady[i]=readyTag;
   }
   if(cx1==w&&w>0&&((w-1)&(xStep-1))!=0){
    int x=w-1,i=y*w+x;const Surface *s=&surfBase[i];
    if(shift&&surfKind(s)==4)s=&underWeapon[i];
    if(surfKind(s)){
     samplePointQ10(s,flicker,x,lowResEnergyQ10[i],ctx);
     lowResReady[i]=readyTag;
    }
   }
  }
  return;
 }
 for(int y=0;y<h;y+=2){
  for(int x=cx0;x<cx1;x+=xStep){
   int i0=y*w+x,xEnd=x+xStep<w?x+xStep:w;
   if(x+xStep<w&&lowResReady[i0]==readyTag&&lowResReady[i0+xStep]==readyTag){
    const Surface *s0=&surfBase[i0],*sMid=&surfBase[i0+(xStep>>1)],*sEnd=&surfBase[i0+xStep];
    if(shift){
     if(surfKind(s0)==4)s0=&underWeapon[i0];
     if(surfKind(sMid)==4)sMid=&underWeapon[i0+(xStep>>1)];
     if(surfKind(sEnd)==4)sEnd=&underWeapon[i0+xStep];
    }
    if(compatibleSpan(s0,sMid,sEnd,xStep)){
     for(int ch=0;ch<3;ch++){
      int e0=lowResEnergyQ10[i0][ch],de=lowResEnergyQ10[i0+xStep][ch]-e0;
      for(int k=1;k<xStep;k++)lowResEnergyQ10[i0+k][ch]=(uint16_t)(e0+((de*k)>>xShift));
     }
     for(int k=1;k<xStep;k++)lowResReady[i0+k]=readyTag;
     continue;
    }
   }
   const Surface *s0=(shift&&surfKind(&surfBase[i0])==4)?&underWeapon[i0]:&surfBase[i0];
   const Surface *sEnd=(x+xStep<w)?((shift&&surfKind(&surfBase[i0+xStep])==4)?&underWeapon[i0+xStep]:&surfBase[i0+xStep]):NULL;
   for(int xx=x+1;xx<xEnd;xx++){
    int i=y*w+xx;if(lowResReady[i]==readyTag)continue;
    const Surface *s=&surfBase[i];
    if(shift&&surfKind(s)==4)s=&underWeapon[i];
    int sk=surfKind(s);
    if(!sk)continue;
    const Surface *sPrev=(shift&&surfKind(&surfBase[i-1])==4)?&underWeapon[i-1]:&surfBase[i-1];
    if(sk==4&&ctx->weaponValidGen==shadowFrameGen){
     memcpy(lowResEnergyQ10[i],ctx->weaponEnergyQ10,sizeof(ctx->weaponEnergyQ10));
    }else if(lowResReady[i-1]==readyTag&&sameSurfNormal(s,sPrev)){
     memcpy(lowResEnergyQ10[i],lowResEnergyQ10[i-1],sizeof(lowResEnergyQ10[0]));
    }else if(lowResReady[i0]==readyTag&&sameSurfNormal(s,s0)){
     memcpy(lowResEnergyQ10[i],lowResEnergyQ10[i0],sizeof(lowResEnergyQ10[0]));
    }else if(sEnd&&lowResReady[i0+xStep]==readyTag&&sameSurfNormal(s,sEnd)){
     memcpy(lowResEnergyQ10[i],lowResEnergyQ10[i0+xStep],sizeof(lowResEnergyQ10[0]));
    }else{
     samplePointQ10(s,flicker,xx,lowResEnergyQ10[i],ctx);
    }
    lowResReady[i]=readyTag;
   }
  }
 }
 for(int y=1;y<h;y+=2){
  for(int x=cx0;x<cx1;x++){
   int i=y*w+x;const Surface *s=&surfBase[i];
   if(shift&&surfKind(s)==4)s=&underWeapon[i];
   int sk=surfKind(s);
   if(!sk)continue;
   if(y+1<h){
    const Surface *sU=&surfBase[i-w],*sD=&surfBase[i+w];
    if(shift&&surfKind(sU)==4)sU=&underWeapon[i-w];
    if(shift&&surfKind(sD)==4)sD=&underWeapon[i+w];
    if(lowResReady[i-w]==readyTag&&lowResReady[i+w]==readyTag&&compatibleSurface(sU,s,sD)){
     for(int ch=0;ch<3;ch++)lowResEnergyQ10[i][ch]=(uint16_t)((lowResEnergyQ10[i-w][ch]+lowResEnergyQ10[i+w][ch])>>1);
     lowResReady[i]=readyTag;continue;
    }
    if(lowResReady[i-w]==readyTag&&sameSurfNormal(sU,s)){
     memcpy(lowResEnergyQ10[i],lowResEnergyQ10[i-w],sizeof(lowResEnergyQ10[0]));
     lowResReady[i]=readyTag;continue;
    }
    if(lowResReady[i+w]==readyTag&&sameSurfNormal(sD,s)){
     memcpy(lowResEnergyQ10[i],lowResEnergyQ10[i+w],sizeof(lowResEnergyQ10[0]));
     lowResReady[i]=readyTag;continue;
    }
   }else if(lowResReady[i-w]==readyTag){
    const Surface *sU=(shift&&surfKind(&surfBase[i-w])==4)?&underWeapon[i-w]:&surfBase[i-w];
    if(sameSurfNormal(sU,s)){
     memcpy(lowResEnergyQ10[i],lowResEnergyQ10[i-w],sizeof(lowResEnergyQ10[0]));
     lowResReady[i]=readyTag;continue;
    }
   }
   if(sk==4&&ctx->weaponValidGen==shadowFrameGen){
    memcpy(lowResEnergyQ10[i],ctx->weaponEnergyQ10,sizeof(ctx->weaponEnergyQ10));
    lowResReady[i]=readyTag;continue;
   }
   if((x&(xStep-1))!=0&&lowResReady[i-1]==readyTag){
    const Surface *sL=(shift&&surfKind(&surfBase[i-1])==4)?&underWeapon[i-1]:&surfBase[i-1];
    if(sameSurfNormal(sL,s)){
     memcpy(lowResEnergyQ10[i],lowResEnergyQ10[i-1],sizeof(lowResEnergyQ10[0]));
     lowResReady[i]=readyTag;continue;
    }
   }
   samplePointQ10(s,flicker,x,lowResEnergyQ10[i],ctx);
   lowResReady[i]=readyTag;
  }
 }
 if(!shift){
  for(int y=0;y<h;y++){
   int row=y*w;
   for(int x=cx0;x<cx1;x++){
    int i=row+x;uint32_t c=original[i];
    if(lowResReady[i]!=readyTag){lit[i]=c;continue;}
    const uint16_t *e=lowResEnergyQ10[i];
    uint32_t i0=((uint32_t)byteToLinearQ10[c&255]*e[0]+512u)>>10;
    uint32_t i1=((uint32_t)byteToLinearQ10[(c>>8)&255]*e[1]+512u)>>10;
    uint32_t i2=((uint32_t)byteToLinearQ10[(c>>16)&255]*e[2]+512u)>>10;
    if(i0>4096u)i0=4096u;
    if(i1>4096u)i1=4096u;
    if(i2>4096u)i2=4096u;
    lit[i]=0xff000000u|(uint32_t)toneLUT[i0]|(((uint32_t)toneLUT[i1])<<8)|(((uint32_t)toneLUT[i2])<<16);
   }
  }
 }else{
  for(int y=0;y<h;y++){
   int row=y*w;
   for(int x=cx0;x<cx1;x++){
    int i=row+x;uint32_t c=(surfKind(&surfBase[i])==4)?weaponBackground[i]:original[i];
    if(lowResReady[i]!=readyTag){lit[i]=c;continue;}
    const uint16_t *e=lowResEnergyQ10[i];
    uint32_t i0=((uint32_t)byteToLinearQ10[c&255]*e[0]+512u)>>10;
    uint32_t i1=((uint32_t)byteToLinearQ10[(c>>8)&255]*e[1]+512u)>>10;
    uint32_t i2=((uint32_t)byteToLinearQ10[(c>>16)&255]*e[2]+512u)>>10;
    if(i0>4096u)i0=4096u;
    if(i1>4096u)i1=4096u;
    if(i2>4096u)i2=4096u;
    lit[i]=0xff000000u|(uint32_t)toneLUT[i0]|(((uint32_t)toneLUT[i1])<<8)|(((uint32_t)toneLUT[i2])<<16);
   }
  }
 }
}
#ifdef OE2_LIGHT_MT
typedef SceUID LightSema;
static inline int lightSemaInit(LightSema *s,const char *name,int maxVal){*s=sceKernelCreateSema(name,0,0,maxVal,NULL);return *s>=0;}
static inline void lightSemaPost(LightSema *s,int count){sceKernelSignalSema(*s,count);}
static inline void lightSemaWait(LightSema *s){sceKernelWaitSema(*s,1,NULL);}
typedef struct {
 const uint32_t *original;
 const float *flicker;
 int w,h,shift,xStep,xShift,chunkW,numChunks;
 unsigned char readyTag;
} LightJob;
static LightJob curLightJob;
static _Atomic int lightWorkPhase;
static _Atomic int lightNextChunk;
static LightSema lightStartSema[LIGHT_THREADS];
static LightSema lightDoneSema;
static int lightWorkersState=0;
static void lightRunWorkerChunks(LightWorkerCtx *ctx){
 atomic_thread_fence(memory_order_acquire);
 const LightJob *job=&curLightJob;
 int phase=atomic_load_explicit(&lightWorkPhase,memory_order_relaxed);
 for(;;){
  int c=atomic_fetch_add_explicit(&lightNextChunk,1,memory_order_relaxed);
  if(c>=job->numChunks)break;
  int cx0=c*job->chunkW,cx1=cx0+job->chunkW;if(cx1>job->w)cx1=job->w;
  lightRunRange(ctx,job->original,job->flicker,job->w,job->h,job->shift,job->xStep,job->xShift,job->readyTag,cx0,cx1,phase);
 }
}
static int lightWorkerEntry(SceSize args,void *argp){
 (void)args;int tid=*(int *)argp;
 for(;;){
  lightSemaWait(&lightStartSema[tid]);
  lightRunWorkerChunks(&lightWorkerCtx[tid]);
  atomic_thread_fence(memory_order_release);
  lightSemaPost(&lightDoneSema,1);
 }
 return 0;
}
static int initLightWorkers(void){
 if(lightWorkersState)return lightWorkersState>0;
 if(!lightSemaInit(&lightDoneSema,"oe2_ldone",LIGHT_THREADS)){lightWorkersState=-1;return 0;}
 for(int t=1;t<LIGHT_THREADS;t++){
  if(!lightSemaInit(&lightStartSema[t],"oe2_lgo",2)){lightWorkersState=-1;return 0;}
  static const int cpuMask[LIGHT_THREADS]={SCE_KERNEL_CPU_MASK_USER_0,SCE_KERNEL_CPU_MASK_USER_1,SCE_KERNEL_CPU_MASK_USER_2};
  int prio=sceKernelGetThreadCurrentPriority();if(prio<0)prio=0x10000100;
  SceUID thid=sceKernelCreateThread("oe2_light",lightWorkerEntry,prio,0x10000,0,cpuMask[t],NULL);
  if(thid<0){lightWorkersState=-1;return 0;}
  int arg=t;
  if(sceKernelStartThread(thid,sizeof(arg),&arg)<0){lightWorkersState=-1;return 0;}
 }
 lightWorkersState=1;
 return 1;
}
#endif
#else
static float lowResEnergy[400*240][3];
static unsigned char lowResReady[400*240];
static uint8_t lowResGen=0;
static int lowResGenW=0,lowResGenH=0;
static int compatibleSurface(const Surface *a,const Surface *b,const Surface *c){
 if(b->kind!=1&&b->kind!=2&&b->kind!=3)return 0;
 if(a->kind!=b->kind||c->kind!=b->kind)return 0;
 if(a->nx!=b->nx||a->ny!=b->ny||a->nz!=b->nz||c->nx!=b->nx||c->ny!=b->ny||c->nz!=b->nz)return 0;
 float d=fabsf(a->x-c->x)+fabsf(a->y-c->y)+fabsf(a->z-c->z);
 return d<48.f;
}
static int compatibleSpan(const Surface *s0,const Surface *sMid,const Surface *sEnd,int step){
 if(s0->kind!=1&&s0->kind!=2)return 0;
 if(sMid->kind!=s0->kind||sEnd->kind!=s0->kind)return 0;
 if(sMid->nx!=s0->nx||sMid->ny!=s0->ny||sMid->nz!=s0->nz||sEnd->nx!=s0->nx||sEnd->ny!=s0->ny||sEnd->nz!=s0->nz)return 0;
 float d=fabsf(s0->x-sEnd->x)+fabsf(s0->y-sEnd->y)+fabsf(s0->z-sEnd->z);
 return d<(step>4?128.f:64.f);
}
#endif
#endif
static const uint32_t *present(const uint32_t *original,int w,int h,int lighting){
 if(!renderer||w!=width||h!=height)return original;
 int shift=staffShift();if(!lighting&&!shift)return original;
 initLUT();
 float flicker[8];weaponEnergyValid=0;
#ifdef OE2_NO_SPRITE_SHADOWS
 if(++shadowFrameGen==0){
  memset(lightWorkerCtx,0,sizeof(lightWorkerCtx));
  memset(shadowColumns,0,(size_t)w*sizeof(shadowColumns[0]));
  memset(cornerCache,0,(size_t)w*sizeof(cornerCache[0]));
  shadowFrameGen=1;
 }
#else
 roomDistCache.valid=0;
 memset(floorShadowCache,0,sizeof(floorShadowCache));
 memset(shadowColumns,0,(size_t)w*sizeof(shadowColumns[0]));
#endif
#ifdef OE2_QA_INTERACTIONS
 if(w>176){
  memset(verticalEnergy,0,(size_t)w*sizeof(verticalEnergy[0]));
  memset(horizontalEnergy,0,(size_t)h*sizeof(horizontalEnergy[0]));
 }
#else
 if(w>176&&(w>400||h>240)){
  memset(verticalEnergy,0,(size_t)w*sizeof(verticalEnergy[0]));
  memset(horizontalEnergy,0,(size_t)h*sizeof(horizontalEnergy[0]));
 }
#endif
#ifdef OE2_QA_INTERACTIONS
 double started=emscripten_get_now(),shadingStarted;
#endif
 if(lighting){
#ifndef OE2_NO_SPRITE_SHADOWS
  memset(cornerCache,0,(size_t)w*sizeof(cornerCache[0]));
#endif
  gather(renderer);
  for(int j=0;j<numLights;j++){
   int phase=lights[j].key%31; /* source identity, independent of processing range */
   flicker[j]=lights[j].flicker?(.92f+.06f*sinf(g_app->time*.021f+phase*2)+.04f*sinf(g_app->time*.047f+phase)):1;
  }
 }
#ifdef OE2_QA_INTERACTIONS
 shadingStarted=emscripten_get_now();profile[0]=shadingStarted-started;profile[3]=numLights;profile[4]=0;
 for(int j=0;j<numLights;j++)profile[4]+=casterCount[j];
#endif
 traceActive=lighting;
#ifndef OE2_QA_INTERACTIONS
 if(lighting&&w<=400&&h<=240){
  const Surface *surfBase=surfaces;
  int xStep=(w>176)?8:4;
#ifdef OE2_NO_SPRITE_SHADOWS
  if(++lowResGen==0||lowResGenW!=w||lowResGenH!=h){
   memset(lowResReady,0,(size_t)w*(size_t)h);
   lowResGen=1;lowResGenW=w;lowResGenH=h;
  }
  unsigned char readyTag=lowResGen;
  int xShift=(w>176)?3:2;
#ifdef OE2_LIGHT_MT
  if(w>176&&initLightWorkers()){
   int chunkW=xStep*4,numChunks=(w+chunkW-1)/chunkW;
   curLightJob=(LightJob){original,flicker,w,h,shift,xStep,xShift,chunkW,numChunks,readyTag};
   for(int phase=0;phase<2;phase++){
    atomic_store_explicit(&lightWorkPhase,phase,memory_order_relaxed);
    atomic_store_explicit(&lightNextChunk,0,memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    for(int t=1;t<LIGHT_THREADS;t++)lightSemaPost(&lightStartSema[t],1);
    lightRunWorkerChunks(&lightWorkerCtx[0]);
    for(int t=1;t<LIGHT_THREADS;t++)lightSemaWait(&lightDoneSema);
    atomic_thread_fence(memory_order_acquire);
   }
  }else
#endif
  {
   lightRunRange(&lightWorkerCtx[0],original,flicker,w,h,shift,xStep,xShift,readyTag,0,w,0);
   lightRunRange(&lightWorkerCtx[0],original,flicker,w,h,shift,xStep,xShift,readyTag,0,w,1);
  }
#else
  memset(lowResReady,0,(size_t)w*(size_t)h);
  for(int y=0;y<h;y+=2){
   for(int x=0;x<w;x+=xStep){
    int i=y*w+x;const Surface *s=&surfBase[i];
    if(shift&&s->kind==4)s=&underWeapon[i];
    if(!s->kind)continue;
    if(s->kind==4&&weaponEnergyValid)memcpy(lowResEnergy[i],weaponEnergy,sizeof(weaponEnergy));
    else{incidentEnergy(s,flicker,x,lowResEnergy[i]);if(s->kind==4){memcpy(weaponEnergy,lowResEnergy[i],sizeof(weaponEnergy));weaponEnergyValid=1;}}
    lowResReady[i]=1;
   }
   if(w>0&&((w-1)&(xStep-1))!=0){
    int x=w-1,i=y*w+x;const Surface *s=&surfBase[i];
    if(shift&&s->kind==4)s=&underWeapon[i];
    if(s->kind){
     if(s->kind==4&&weaponEnergyValid)memcpy(lowResEnergy[i],weaponEnergy,sizeof(weaponEnergy));
     else{incidentEnergy(s,flicker,x,lowResEnergy[i]);if(s->kind==4){memcpy(weaponEnergy,lowResEnergy[i],sizeof(weaponEnergy));weaponEnergyValid=1;}}
     lowResReady[i]=1;
    }
   }
   for(int x=0;x<w;x+=xStep){
    int i0=y*w+x,xEnd=x+xStep<w?x+xStep:w;
    if(x+xStep<w&&lowResReady[i0]&&lowResReady[i0+xStep]){
     const Surface *s0=&surfBase[i0],*sMid=&surfBase[i0+(xStep>>1)],*sEnd=&surfBase[i0+xStep];
     if(shift){
      if(s0->kind==4)s0=&underWeapon[i0];
      if(sMid->kind==4)sMid=&underWeapon[i0+(xStep>>1)];
      if(sEnd->kind==4)sEnd=&underWeapon[i0+xStep];
     }
     if(compatibleSpan(s0,sMid,sEnd,xStep)){
      float invStep=1.f/(float)xStep;
      for(int ch=0;ch<3;ch++){
       float e0=lowResEnergy[i0][ch],de=(lowResEnergy[i0+xStep][ch]-e0)*invStep;
       for(int k=1;k<xStep;k++)lowResEnergy[i0+k][ch]=e0+de*k;
      }
      for(int k=1;k<xStep;k++)lowResReady[i0+k]=1;
      continue;
     }
    }
    for(int xx=x+1;xx<xEnd;xx++){
     int i=y*w+xx;if(lowResReady[i])continue;
     const Surface *s=&surfBase[i];
     if(shift&&s->kind==4)s=&underWeapon[i];
     if(!s->kind)continue;
     if(s->kind==4&&weaponEnergyValid)memcpy(lowResEnergy[i],weaponEnergy,sizeof(weaponEnergy));
     else if(lowResReady[i0]&&s->kind==surfBase[i0].kind&&s->nx==surfBase[i0].nx&&s->ny==surfBase[i0].ny&&s->nz==surfBase[i0].nz){
      memcpy(lowResEnergy[i],lowResEnergy[i0],sizeof(lowResEnergy[0]));
     }else if(x+xStep<w&&lowResReady[i0+xStep]&&s->kind==surfBase[i0+xStep].kind&&s->nx==surfBase[i0+xStep].nx&&s->ny==surfBase[i0+xStep].ny&&s->nz==surfBase[i0+xStep].nz){
      memcpy(lowResEnergy[i],lowResEnergy[i0+xStep],sizeof(lowResEnergy[0]));
     }else{
      incidentEnergy(s,flicker,xx,lowResEnergy[i]);if(s->kind==4){memcpy(weaponEnergy,lowResEnergy[i],sizeof(weaponEnergy));weaponEnergyValid=1;}
     }
     lowResReady[i]=1;
    }
   }
  }
  for(int y=1;y<h;y+=2){
   for(int x=0;x<w;x++){
    int i=y*w+x;const Surface *s=&surfBase[i];
    if(shift&&s->kind==4)s=&underWeapon[i];
    if(!s->kind)continue;
    if(y+1<h){
     const Surface *sU=&surfBase[i-w],*sD=&surfBase[i+w];
     if(shift&&sU->kind==4)sU=&underWeapon[i-w];
     if(shift&&sD->kind==4)sD=&underWeapon[i+w];
     if(lowResReady[i-w]&&lowResReady[i+w]&&compatibleSurface(sU,s,sD)){
      for(int ch=0;ch<3;ch++)lowResEnergy[i][ch]=.5f*(lowResEnergy[i-w][ch]+lowResEnergy[i+w][ch]);
      lowResReady[i]=1;continue;
     }
     if(lowResReady[i-w]&&sU->kind==s->kind&&sU->nx==s->nx&&sU->ny==s->ny&&sU->nz==s->nz){
      memcpy(lowResEnergy[i],lowResEnergy[i-w],sizeof(lowResEnergy[0]));
      lowResReady[i]=1;continue;
     }
     if(lowResReady[i+w]&&sD->kind==s->kind&&sD->nx==s->nx&&sD->ny==s->ny&&sD->nz==s->nz){
      memcpy(lowResEnergy[i],lowResEnergy[i+w],sizeof(lowResEnergy[0]));
      lowResReady[i]=1;continue;
     }
    }else if(lowResReady[i-w]&&surfBase[i-w].kind==s->kind){
     memcpy(lowResEnergy[i],lowResEnergy[i-w],sizeof(lowResEnergy[0]));
     lowResReady[i]=1;continue;
    }
    if(x>0&&x+1<w){
     const Surface *sL=&surfBase[i-1],*sR=&surfBase[i+1];
     if(shift&&sL->kind==4)sL=&underWeapon[i-1];
     if(shift&&sR->kind==4)sR=&underWeapon[i+1];
     if(lowResReady[i-1]&&compatibleSurface(sL,s,sR)){
      if(!lowResReady[i+1]){
       if(sR->kind==4&&weaponEnergyValid)memcpy(lowResEnergy[i+1],weaponEnergy,sizeof(weaponEnergy));
       else{incidentEnergy(sR,flicker,x+1,lowResEnergy[i+1]);if(sR->kind==4){memcpy(weaponEnergy,lowResEnergy[i+1],sizeof(weaponEnergy));weaponEnergyValid=1;}}
       lowResReady[i+1]=1;
      }
      for(int ch=0;ch<3;ch++)lowResEnergy[i][ch]=.5f*(lowResEnergy[i-1][ch]+lowResEnergy[i+1][ch]);
      lowResReady[i]=1;continue;
     }
     if(lowResReady[i-1]&&sL->kind==s->kind&&sL->nx==s->nx&&sL->ny==s->ny&&sL->nz==s->nz){
      memcpy(lowResEnergy[i],lowResEnergy[i-1],sizeof(lowResEnergy[0]));
      lowResReady[i]=1;continue;
     }
    }
    if(s->kind==4&&weaponEnergyValid)memcpy(lowResEnergy[i],weaponEnergy,sizeof(weaponEnergy));
    else{incidentEnergy(s,flicker,x,lowResEnergy[i]);if(s->kind==4){memcpy(weaponEnergy,lowResEnergy[i],sizeof(weaponEnergy));weaponEnergyValid=1;}}
    lowResReady[i]=1;
   }
  }
  for(int i=0;i<w*h;i++){
   const Surface *s=&surfBase[i];uint32_t c=original[i];
   if(shift&&s->kind==4){s=&underWeapon[i];c=weaponBackground[i];}
   lit[i]=s->kind?shadePixelWithEnergy(s,c,lowResEnergy[i]):c;
  }
#endif
 }else
#endif
 {
  for(int i=0,x=0,y=0;i<w*h;i++){
   const Surface *s=&surfaces[i];uint32_t c=original[i];
   if(shift&&surfKind(s)==4){s=&underWeapon[i];c=weaponBackground[i];}
   lit[i]=lighting?shadePixel(s,c,flicker,x,y):c;
   if(++x==w){x=0;y++;}
  }
 }
#ifdef OE2_QA_INTERACTIONS
 profile[1]=emscripten_get_now()-shadingStarted;double haloStarted=emscripten_get_now();
#endif
 if(lighting)for(int j=0;j<numLights;j++)glow(&lights[j],flicker[j]);
 if(shift)for(int y=0;y<h-shift;y++)for(int x=0;x<w;x++){
  int i=y*w+x;if(surfKind(&surfaces[i])==4)lit[(y+shift)*w+x]=lighting?shadePixel(&surfaces[i],original[i],flicker,x,y):original[i];
 }
#ifdef OE2_QA_INTERACTIONS
 profile[2]=emscripten_get_now()-haloStarted;
#endif
 traceActive=0;
 return lit;
}
const uint32_t *BrewLight_render(const uint32_t *original,int w,int h){return present(original,w,h,1);}
const uint32_t *BrewLight_classic(const uint32_t *original,int w,int h){return present(original,w,h,0);}
#include "lighting_gpu.inc"
