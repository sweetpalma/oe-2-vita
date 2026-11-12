/* Replay only raster commands at a higher resolution. Gameplay, camera updates,
 * visibility discovery, sprite animation and RNG execute once on the BREW core.
 * Replaying never calls Render_renderScene/Render_drawSprite or game handlers. */
#include "presentation.h"
#include "lighting.h"
#include "render_local.h"
#define MAX_COMMANDS 8192
#define MAX_VIEW_PIXELS (704*880)
typedef struct {int type,shade,effects,projScale,projZ,lineIdx;Line line;int args[8];} Command;
static Command commands[MAX_COMMANDS];
static int count,replaying,quality,valid,nativeLive=1;
static Render *snapshot,*target,*owner;
static uint16_t *pixels;
static uint32_t *rgbaPixels;
static int32_t depth[704];
static IDIB dib;
static void add(Command value){if(!replaying&&valid&&count<MAX_COMMANDS)commands[count++]=value;}
void BrewView_begin(Render *r){
 if(replaying)return;
 if(!snapshot)snapshot=malloc(sizeof(Render));
 if(!snapshot){valid=0;return;}
 memcpy(snapshot,r,sizeof(Render));owner=r;count=0;valid=1;nativeLive=1;
}
void BrewView_line(Render *r,const Line *line){
 if(replaying||r!=owner)return;
 add((Command){.type=1,.shade=r->shade,.effects=(int)r->effectFlags,.projScale=r->projScale,.projZ=r->projZ,.lineIdx=r->curLineIdx,.line=*line});
}
void BrewView_sprite(Render *r,int sprite,int frame,int x,int y,int a,int blend,int scale,int flip){
 if(replaying||r!=owner)return;
 Command c={.type=2,.args={sprite,frame,x,y,a,blend,scale,flip}};add(c);
}
void BrewView_effect(Render *r,int type,int value){
 if(type==3)BrewLight_fade(value);
 if(replaying||r!=owner)return;
 add((Command){.type=type,.args={value}});
}
void BrewView_setQuality(int level){quality=level>=0&&level<=2?level:0;}
static const uint32_t *view(const uint32_t *original,int width,int height,int gpu){
 if(!valid||width!=176||height<1||height>220||(!quality&&nativeLive))return gpu?BrewLight_gpu(original,width,height):BrewLight_render(original,width,height);
 if(!target)target=malloc(sizeof(Render));
 if(!pixels)pixels=malloc(MAX_VIEW_PIXELS*sizeof(uint16_t));
 if(!rgbaPixels)rgbaPixels=malloc(MAX_VIEW_PIXELS*sizeof(uint32_t));
 if(!target||!pixels||!rgbaPixels)return NULL;
 int scale=1<<quality,w=width*scale,h=height*scale;
 nativeLive=0;
 memcpy(target,snapshot,sizeof(Render));
 /* Animation has already been resolved in the recorded commands. Light and
  * caster positions read the same current sprite state as the normal target. */
 memcpy(target->sprites,owner->sprites,sizeof(target->sprites));
 memcpy(target->lines,owner->lines,sizeof(target->lines));
 for(int i=0;i<1024;i++){
  uintptr_t base=(uintptr_t)owner->flats,fp=(uintptr_t)snapshot->floorTiles[i],cp=(uintptr_t)snapshot->ceilTiles[i];
  if(fp>=base&&fp<base+sizeof(snapshot->flats))target->floorTiles[i]=(uint16_t *)((uint8_t *)target->flats+(fp-base));
  if(cp>=base&&cp<base+sizeof(snapshot->flats))target->ceilTiles[i]=(uint16_t *)((uint8_t *)target->flats+(cp-base));
 }
 target->width=w;target->height=h;target->centerX=w/2;target->centerY=h/2;
 target->projX=((w/2)<<16)-0x8000;target->projW=(w<<16)-0x8000;target->projH=w<<16;
 target->fbOffsetX=target->fbOffsetY=0;target->clipX1=target->clipY1=0;target->clipX2=w;target->clipY2=h;
 target->columnDepth=depth;target->framebuffer=pixels;
 dib=*snapshot->fbDib;dib.pBmp=(uint8_t *)pixels;dib.cx=(uint16_t)w;dib.cy=(uint16_t)h;dib.nPitch=(int16_t)(w*2);target->fbDib=&dib;
 memset(pixels,0,(size_t)w*h*sizeof(uint16_t));
 void *drawers[13]={target->spanNormal,target->spanBlend75,target->spanBlend50,target->spanBlend25,target->spanAdditive,target->span18,target->span1c,target->span20,target->spanGray,target->span28,target->span2c,target->spanMultiply,target->spanNone};
 Render *old[13];for(int i=0;i<13;i++){old[i]=drawers[i]?((SpanDrawer *)drawers[i])->render:NULL;if(drawers[i])((SpanDrawer *)drawers[i])->render=target;}
 replaying=1;BrewLight_begin(target);Render_clearColumnDepth(target);
 if(!target->dbgSkipFlats)Render_drawFloorCeiling(target);
 for(int i=0;i<count;i++){
  Command *c=&commands[i];
  if(c->type==1){
   Line line=c->line;target->shade=c->shade;target->effectFlags=(uint32_t)c->effects;target->projScale=c->projScale;target->projZ=c->projZ;target->curLineIdx=c->lineIdx;
   Render_drawLineSeg(target,&line);
  }else if(c->type==2){
   int *a=c->args;Render_draw2DSprite(target,a[0],a[1],a[2]*scale,a[3]*scale,a[4],a[5],a[6],a[7]);
  }else if(c->type==3)Render_fadeFramebuffer(target,c->args[0]);
  else if(c->type==4)Render_tintScreen(target);
 }
 replaying=0;for(int i=0;i<13;i++)if(drawers[i])((SpanDrawer *)drawers[i])->render=old[i];
 for(int i=0;i<w*h;i++){uint16_t c=pixels[i];rgbaPixels[i]=(((c>>11)&31)*255/31)|((((c>>5)&63)*255/63)<<8)|(((c&31)*255/31)<<16)|0xff000000u;}
 return gpu?BrewLight_gpu(rgbaPixels,w,h):BrewLight_render(rgbaPixels,w,h);
}
const uint32_t *BrewView_lit(const uint32_t *original,int width,int height){return view(original,width,height,0);}
const uint32_t *BrewView_gpu(const uint32_t *original,int width,int height){return view(original,width,height,1);}

#ifdef OE2_QA_INTERACTIONS
int BrewView_qaSpriteCommand(int index,int field){
 for(int i=0;i<count;i++)if(commands[i].type==2){if(index--==0)return field>=0&&field<8?commands[i].args[field]:-1;}
 return -1;
}
#endif
