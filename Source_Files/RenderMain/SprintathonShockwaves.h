#ifndef SPRINTATHON_SHOCKWAVES_H
#define SPRINTATHON_SHOCKWAVES_H
#include "SprintathonFlashlightShadow.h"
#include "effects.h"
#include "SprintathonShockwaveEvents.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace SprintathonShockwaves {
struct State {long tick=-1;uint64_t motionTick=0;Uint32 stamp=0;float fraction=0;int level=-1;};
inline State& state(){static State s;return s;}
inline bool update(bool allowed){
    SprintathonShockwaveEvents::sync();auto& waves=SprintathonShockwaveEvents::queue().waves;
    auto& s=state();const long tick=dynamic_world->tick_count;
    const uint64_t motionTick=sprintathon_projectile_render_tick();
    const Uint32 now=SDL_GetTicks();
    // Global ticks continue during skipped bullet-time world updates. Follow
    // the projectile clock for both expansion and expiry instead.
    if(s.level!=dynamic_world->current_level_number || tick<s.tick || motionTick!=s.motionTick)
        s.fraction=0;
    else
        s.fraction=std::min(1.0f,s.fraction+float(Uint32(now-s.stamp))*
            TICKS_PER_SECOND/1000.0f*(sprintathon_bullet_time_active()?0.35f:1.0f));
    s.tick=tick;s.motionTick=motionTick;s.stamp=now;s.level=dynamic_world->current_level_number;
    const bool enabled=allowed && graphics_preferences->explosion_shockwaves && graphics_preferences->explosion_shockwave_strength>0;
    if(std::getenv("SPRINTATHON_SHOCKWAVE_TRACE") && !waves.empty()){
        static long logged=-1;
        if(logged!=tick){logged=tick;std::fprintf(stderr,"Shockwave update tick=%ld enabled=%d queued=%u oldest_age=%ld rocket=%d\n",tick,int(enabled),unsigned(waves.size()),tick-waves.front().tick,int(waves.front().rocket));}
    }
    if(!enabled)waves.clear();
    waves.erase(std::remove_if(waves.begin(),waves.end(),[&](const SprintathonShockwaveEvents::Wave& w){return motionTick<w.motionTick || motionTick-w.motionTick>=(w.rocket?8u:6u);}),waves.end());
    return enabled && !waves.empty();
}
inline GLhandleARB program(){
    static SDL_GLContext context=nullptr;static GLhandleARB p=0;static bool tried=false;
    if(context!=SDL_GL_GetCurrentContext()){context=SDL_GL_GetCurrentContext();p=0;tried=false;}
    if(tried)return p;tried=true;
    const char* vertex=R"SW(#version 120
void main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}
)SW";
    const char* fragment=R"SW(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect sceneColor;
