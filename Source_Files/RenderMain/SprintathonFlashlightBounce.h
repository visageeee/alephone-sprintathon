#ifndef SPRINTATHON_FLASHLIGHT_BOUNCE_H
#define SPRINTATHON_FLASHLIGHT_BOUNCE_H
#include "SprintathonFlashlightShadow.h"
#include <string>
#include <cstdlib>
#include <algorithm>
// Experimental, opt-in screen-space single-bounce lighting. No CPU readbacks.
namespace SprintathonFlashlightBounce {
// Read saved settings each frame so changes take effect without a restart.
inline float gain(){return std::max(0.0f,std::min(4.0f,graphics_preferences->flashlight_bounce_intensity/100.0f));}
inline float saturation(){return std::max(0.0f,std::min(1.0f,graphics_preferences->flashlight_bounce_saturation/100.0f));}
inline int debugMode(){const char* p=std::getenv("SPRINTATHON_FLASHLIGHT_BOUNCE_DEBUG");return p?std::atoi(p):0;}
inline bool enabled(){return graphics_preferences->flashlight_bounce && gain()>0.0f && SprintathonFlashlight::frame().params[0]>0;}
static const char* vertex=R"BOUNCE(#version 120
void main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}
)BOUNCE";
static const char* common=R"BOUNCE(
uniform sampler2DRect sceneDepth,sceneColor;
uniform vec2 imageSize,depthProjection;
uniform vec4 viewProjection;
vec2 pixelAt(vec3 p){return ((p.xy/max(-p.z,1.0))*viewProjection.xy-viewProjection.zw)*imageSize*0.5+imageSize*0.5;}
float zAt(vec2 p){float d=texture2DRect(sceneDepth,p).r;return abs(depthProjection.y/(2.0*d-1.0+depthProjection.x));}
vec3 positionAt(vec2 p){float z=zAt(p);return vec3((p/imageSize*2.0-1.0+viewProjection.zw)/viewProjection.xy*z,-z);}
bool inside(vec2 p){return p.x>2.0 && p.y>2.0 && p.x<imageSize.x-2.0 && p.y<imageSize.y-2.0;}
vec3 normalAt(vec2 p){
 vec3 c=positionAt(p),l=c-positionAt(p-vec2(1,0)),r=positionAt(p+vec2(1,0))-c;
 vec3 d=c-positionAt(p-vec2(0,1)),u=positionAt(p+vec2(0,1))-c;
 vec3 n=cross(dot(l,l)<dot(r,r)?l:r,dot(d,d)<dot(u,u)?d:u);
 return n/max(length(n),0.00001);
}
)BOUNCE";
static const char* sourceBody=R"BOUNCE(
uniform float bounceSaturation;
void main(){
 vec3 lamp=vec3(flashlightProjection.xy,flashlightProjection.w);
 vec3 axis=normalize(vec3(flashlightDepth.zw,-1.0));
 vec3 right=normalize(cross(axis,vec3(0,1,0))),up=cross(right,axis);
 int index=int(floor(gl_FragCoord.y));
 vec2 offset=index==1?vec2(1,0):index==2?vec2(-1,0):
             index==3?vec2(0,1):index==4?vec2(0,-1):vec2(0);
 // Centre plus four rays halfway out through the beam footprint.
 vec3 direction=normalize(axis+(right*offset.x+up*offset.y)*flashlightParams.y*0.55);
 float last=8.0,hit=-1.0;
 // Increasing step lengths cover the flashlight range; refine the first
 // crossing. Reject offscreen paths rather than inventing hidden surfaces.
 for(int i=0;i<64;++i){
  float t=8.0+float(i+1)*float(i+1)*(flashlightParams.z-8.0)/4096.0;
  vec3 p=lamp+direction*t;vec2 uv=pixelAt(p);
  if(p.z>=-8.0 || !inside(uv)){last=t;continue;}
  float z=zAt(uv);
  if(-p.z>=z){
   float low=last,high=t;
   for(int j=0;j<8;++j){float mid=(low+high)*0.5;vec3 q=lamp+direction*mid;
    if(-q.z>=zAt(pixelAt(q)))high=mid;else low=mid;}
   hit=high;break;
  }
  last=t;
 }
 if(hit<0.0){gl_FragColor=vec4(0);return;}
 vec3 centre=lamp+direction*hit;vec2 uv=pixelAt(centre);
 if(!inside(uv) || abs(zAt(uv)+centre.z)>32.0 || texture2DRect(sceneDepth,uv).r>=0.99999){gl_FragColor=vec4(0);return;}
 vec3 n=normalAt(uv);if(dot(n,-direction)<0.15){gl_FragColor=vec4(0);return;}
 vec3 colour=texture2DRect(sceneColor,uv).rgb;float weight=1.0;
 // Spatially smooth colour, keeping samples on the same depth surface.
 for(int i=0;i<4;++i){vec2 offset=i==0?vec2(3,0):i==1?vec2(-3,0):i==2?vec2(0,3):vec2(0,-3);
  vec2 q=uv+offset;vec3 p=positionAt(q);
  if(abs(dot(n,p-centre))<16.0){colour+=texture2DRect(sceneColor,q).rgb;weight+=1.0;}}
 colour/=weight;
 // This is shaded scene colour, not recovered albedo. Clamp the direct beam
 // contribution so brighter flashlight settings cannot amplify it without bound.
 // Test just above the surface to avoid the receiver shadowing itself.
 vec3 light=sprintathonFlashlightFog(centre+n*16.0);
 float energy=clamp(max(light.r,max(light.g,light.b)),0.0,1.0);
 float border=smoothstep(2.0,24.0,min(min(uv.x,uv.y),min(imageSize.x-uv.x,imageSize.y-uv.y)));
 if(gl_FragCoord.x<1.0)gl_FragColor=vec4(centre+n*32.0,border);
 else if(gl_FragCoord.x<2.0)gl_FragColor=vec4(n,1.0);
 else {
  colour=clamp(colour,0.0,1.0);
  float luminance=dot(colour,vec3(0.2126,0.7152,0.0722));
  // Artistic exposure compensation for shaded (rather than albedo) input.
  // Zero remains zero. Tint is mixed at constant luminance, independently of gain.
  vec3 tint=mix(vec3(luminance),colour,bounceSaturation);
  vec3 radiance=tint*(sqrt(luminance)/max(luminance,0.00001));
  gl_FragColor=vec4(radiance*energy*0.9,1.0);
 }
}
)BOUNCE";
static const char* bounceBody=R"BOUNCE(
uniform sampler2D bounceData;
uniform float debugForce;
uniform float bounceGain;
void main(){
 if(debugForce>0.5){vec2 uv=gl_TexCoord[0].xy*imageSize;
  gl_FragColor=texture2DRect(sceneDepth,uv).r<0.99999?vec4(0.7,0.5,0.3,zAt(uv)):vec4(0.0);return;}
 vec2 uv=gl_TexCoord[0].xy*imageSize;
 if(texture2DRect(sceneDepth,uv).r>=0.99999){gl_FragColor=vec4(0);return;}
 vec3 p=positionAt(uv),receiverNormal=normalAt(uv),total=vec3(0);
 for(int sampleIndex=0;sampleIndex<5;++sampleIndex){
  float row=(float(sampleIndex)+0.5)/5.0;
  vec4 source=texture2D(bounceData,vec2(1.0/6.0,row));
  if(source.w<=0.0)continue;
  vec3 colour=texture2D(bounceData,vec2(5.0/6.0,row)).rgb;
  if(max(colour.r,max(colour.g,colour.b))<=0.0)continue;
  vec3 delta=p-source.xyz;float distance=length(delta);
  if(distance<32.0 || distance>=3072.0)continue;
  vec3 direction=delta/distance;
  vec3 n=texture2D(bounceData,vec2(0.5,row)).xyz;
  float facing=max(dot(n,direction),0.0)*max(dot(receiverNormal,-direction),0.0);
  if(facing<=0.0)continue;
  // Retain per-source visibility checks; never brighten a rejected ray.
  bool visible=true;
  for(int i=1;i<13;++i){
   vec3 q=mix(source.xyz,p,float(i)/13.0);vec2 pixel=pixelAt(q);
   if(!inside(pixel) || -q.z>zAt(pixel)+24.0){visible=false;break;}
  }
  if(!visible)continue;
  float fade=1.0-distance/3072.0;fade*=fade;
  // Fixed weights avoid amplifying the remaining sources as others disappear.
  float weight=sampleIndex==0?0.4:0.15;
  total+=colour*(sqrt(facing)*fade*source.w*weight);
 }
 // Store incident light and depth; material shaders retain texture detail.
 gl_FragColor=vec4(total*bounceGain,-p.z);
}
)BOUNCE";
struct State{
 SDL_GLContext context=nullptr;GLuint texture=0,fbo=0,illumination=0;
 GLhandleARB source=0,bounce=0,debug=0;bool tried=false,valid=false,applying=false;
 int width=0,height=0,level=-1;long tick=-1;Uint32 stamp=0;
 GLfloat historyView[16]={},historyProjection[4]={};float aim[2]={};
};
static const char* debugBody=R"BOUNCE(
uniform sampler2D bounceData;
uniform sampler2DRect lightHistory;
uniform vec2 historySize;
uniform float historyApplied;
void main(){
 vec2 uv=gl_TexCoord[0].xy;vec2 pixel=uv*historySize;
 float sourceValid=0.0;vec3 energy=vec3(0);
 for(int i=0;i<5;++i){
  float row=(float(i)+0.5)/5.0;
  sourceValid=max(sourceValid,texture2D(bounceData,vec2(1.0/6.0,row)).w);
  energy+=texture2D(bounceData,vec2(5.0/6.0,row)).rgb;
 }
 // Three diagnostic squares: source hit, source energy, history application.
 if(pixel.y>historySize.y-48.0 && pixel.x<144.0){
  bool ok=pixel.x<48.0?sourceValid>0.0:(pixel.x<96.0?max(energy.r,max(energy.g,energy.b))>0.00001:historyApplied>0.5);
  gl_FragColor=vec4(ok?vec3(0,1,0):vec3(1,0,0),0.9);return;
 }
 vec3 light=texture2DRect(lightHistory,pixel).rgb;
 float value=max(light.r,max(light.g,light.b));
 if(value<=0.00001)discard;
 gl_FragColor=vec4(clamp(light*12.0,0.0,1.0),0.85);
}
)BOUNCE";
inline State& state(){static State s;return s;}
// Called before world rendering, before the current frame overwrites history.
inline void beginFrame(const view_data* camera,const GLfloat* modelview,bool allowed){
 auto& s=state();const auto& f=SprintathonFlashlight::frame();
 s.applying=allowed && enabled() && s.valid && s.context==SDL_GL_GetCurrentContext() &&
    s.level==dynamic_world->current_level_number && dynamic_world->tick_count>=s.tick &&
    Uint32(SDL_GetTicks()-s.stamp)<100 && !camera->under_media_boundary;
 if(s.applying){
    float travel=0,alignment=0;
    for(int i=0;i<3;++i){float d=modelview[12+i]-s.historyView[12+i];travel+=d*d;
        alignment+=modelview[i*4+2]*s.historyView[i*4+2];}
    if(travel>1024.0f*1024.0f || alignment<0.995f ||
       std::fabs(f.depth[2]-s.aim[0])+std::fabs(f.depth[3]-s.aim[1])>0.05f)s.applying=false;
 }
 if(!allowed || !enabled() || camera->under_media_boundary)s.valid=false;
}
inline void bindSurface(GLhandleARB shader){
 auto& s=state();auto loc=[&](const char* n){return glGetUniformLocationARB(shader,n);};
 // Keep sampler types on separate units even when the effect is disabled.
 glUniform1iARB(loc("flashlightBounceMap"),10);
 glUniform1fARB(loc("flashlightBounceEnabled"),s.applying?1.0f:0.0f);
 if(!s.applying)return;
 GLint active;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
 glActiveTextureARB(GL_TEXTURE0_ARB+10);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,s.illumination);glActiveTextureARB(active);
 glUniformMatrix4fvARB(loc("flashlightBounceView"),1,GL_FALSE,s.historyView);
 glUniform4fvARB(loc("flashlightBounceProjection"),1,s.historyProjection);
 glUniform2fARB(loc("flashlightBounceSize"),s.width,s.height);
}
inline GLhandleARB link(const std::string& fragment){
 auto v=SprintathonFlashlightShadow::compile(GL_VERTEX_SHADER_ARB,vertex);
 auto f=SprintathonFlashlightShadow::compile(GL_FRAGMENT_SHADER_ARB,fragment.c_str());
 if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return 0;}
 auto p=glCreateProgramObjectARB();glAttachObjectARB(p,v);glAttachObjectARB(p,f);glLinkProgramARB(p);
 glDeleteObjectARB(v);glDeleteObjectARB(f);GLint ok=0;glGetObjectParameterivARB(p,GL_OBJECT_LINK_STATUS_ARB,&ok);
 if(!ok){glDeleteObjectARB(p);return 0;}return p;
}
inline bool prepare(){
 auto& s=state();if(s.context!=SDL_GL_GetCurrentContext()){s=State{};s.context=SDL_GL_GetCurrentContext();}
 if(s.tried)return s.source && s.bounce && s.fbo && s.texture;s.tried=true;
 const std::string flashlight=
 #include "Shaders/flashlight_fog.glsl"
 ;
 s.source=link(std::string("#version 120\n#extension GL_ARB_texture_rectangle : enable\n")+flashlight+common+sourceBody);
 s.bounce=link(std::string("#version 120\n#extension GL_ARB_texture_rectangle : enable\n")+common+bounceBody);
 if(debugMode()==1)s.debug=link(std::string("#version 120\n#extension GL_ARB_texture_rectangle : enable\n")+debugBody);
 if(!s.source || !s.bounce)return false;
 GLint units=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);if(units<11)return false;
 glGenTextures(1,&s.texture);glBindTexture(GL_TEXTURE_2D,s.texture);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F_ARB,3,5,0,GL_RGBA,GL_FLOAT,nullptr);
 glGenFramebuffersEXT(1,&s.fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,s.fbo);
 glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,s.texture,0);
 glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
 if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT){glDeleteFramebuffersEXT(1,&s.fbo);s.fbo=0;return false;}
 if (std::getenv("SPRINTATHON_DIAGNOSTICS") && std::getenv("SPRINTATHON_DIAGNOSTICS")[0]=='1') std::fprintf(stderr,"Flashlight bounce: surface-lighting history passes ready\n");
 return true;
}
inline void quad(){glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();}
inline void draw(GLuint colour,GLuint depth,int width,int height,const GLfloat* projection,const GLfloat* modelview){
 if(!enabled() || !colour || !depth)return;
 GLint active=0,fbo=0,viewport[4],unpack=0,drawBuffer=0;glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer);const bool srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&fbo);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&unpack);
 auto previous=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
 glPushAttrib(GL_ALL_ATTRIB_BITS);glActiveTextureARB(GL_TEXTURE0_ARB);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
 const bool ready=prepare();auto& s=state();
 if(ready){
  glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_FOG);glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_BLEND);glDisable(GL_FRAMEBUFFER_SRGB_EXT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  auto bind=[&](GLhandleARB shader){
   glUseProgramObjectARB(shader);auto loc=[&](const char* n){return glGetUniformLocationARB(shader,n);};
   glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,depth);glUniform1iARB(loc("sceneDepth"),0);
   glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,colour);glUniform1iARB(loc("sceneColor"),1);
   glUniform2fARB(loc("imageSize"),width,height);glUniform2fARB(loc("depthProjection"),projection[10],projection[14]);glUniform4fARB(loc("viewProjection"),projection[0],projection[5],projection[8],projection[9]);
  };
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,s.fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,3,5);bind(s.source);
  const auto& f=SprintathonFlashlight::frame();auto loc=[&](const char* n){return glGetUniformLocationARB(s.source,n);};
  SprintathonFlashlight::bindShadowTexture();glUniform1iARB(loc("flashlightShadowMap"),9);
  glUniformMatrix4fvARB(loc("flashlightShadowMatrix"),1,GL_FALSE,f.shadowMatrix);
  glUniform4fvARB(loc("flashlightShadowParams"),1,f.shadowParams);glUniform4fvARB(loc("flashlightParams"),1,f.params);glUniform4fvARB(loc("flashlightProjection"),1,f.projection);glUniform4fvARB(loc("flashlightDepth"),1,f.depth);glUniform1fARB(loc("vfFlashlightIntensity"),1);
  glUniform1fARB(loc("bounceSaturation"),saturation());
  quad();glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(drawBuffer);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);bind(s.bounce);
  glActiveTextureARB(GL_TEXTURE0_ARB+2);glBindTexture(GL_TEXTURE_2D,s.texture);glUniform1iARB(glGetUniformLocationARB(s.bounce,"bounceData"),2);
  glUniform1fARB(glGetUniformLocationARB(s.bounce,"bounceGain"),gain());
  glUniform1fARB(glGetUniformLocationARB(s.bounce,"debugForce"),debugMode()==2?1.0f:0.0f);
  // Store light separately. Never add a tint to the completed scene.
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,s.fbo);
  if(!s.illumination)glGenTextures(1,&s.illumination);
  glActiveTextureARB(GL_TEXTURE0_ARB+10);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,s.illumination);
  if(s.width!=width || s.height!=height){
    s.valid=false;s.width=width;s.height=height;
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA16F_ARB,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
  }
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,s.illumination,0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,width,height);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
  glDisable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  s.valid=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
  if(s.valid){quad();std::copy_n(modelview,16,s.historyView);
    s.historyProjection[0]=projection[0];s.historyProjection[1]=projection[5];s.historyProjection[2]=projection[8];s.historyProjection[3]=projection[9];
    s.stamp=SDL_GetTicks();s.level=dynamic_world->current_level_number;s.tick=dynamic_world->tick_count;
    s.aim[0]=f.depth[2];s.aim[1]=f.depth[3];}
  if(debugMode()){
    static Uint32 logged=0;
    if(Uint32(SDL_GetTicks()-logged)>1000){logged=SDL_GetTicks();
      std::fprintf(stderr,"Bounce debug: gain=%.2f mode=%d valid=%d applying=%d size=%dx%d texture=%u source_ready=%d bounce_ready=%d\n",gain(),debugMode(),int(s.valid),int(s.applying),s.width,s.height,s.illumination,int(s.source!=0),int(s.bounce!=0));}
  }
  if(debugMode()==1 && s.valid && s.debug){
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(drawBuffer);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glUseProgramObjectARB(s.debug);
    glUniform1iARB(glGetUniformLocationARB(s.debug,"bounceData"),2);
    glUniform1iARB(glGetUniformLocationARB(s.debug,"lightHistory"),10);
    glUniform2fARB(glGetUniformLocationARB(s.debug,"historySize"),width,height);
    glUniform1fARB(glGetUniformLocationARB(s.debug,"historyApplied"),s.applying?1.0f:0.0f);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);quad();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,s.fbo);
  }
  // Restore the tiny source target for the next frame.
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,s.texture,0);
 }
 glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(drawBuffer);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,unpack);glUseProgramObjectARB(previous);glPopAttrib();glActiveTextureARB(active);
}
}
#endif
