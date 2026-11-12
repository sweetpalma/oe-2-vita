/* Deferred presentation. The CPU only prepares geometry/contact shade and
 * the existing three-ray visibility field. Per-pixel point-light arithmetic,
 * colour modulation, tone mapping, haze and halos execute in the GPU shader.
 * No readback, offline lighting variants or lower-resolution colour buffer. */
static uint32_t *gpuColour,*gpuXY,*gpuZNA;
static size_t gpuCapacity;
static uint32_t gpuShadows[GRID_CELLS];
static uint32_t gpuContact[3*129*129];
static unsigned char gpuVisited[GRID_CELLS];
static unsigned gpuShadowRevision;
static unsigned gpuContactRevision;
static int gpuInitialized;
static float gpuParams[112];
static uint16_t gpuNormalCodes[256*256];
static float gpuNormals[248][2];
static unsigned char gpuNormalsReady[248];
typedef struct {int valid,normal,a,b,c;} GpuReceiverSpan;
static GpuReceiverSpan gpuWallSpans[704],gpuFloorSpans[880];
static GpuReceiverSpan gpuContactColumns[704],gpuContactRows[880];
typedef struct {int valid,mask;float x,y;} GpuNearColumn;
static GpuNearColumn gpuNearColumns[704];
static unsigned gpuCoordinate(float value){return (unsigned)clampf(floorf((value+1024)*16+.5f),0,65535);}
static unsigned gpuRelative(float value){return (unsigned)clampf(floorf((value+2)*16384+.5f),0,65535);}
static unsigned gpuNormal(const Surface *s){
 int kind=surfKind(s),snz=surfNz(s);
 if(!kind)return 250;
 if(kind==4)return 251;
 if(snz)return snz>0?248:249;
 unsigned key=(unsigned char)s->nx|((unsigned char)s->ny<<8);
 if(!gpuNormalCodes[key]){
  int angle=(int)floorf(atan2f(s->ny,s->nx)*(248.f/6.28318530718f)+.5f);
  gpuNormalCodes[key]=(uint16_t)((angle+248)%248+1);
 }
 return gpuNormalCodes[key]-1;
}
static void gpuNormalVector(unsigned code,float *nx,float *ny,float *nz){
 *nx=*ny=*nz=0;
 if(code>=248){if(code==248)*nz=1;else if(code==249)*nz=-1;else if(code==251){*nx=-fx;*ny=-fy;}return;}
 if(code%62==0){if(code==0)*nx=1;else if(code==62)*ny=1;else if(code==124)*nx=-1;else *ny=-1;return;}
 if(code%62==31){*nx=(code==31||code==217)?.70710678118f:-.70710678118f;*ny=code<124?.70710678118f:-.70710678118f;return;}
 if(!gpuNormalsReady[code]){float a=code*(6.28318530718f/248);gpuNormals[code][0]=cosf(a);gpuNormals[code][1]=sinf(a);gpuNormalsReady[code]=1;}
 *nx=gpuNormals[code][0];*ny=gpuNormals[code][1];
}
static void gpuMarkCell(int x,int y,int z){
 if(x<0||x>=128||y<0||y>=128||z<0||z>=24)return;
 int cell=(z*128+y)*128+x;if(gpuVisited[cell])return;gpuVisited[cell]=1;
 uint32_t value=0xffffffffu;
 for(int j=0;j<numLights;j++){
  float dx=x*16+8.f-lights[j].x,dy=y*16+8.f-lights[j].y,dz=z*8+4.f-lights[j].z,reach=lights[j].radius+32;
  /* An unused corner outside every possible receiver's source influence
   * needs no ray. The 32-unit guard includes interpolation and normal bias. */
  if(dx*dx+dy*dy+dz*dz>reach*reach)continue;
  visibilityAt(x,y,z,j,1);
  unsigned shift=j*2,mask=3u<<shift;
  value=(value&~mask)|((uint32_t)visibility[cell]&mask);
  value=(value&~(mask<<16))|(((uint32_t)wallVisibility[cell]&mask)<<16);
 }
 if(gpuShadows[cell]!=value){gpuShadows[cell]=value;gpuShadowRevision++;}
}
static int gpuSameSpan(GpuReceiverSpan *s,int normal,int a,int b,int c){
 if(s->valid&&s->normal==normal&&s->a==a&&s->b==b&&s->c==c)return 1;
 *s=(GpuReceiverSpan){1,normal,a,b,c};return 0;
}
static void gpuContactCell(int x,int y,int layer){
 if(x<0||x>128||y<0||y>128)return;
 int cell=y*129+x,at=layer*129*129+cell;
 wallDistanceAt(x,y,0,layer);uint32_t value=0;
 float d[3]={wallAxisDistance[layer][0][cell],wallAxisDistance[layer][1][cell],wallDistance[layer][cell]};
 for(int i=0;i<3;i++)value|=(uint32_t)floorf(d[i]*(511.f/40)+.5f)<<(i*9);
 if(gpuContact[at]!=value){gpuContact[at]=value;gpuContactRevision++;}
}
static void gpuMarkContact(float x,float y,int normal,int sprite,int column,int row){
 if(normal>=250||sprite)return;
 int ix=(int)floorf(x/16),iy=(int)floorf(y/16),layer=normal>=248?(normal==248?0:1):2;
 GpuReceiverSpan *span=normal>=248?&gpuContactRows[row]:&gpuContactColumns[column];
 if(gpuSameSpan(span,layer,ix,iy,0))return;
 gpuContactCell(ix,iy,layer);gpuContactCell(ix+1,iy,layer);gpuContactCell(ix,iy+1,layer);gpuContactCell(ix+1,iy+1,layer);
}
static void gpuMarkReceiver(uint32_t xy,uint32_t zna,int column,int row){
 unsigned normal=(zna>>16)&255;if(normal>=250)return;
 float x=(xy&65535)/16.f-1024,y=(xy>>16)/16.f-1024,z=(zna&65535)/16.f-1024;
 if(zna&0x20000000u){int j=(zna>>26)&7;x=lights[j].x+(xy&65535)/16384.f-2;y=lights[j].y+(xy>>16)/16384.f-2;z=lights[j].z+(zna&65535)/16384.f-2;}
 gpuMarkContact(x,y,normal,(zna&0x80000000u)!=0,column,row);
 float dx=x-processingX,dy=y-processingY,reach=processingSteps*WALK_STEP;
 if(dx*dx+dy*dy>=reach*reach)return;
 float nx,ny,nz;gpuNormalVector(normal,&nx,&ny,&nz);
 x=(x+nx*2)/16;y=(y+ny*2)/16;z=(z+nz*2)/8;
 int ix=(int)floorf(x),iy=(int)floorf(y),iz=(int)floorf(z);
 if(normal>=248){
  int u=(int)floorf(x-.5f),v=(int)floorf(y-.5f);
  if(gpuSameSpan(&gpuFloorSpans[row],normal,u,v,iz))return;
  gpuMarkCell(u,v,iz);gpuMarkCell(u+1,v,iz);gpuMarkCell(u,v+1,iz);gpuMarkCell(u+1,v+1,iz);
 }else{
  int axis=(zna&0x40000000u)!=0,u=(int)floorf((axis?y:x)-.5f),v=(int)floorf(z-.5f),delta=(int)((zna>>24)&3)-1;
  if(axis)ix+=delta;else iy+=delta;
  if(gpuSameSpan(&gpuWallSpans[column],normal+axis*256,axis?ix:iy,u,v))return;
  if(axis){gpuMarkCell(ix,u,v);gpuMarkCell(ix,u+1,v);gpuMarkCell(ix,u,v+1);gpuMarkCell(ix,u+1,v+1);}
  else{gpuMarkCell(u,iy,v);gpuMarkCell(u+1,iy,v);gpuMarkCell(u,iy,v+1);gpuMarkCell(u+1,iy,v+1);}
 }
}
static void gpuPack(int index,const Surface *s,uint32_t colour,int column){
 unsigned normal=gpuNormal(s),flags=surfKind(s)==3?128:0;
 float sx=surfX(s),sy=surfY(s),sz=surfZ(s);
 unsigned z=gpuCoordinate(sz);
 gpuColour[index]=colour;
 gpuXY[index]=gpuCoordinate(sx)|(gpuCoordinate(sy)<<16);
 if(normal<248){
  /* Preserve precision at a point emitter's singular normal response without
   * float textures. Almost all receivers use absolute 1/16-unit coordinates;
   * a tiny <2-unit neighborhood uses 1/16384-unit source-relative coordinates. */
  GpuNearColumn *near=&gpuNearColumns[column];
  if(!near->valid||near->x!=sx||near->y!=sy){
   near->valid=1;near->x=sx;near->y=sy;near->mask=0;
   for(int j=0;j<numLights;j++){float dx=sx-lights[j].x,dy=sy-lights[j].y;if(dx*dx+dy*dy<4)near->mask|=1<<j;}
  }
  if(near->mask)for(int j=0;j<numLights;j++)if(near->mask&(1<<j)){
   float dx=sx-lights[j].x,dy=sy-lights[j].y,dz=sz-lights[j].z;if(dx*dx+dy*dy+dz*dz>=4)continue;
   gpuXY[index]=gpuRelative(dx)|(gpuRelative(dy)<<16);z=gpuRelative(dz);flags|=32|(j<<2);break;
  }
  int axis=abs(s->nx)>=abs(s->ny);float nx,ny,nz;gpuNormalVector(normal,&nx,&ny,&nz);
  float coordinate=axis?sx:sy,bias=(axis?s->nx:s->ny)/127.f*2;
  float decoded=(axis?(gpuXY[index]&65535):(gpuXY[index]>>16))/16.f-1024;
  if(flags&32){int j=(flags>>2)&7;decoded=(axis?lights[j].x:lights[j].y)+(axis?(gpuXY[index]&65535):(gpuXY[index]>>16))/16384.f-2;}
  int originalCell=(int)floorf((coordinate+bias)/16),decodedCell=(int)floorf((decoded+(axis?nx:ny)*2)/16);
  /* Keep the exact receiver plane at 16-unit boundaries. Rounding geometry
   * must not select the shadow cell on the other side of a wall. */
  int delta=originalCell-decodedCell;delta=delta< -1?-1:delta>1?1:delta;
  flags|=(axis?64:0)|(unsigned)(delta+1);
 }
 gpuZNA[index]=z|(normal<<16)|(flags<<24);
#ifdef OE2_QA_INTERACTIONS
 if(costMode==4)return;
#endif
 gpuMarkReceiver(gpuXY[index],gpuZNA[index],column,index/width);
}
const uint32_t *BrewLight_gpu(const uint32_t *original,int w,int h){
 if(!renderer||w!=width||h!=height)return NULL;
 size_t pixels=(size_t)w*h;
 if(pixels>gpuCapacity){
  uint32_t *c=malloc(pixels*4),*xy=malloc(pixels*4),*zna=malloc(pixels*4);
  if(!c||!xy||!zna){free(c);free(xy);free(zna);return NULL;}
  free(gpuColour);free(gpuXY);free(gpuZNA);gpuColour=c;gpuXY=xy;gpuZNA=zna;gpuCapacity=pixels;
 }
 if(!gpuInitialized){memset(gpuShadows,0xff,sizeof(gpuShadows));gpuInitialized=1;gpuShadowRevision++;}
#ifdef OE2_QA_INTERACTIONS
 double started=emscripten_get_now();
#endif
 int shift=staffShift();memset(cornerCache,0,sizeof(cornerCache));memset(gpuVisited,0,sizeof(gpuVisited));
 memset(gpuWallSpans,0,sizeof(gpuWallSpans));memset(gpuFloorSpans,0,sizeof(gpuFloorSpans));
 memset(gpuContactColumns,0,sizeof(gpuContactColumns));memset(gpuContactRows,0,sizeof(gpuContactRows));
 memset(gpuNearColumns,0,sizeof(gpuNearColumns));
 gather(renderer);traceActive=1;
#ifdef OE2_QA_INTERACTIONS
 profile[0]=emscripten_get_now()-started;double packed=emscripten_get_now();
 profile[3]=numLights;profile[4]=0;for(int j=0;j<numLights;j++)profile[4]+=casterCount[j];
#endif
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  int i=y*w+x;const Surface *s=&surfaces[i];uint32_t colour=original[i];
  if(shift&&surfKind(s)==4){s=&underWeapon[i];colour=weaponBackground[i];}
  gpuPack(i,s,colour,x);
 }
 if(shift)for(int y=0;y<h-shift;y++)for(int x=0;x<w;x++){
  int i=y*w+x;if(surfKind(&surfaces[i])==4)gpuPack((y+shift)*w+x,&surfaces[i],original[i],x);
 }
 memset(gpuParams,0,sizeof(gpuParams));
 gpuParams[0]=numLights;gpuParams[1]=w;gpuParams[2]=h;gpuParams[3]=fadeLevel/255.f;
 gpuParams[4]=renderer->viewX;gpuParams[5]=renderer->viewY;gpuParams[6]=renderer->viewZ;gpuParams[7]=fx;
 gpuParams[8]=fy;gpuParams[9]=processingX;gpuParams[10]=processingY;gpuParams[11]=processingSteps*WALK_STEP;
 gpuParams[12]=renderer->effFloor;gpuParams[13]=renderer->effCeil;
 for(int j=0;j<numLights;j++){
  Light *l=&lights[j];float *p=&gpuParams[16+j*12];int phase=l->key%31;
  float flicker=l->flicker?(.92f+.06f*sinf(g_app->time*.021f+phase*2)+.04f*sinf(g_app->time*.047f+phase)):1;
  p[0]=l->x;p[1]=l->y;p[2]=l->z;p[3]=l->radius;p[4]=l->r;p[5]=l->g;p[6]=l->b;p[7]=l->power*flicker*2.1f;
  float dx=l->x-renderer->viewX,dy=l->y-renderer->viewY,depth=dx*fx+dy*fy,weight=worldLightWeight(l->x,l->y);
  if(l->key==2000||depth<12||weight<=0||!BrewLight_visible(renderer,renderer->viewX,renderer->viewY,renderer->viewZ,l->x,l->y,l->z))continue;
  p[8]=w*.5f+(dx*rx+dy*ry)*w/(2*depth);p[9]=h*.5f-(l->z-renderer->viewZ)*w/(2*depth);
  p[10]=clampf(w*5.5f/depth,1.5f*w/176.f,12.f*w/176.f);p[11]=.008f*l->power*flicker*weight*gpuParams[3]*gpuParams[3];
 }