uniform sampler2DRect sceneDepth;
uniform vec2 imageSize;
uniform vec2 depthProjection;
uniform vec4 viewProjection; // x/y scale and asymmetric projection offset
uniform vec4 centres[8]; // explosion positions in camera space
uniform int waveCount;
uniform float debugIgnoreOcclusion;
uniform vec4 waves[8]; // pixel centre, world radius, displacement strength
uniform vec4 shapes[8]; // eye depth, ring width, occlusion tolerance, white highlight
float eyeDepth(vec2 p){float z=texture2DRect(sceneDepth,p).r;return abs(depthProjection.y/(2.0*z-1.0+depthProjection.x));}
vec3 surfaceAt(vec2 p){float z=eyeDepth(p);return vec3((p/imageSize*2.0-1.0+viewProjection.zw)/viewProjection.xy*z,-z);}
bool nearbyImpactPlane(vec2 pixel,vec3 centre,float tolerance){
 if(pixel.x<1.0 || pixel.y<1.0 || pixel.x>imageSize.x-2.0 || pixel.y>imageSize.y-2.0)return false;
 vec2 p=floor(pixel)+0.5;
 vec3 hit=surfaceAt(p);
 // Only nearby geometry may validate an impact. Distant scenery or sky
 // exposed around a foreground wall must not reveal a hidden explosion.
 if(length(hit-centre)>2048.0)return false;
 vec3 n=cross(surfaceAt(p+vec2(1,0))-hit,surfaceAt(p+vec2(0,1))-hit);
 float len=length(n);if(len<0.00001)return false;
 return abs(dot(n/len,centre-hit))<=tolerance;
}
bool hiddenCentre(vec2 pixel,vec3 centre,float tolerance){
 vec2 p=clamp(floor(pixel)+0.5,vec2(0.5),imageSize-1.5);
 vec3 hit=surfaceAt(p);
 if(-hit.z+tolerance>=-centre.z)return false;
 if(nearbyImpactPlane(pixel,centre,tolerance))return false;
 // Explosion billboards can cover the impact pixel. Look for the same
 // impact plane around the sprite, rather than increasing the depth bias.
 float radius=clamp(abs(viewProjection.y)*imageSize.y*0.5*1536.0/max(-centre.z,1.0),4.0,128.0);
 for(int ring=0;ring<2;++ring)for(int i=0;i<8;++i){
  float angle=float(i)*0.7853981634;
  vec2 offset=vec2(cos(angle),sin(angle))*radius*(ring==0?0.5:1.0);
  if(nearbyImpactPlane(pixel+offset,centre,tolerance))return false;
 }
 return true;
}
void main(){
 vec2 p=gl_TexCoord[0].xy*imageSize;
 float z=eyeDepth(p);vec2 displacement=vec2(0.0);float highlight=0.0;
 vec2 ndc=p/imageSize*2.0-1.0;
 vec3 surface=vec3((ndc+viewProjection.zw)/viewProjection.xy*z,-z);
 // Derivatives of the actual surface give the outward direction along its
 // projected slope. Compute before divergent branches for GLSL 1.20.
 vec3 dx=dFdx(surface),dy=dFdy(surface);
 for(int i=0;i<8;++i){
  if(i>=waveCount)break;
  vec4 wave=waves[i],shape=shapes[i];
  // Reject hidden blast centres before applying any screen-space ripple.
  // Intersect the expanding 3D sphere with visible geometry. Wall/floor
  // angles now shape the ring instead of merely clipping a flat circle.
  vec3 delta=surface-centres[i].xyz;float distance=length(delta);
  // Keep a compressed ring at least a few pixels wide on steep surfaces.
  vec2 gradient=vec2(dot(delta,dx),dot(delta,dy))/max(distance,0.001);
  float footprint=min(length(gradient)*1.5,shape.y*4.0);
  float band=(distance-wave.z)/max(shape.y,footprint);
  if(abs(band)>=1.0)continue;
  // Depth validation is only needed on pixels touched by this ring.
  if(debugIgnoreOcclusion<0.5 && hiddenCentre(wave.xy,centres[i].xyz,shape.z))continue;
  // One outward displacement lobe, not alternating push/pull bands.
  float profile=(1.0-band*band);profile*=profile;
  vec2 direction=vec2(dot(delta,dx),dot(delta,dy));
  displacement+=direction/max(length(direction),0.001)*profile*wave.w;
  highlight+=profile*shape.w;
 }
 vec2 q=clamp(p+displacement,vec2(0.5),imageSize-0.5);
 // Don't pull foreground silhouettes into the refracted background.
 // Compare against the local plane, not a flat-depth threshold: a steep
 // continuous wall may change depth substantially over a few pixels.
 vec3 normal=cross(dx,dy);float normalLength=length(normal);
 if(normalLength>0.00001 && abs(dot(normal/normalLength,surfaceAt(q)-surface))>32.0)q=p;
 vec4 color=texture2DRect(sceneColor,q);
 // A restrained white crest shares the distortion ring and its occlusion.
 color.rgb=mix(color.rgb,vec3(1.0),clamp(highlight,0.0,0.25));
 gl_FragColor=color;
}
)SW";
    auto v=SprintathonFlashlightShadow::compile(GL_VERTEX_SHADER_ARB,vertex);
    auto f=SprintathonFlashlightShadow::compile(GL_FRAGMENT_SHADER_ARB,fragment);
    if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return 0;}
    p=glCreateProgramObjectARB();glAttachObjectARB(p,v);glAttachObjectARB(p,f);glLinkProgramARB(p);
    glDeleteObjectARB(v);glDeleteObjectARB(f);GLint ok=0;glGetObjectParameterivARB(p,GL_OBJECT_LINK_STATUS_ARB,&ok);
    if(!ok){std::fprintf(stderr,"Explosion shockwave shader link failed\n");glDeleteObjectARB(p);p=0;}
    return p;
}
inline void draw(const view_data* camera,GLuint color,GLuint depth,int width,int height,const GLfloat* projection,const GLfloat* modelview){
    if(!color || !depth || width<=0 || height<=0)return;
    auto& s=state();GLfloat waves[32]={},shapes[32]={},centres[32]={};int count=0;
    for(const auto& w:SprintathonShockwaveEvents::queue().waves){
        const float age=(s.motionTick-w.motionTick+s.fraction)/float(TICKS_PER_SECOND);
        const float life=w.rocket?0.25f:0.20f;
        if(age<=0 || age>=life)continue;
        const float delta[3]={float(w.position.x-camera->origin.x),float(w.position.y-camera->origin.y),float(w.position.z-camera->origin.z)};
        float eye[3]={};for(int r=0;r<3;++r)for(int c=0;c<3;++c)eye[r]+=modelview[c*4+r]*delta[c];
        const float z=-eye[2];if(z<64)continue;
        const float x=(projection[0]*eye[0]/z-projection[8])*0.5f+0.5f;
        const float y=(projection[5]*eye[1]/z-projection[9])*0.5f+0.5f;
        // Offscreen centres lack a reliable depth visibility sample.
        if(x<=0 || x>=1 || y<=0 || y>=1)continue;
        const float progress=age/life;
        const float radius=(w.rocket?5.0f:3.5f)*WORLD_ONE*progress;
        const float fade=std::min(1.0f,progress*12.0f)*(1-progress)*(1-progress);
        waves[count*4]=x*width;waves[count*4+1]=y*height;waves[count*4+2]=radius;
        waves[count*4+3]=(w.rocket?12.0f:8.0f)*(height/1080.0f)*fade*
            std::max(0,std::min(200,int(graphics_preferences->explosion_shockwave_strength)))/100.0f;
        shapes[count*4]=z;shapes[count*4+1]=0.22f*WORLD_ONE;shapes[count*4+2]=32.0f;
        shapes[count*4+3]=0.10f*fade*std::max(0,std::min(200,int(graphics_preferences->explosion_shockwave_strength)))/100.0f;
        for(int k=0;k<3;++k)centres[count*4+k]=eye[k];
        if(++count>=8)break;
    }
    if(std::getenv("SPRINTATHON_SHOCKWAVE_TRACE")){
        static long logged=-1;
        if(logged!=s.tick){logged=s.tick;std::fprintf(stderr,"Shockwave draw tick=%ld submitted=%d ignore_occlusion=%d\n",s.tick,count,int(std::getenv("SPRINTATHON_SHOCKWAVE_NO_OCCLUSION")!=nullptr));}
    }
    if(!count)return;const auto shader=program();if(!shader)return;
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);const auto previous=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_FOG);
    glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glUseProgramObjectARB(shader);
    auto loc=[&](const char* name){return glGetUniformLocationARB(shader,name);};
    glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,color);glUniform1iARB(loc("sceneColor"),0);
    glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,depth);glUniform1iARB(loc("sceneDepth"),1);
    glUniform2fARB(loc("imageSize"),width,height);glUniform2fARB(loc("depthProjection"),projection[10],projection[14]);
    glUniform4fARB(loc("viewProjection"),projection[0],projection[5],projection[8],projection[9]);
    glUniform4fvARB(loc("centres"),count,centres);
    glUniform1fARB(loc("debugIgnoreOcclusion"),std::getenv("SPRINTATHON_SHOCKWAVE_NO_OCCLUSION")?1.0f:0.0f);
    glUniform1iARB(loc("waveCount"),count);glUniform4fvARB(loc("waves"),count,waves);glUniform4fvARB(loc("shapes"),count,shapes);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    glUseProgramObjectARB(previous);glPopAttrib();glActiveTextureARB(active);
}
}
#endif
