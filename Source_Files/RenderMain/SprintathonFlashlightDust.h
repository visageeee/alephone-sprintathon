#ifndef SPRINTATHON_FLASHLIGHT_DUST_H
#define SPRINTATHON_FLASHLIGHT_DUST_H
#include "SprintathonFlashlightShadow.h"
#include <cstdint>
#include <string>

namespace SprintathonFlashlightDust {
inline bool enabled() {
    return graphics_preferences->flashlight_dust &&
        !(graphics_preferences->flashlight_dust_no_vacuum && static_world &&
          (static_world->environment_flags & _environment_vacuum)) &&
        graphics_preferences->flashlight_dust_density>0 && SprintathonFlashlight::frame().params[0]>0;
}
inline uint32_t hash(uint32_t n) {
    n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);
}
inline float random(uint32_t n) {return float(hash(n)&0xffffffu)/16777216.0f;}
inline GLhandleARB program() {
    static SDL_GLContext context=nullptr;
    static GLhandleARB value=0;
    static bool attempted=false;
    if(context!=SDL_GL_GetCurrentContext()){context=SDL_GL_GetCurrentContext();value=0;attempted=false;}
    if(attempted)return value;
    attempted=true;
    const char* vertex=R"DUST(#version 120
uniform mat4 dustProjection;
varying vec3 dustEye;
varying vec2 dustUV;
varying float dustFade;
void main(){
 dustEye=gl_Vertex.xyz;dustUV=gl_MultiTexCoord0.xy;dustFade=gl_Color.a;
 gl_Position=dustProjection*gl_Vertex;
}
)DUST";
    const std::string fragment=std::string("#version 120\n#extension GL_ARB_texture_rectangle : enable\n")+
#include "Shaders/flashlight_fog.glsl"
    +R"DUST(