#ifdef OE2_QA_INTERACTIONS
 profile[1]=emscripten_get_now()-packed;profile[2]=0;
#endif
 traceActive=0;return gpuColour;
}
const uint32_t *BrewLight_gpuXY(void){return gpuXY;}
const uint32_t *BrewLight_gpuZNA(void){return gpuZNA;}
const uint32_t *BrewLight_gpuShadows(void){return gpuShadows;}
const float *BrewLight_gpuParams(void){return gpuParams;}
unsigned BrewLight_gpuShadowRevision(void){return gpuShadowRevision;}
const uint32_t *BrewLight_gpuContact(void){return gpuContact;}
unsigned BrewLight_gpuContactRevision(void){return gpuContactRevision;}

#ifdef OE2_QA_INTERACTIONS
/* Independent CPU execution of the byte-texture contract for pixel comparisons
 * with both the existing renderer and a real WebGL/Godot shader. Never shipped. */
static float gpuTestVisibility(int x,int y,int z,int light,int sprites){
 if(x<0||x>=128||y<0||y>=128||z<0||z>=24)return 1;
 unsigned value=gpuShadows[(z*128+y)*128+x]>>(sprites?0:16);
 return ((value>>(light*2))&3)/3.f;
}
static float gpuTestShadow(float x,float y,float z,float nx,float ny,float nz,int light,int sprites,int axis,int delta){
 x=(x+nx*2)/16;y=(y+ny*2)/16;z=(z+nz*2)/8;
 int ix=(int)floorf(x),iy=(int)floorf(y),iz=(int)floorf(z),u,v;float a,b;
 if(nz){a=x-.5f;b=y-.5f;u=(int)floorf(a);v=(int)floorf(b);
  return mixf(mixf(gpuTestVisibility(u,v,iz,light,sprites),gpuTestVisibility(u+1,v,iz,light,sprites),a-u),mixf(gpuTestVisibility(u,v+1,iz,light,sprites),gpuTestVisibility(u+1,v+1,iz,light,sprites),a-u),b-v);}
 if(axis)ix+=delta;else iy+=delta;
 a=(axis?y:x)-.5f;b=z-.5f;u=(int)floorf(a);v=(int)floorf(b);
 if(axis)return mixf(mixf(gpuTestVisibility(ix,u,v,light,sprites),gpuTestVisibility(ix,u+1,v,light,sprites),a-u),mixf(gpuTestVisibility(ix,u,v+1,light,sprites),gpuTestVisibility(ix,u+1,v+1,light,sprites),a-u),b-v);
 return mixf(mixf(gpuTestVisibility(u,iy,v,light,sprites),gpuTestVisibility(u+1,iy,v,light,sprites),a-u),mixf(gpuTestVisibility(u,iy,v+1,light,sprites),gpuTestVisibility(u+1,iy,v+1,light,sprites),a-u),b-v);
}
static float gpuTestDistance(int x,int y,int layer,int axis){
 if(x<0||x>128||y<0||y>128)return 40;
 unsigned value=gpuContact[(layer*129+y)*129+x];return ((value>>(axis*9))&511)*(40.f/511);
}
static float gpuTestContact(float x,float y,float z,unsigned code,int sprite,int axisX){
 if(code==251)return 1;
 float distance=fminf(fabsf(z-gpuParams[12]),fabsf(gpuParams[13]-z));
 if(code>=248||!sprite){
  int layer=code>=248?(code==248?0:1):2,axis=code>=248?2:(axisX?1:0);
  x/=16;y/=16;int ix=(int)floorf(x),iy=(int)floorf(y);
  float room=mixf(mixf(gpuTestDistance(ix,iy,layer,axis),gpuTestDistance(ix+1,iy,layer,axis),x-ix),mixf(gpuTestDistance(ix,iy+1,layer,axis),gpuTestDistance(ix+1,iy+1,layer,axis),x-ix),y-iy);
  distance=code>=248?room:fminf(room,distance);
 }
 float t=clampf(distance/20,0,1);return .35f+.65f*t*t*(3-2*t);
}
const uint32_t *BrewLight_gpuTestFrame(void){
 for(int i=0;i<width*height;i++){
  uint32_t xy=gpuXY[i],zna=gpuZNA[i],colour=gpuColour[i],out=0xff000000u;unsigned code=(zna>>16)&255;
  float x=(xy&65535)/16.f-1024,y=(xy>>16)/16.f-1024,z=(zna&65535)/16.f-1024,nx,ny,nz;
  if(zna&0x20000000u){int j=(zna>>26)&7;x=lights[j].x+(xy&65535)/16384.f-2;y=lights[j].y+(xy>>16)/16384.f-2;z=lights[j].z+(zna&65535)/16384.f-2;}
  gpuNormalVector(code,&nx,&ny,&nz);float depth=(x-gpuParams[4])*fx+(y-gpuParams[5])*fy;
  float result[3];for(int ch=0;ch<3;ch++)result[ch]=((colour>>(ch*8))&255)/255.f;
  if(code!=250){
   int axis=(zna&0x40000000u)!=0,delta=(int)((zna>>24)&3)-1;
   float ao=gpuTestContact(x,y,z,code,(zna&0x80000000u)!=0,axis),energy[3]={.17f*ao,.20f*ao,.27f*ao};
   float weight=code==251?1:worldLightWeight(x,y);
   for(int j=0;j<numLights&&weight>0;j++){
    const float *p=&gpuParams[16+j*12];float dx=p[0]-x,dy=p[1]-y,dz=p[2]-z,d2=dx*dx+dy*dy+dz*dz;if(d2>=p[3]*p[3])continue;
    float d=sqrtf(d2)+.01f,lambert=clampf((nx*dx+ny*dy+nz*dz)/d,0,1),attenuation=1-d/p[3];
    float e=weight*attenuation*attenuation*p[7]*(.18f+.82f*lambert);
    if(code!=251)e*=gpuTestShadow(x,y,z,nx,ny,nz,j,!(zna&0x80000000u),axis,delta)*(.75f+.25f*ao);
    for(int ch=0;ch<3;ch++)energy[ch]+=p[4+ch]*e;
   }
   float haze=code==251?0:.12f*clampf((depth-128)/384,0,1)*gpuParams[3],mist[3]={.012f,.022f,.035f};
   for(int ch=0;ch<3;ch++){
    float linear=result[ch]*result[ch]*energy[ch];linear=mixf(linear/(1+fmaxf(0,linear-1)*.7f),mist[ch],haze);
    result[ch]=floorf(clampf(sqrtf(linear),0,1)*255+.5f)/255.f;
   }
  }
  for(int j=0;j<numLights;j++){
   const float *p=&gpuParams[16+j*12];if(p[11]<=0||code==251)continue;
   float sourceDepth=(p[0]-gpuParams[4])*fx+(p[1]-gpuParams[5])*fy;if(code!=250&&depth+12<sourceDepth)continue;
   float ax=(i%width-p[8])/p[10],ay=(i/width-p[9])/p[10],r2=ax*ax+ay*ay;if(r2>1)continue;
   for(int ch=0;ch<3;ch++)result[ch]=floorf(sqrtf(fminf(1,result[ch]*result[ch]+p[4+ch]*p[11]*expf(-r2*4)))*255+.5f)/255.f;
  }
  for(int ch=0;ch<3;ch++)out|=(uint32_t)(result[ch]*255+.5f)<<(ch*8);lit[i]=out;
 }
 return lit;
}
#endif