uniform sampler2DRect dustDepth;
uniform vec4 dustViewport;
uniform vec2 dustSize;
varying vec3 dustEye;
varying vec2 dustUV;
varying float dustFade;
void main(){
 float radius=dot(dustUV,dustUV);if(radius>=1.0)discard;
 vec2 uv=(gl_FragCoord.xy-dustViewport.xy)/dustViewport.zw;
 float z=texture2DRect(dustDepth,uv*dustSize).r;
 float sceneZ=abs(flashlightDepth.y/(z*2.0-1.0+flashlightDepth.x));
 float gap=sceneZ+dustEye.z;if(gap<=0.0)discard;
 vec3 light=sprintathonFlashlightFog(dustEye);
 // Lift illuminated motes near the lamp without widening the beam or
 // overriding the visibility returned by its shadow map.
 float lampDistance=length(dustEye-vec3(flashlightProjection.xy,flashlightProjection.w));
 light*=1.0+1.5*(1.0-smoothstep(128.0,1024.0,lampDistance));
 float alpha=(1.0-radius)*(1.0-radius)*dustFade*smoothstep(0.0,16.0,gap)*0.40;
 gl_FragColor=vec4(light,alpha);
}
)DUST";
    const auto v=SprintathonFlashlightShadow::compile(GL_VERTEX_SHADER_ARB,vertex);
    const auto f=SprintathonFlashlightShadow::compile(GL_FRAGMENT_SHADER_ARB,fragment.c_str());
    if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return 0;}
    value=glCreateProgramObjectARB();glAttachObjectARB(value,v);glAttachObjectARB(value,f);
    glLinkProgramARB(value);glDeleteObjectARB(v);glDeleteObjectARB(f);
    GLint ok=0;glGetObjectParameterivARB(value,GL_OBJECT_LINK_STATUS_ARB,&ok);
    if(!ok){std::fprintf(stderr,"Flashlight dust shader link failed\n");glDeleteObjectARB(value);value=0;}
    return value;
}
inline void draw(const view_data* camera,GLuint depth,int width,int height,
                 const GLfloat* projection,const GLfloat* modelview) {
    if(!enabled() || !depth || width<=0 || height<=0)return;
    const auto shader=program();if(!shader)return;
    GLint active=0,viewport[4];glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);glGetIntegerv(GL_VIEWPORT,viewport);
    const auto previous=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_FOG);
    glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    glUseProgramObjectARB(shader);
    auto location=[&](const char* name){return glGetUniformLocationARB(shader,name);};
    const auto& f=SprintathonFlashlight::frame();
    SprintathonFlashlight::bindShadowTexture();
    glUniform1iARB(location("flashlightShadowMap"),9);
    glUniformMatrix4fvARB(location("flashlightShadowMatrix"),1,GL_FALSE,f.shadowMatrix);
    glUniform4fvARB(location("flashlightShadowParams"),1,f.shadowParams);
    glUniform4fvARB(location("flashlightParams"),1,f.params);
    glUniform4fvARB(location("flashlightProjection"),1,f.projection);
    glUniform4fvARB(location("flashlightDepth"),1,f.depth);
    glUniform1fARB(location("vfFlashlightIntensity"),1.0f); // independent of fog preferences
    glUniformMatrix4fvARB(location("dustProjection"),1,GL_FALSE,projection);
    glUniform4fARB(location("dustViewport"),viewport[0],viewport[1],viewport[2],viewport[3]);
    glUniform2fARB(location("dustSize"),width,height);
    glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,depth);
    glUniform1iARB(location("dustDepth"),0);
    // Stable world cells: camera movement reveals particles instead of carrying
    // them along. Bounded periodic drift never crosses the cell boundary.
    const float cell=512.0f,reach=3072.0f;
    const float origin[3]={float(camera->origin.x),float(camera->origin.y),float(camera->origin.z)};
    const int base[3]={int(std::floor(origin[0]/cell)),int(std::floor(origin[1]/cell)),int(std::floor(origin[2]/cell))};
    const double time=double(SDL_GetTicks())*0.001;
    const int densityPercent=std::max(0,std::min(1000,int(graphics_preferences->flashlight_dust_density)));
    // Each extra 200% adds another stable group; existing particles retain
    // their positions and counts at the original slider settings.
    const int candidates=9*((densityPercent+199)/200);
    glBegin(GL_QUADS);
    for(int z=-6;z<=6;++z)for(int y=-6;y<=6;++y)for(int x=-6;x<=6;++x)for(int mote=0;mote<candidates;++mote){
        uint32_t seed=hash(uint32_t(base[0]+x))^hash(uint32_t(base[1]+y)+0x9e3779b9u)^hash(uint32_t(base[2]+z)+0x85ebca6bu);
        seed=hash(seed+uint32_t(mote)*0x27d4eb2du);
        const float density=std::min(200,densityPercent-200*(mote/9))*0.0045f;
        if(random(seed)>density)continue;
        float delta[3];const int cells[3]={base[0]+x,base[1]+y,base[2]+z};
        for(int k=0;k<3;++k){
            const float phase=random(seed+10+k)*6.2831853f;
            delta[k]=(cells[k]+0.2f+0.6f*random(seed+20+k))*cell+
                60.0f*float(std::sin(time*(0.18+0.04*k)+phase))-origin[k];
        }
        const float distance=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
        if(distance>=reach || distance<=16.0f)continue;
        float eye[3]={};for(int r=0;r<3;++r)for(int c=0;c<3;++c)eye[r]+=modelview[c*4+r]*delta[c];
        if(eye[2]>=-16.0f)continue;
        const float fade=std::min(1.0f,(reach-distance)/512.0f)*std::min(1.0f,(distance-16.0f)/80.0f);
        const float size=0.7f+random(seed+30)*0.8f;
        glColor4f(1,1,1,fade);
        for(int corner=0;corner<4;++corner){
            const float u=(corner==0 || corner==3)?-1.0f:1.0f,v=corner<2?-1.0f:1.0f;
            glTexCoord2f(u,v);glVertex3f(eye[0]+u*size,eye[1]+v*size,eye[2]);
        }
    }
    glEnd();glUseProgramObjectARB(previous);glPopAttrib();glActiveTextureARB(active);
}
}
#endif
