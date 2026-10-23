#ifndef SPRINTATHON_FLASHLIGHT_H
#define SPRINTATHON_FLASHLIGHT_H
#include "OGL_Headers.h"
#include "OGL_Shader.h"
#include "preferences.h"
#include "player.h"
#include "map.h"
#include "SprintathonFlashlightState.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
namespace SprintathonFlashlight {
struct Frame {
    GLfloat params[4] = {0,0.38f,16384,0};
    GLfloat projection[4] = {1,1,0,0}; // lamp x, lamp y, radius, lamp z
    GLfloat depth[4] = {0,0,0,0};
    GLfloat viewport[4] = {0,0,1,1};
    GLuint shadowTexture = 0;
    GLfloat shadowMatrix[16] = {};
    GLfloat shadowParams[4] = {}; // strength, inverse map size, distance softness, maximum radius in texels

};
// Smooth deterministic noise: one brightness value shared by every pass.
inline float flickerSample(uint32_t n) {
    n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;n^=n>>16;
    const float v=float(n&0xffffffu)/16777216.0f;
    return v<0.08f ? 0.35f+0.45f*(v/0.08f) : 0.94f+0.06f*((v-0.08f)/0.92f);
}
inline float flickerBrightness(double seconds) {
    const double phase=seconds*12.0;
    const uint32_t cell=static_cast<uint32_t>(std::fmod(std::floor(phase),4294967296.0));
    float t=float(phase-std::floor(phase));t=t*t*(3.0f-2.0f*t);
    return flickerSample(cell)*(1.0f-t)+flickerSample(cell+1u)*t;
}
inline Frame& frame() { static Frame value; return value; }
// Smooth the lamp direction in world space, never the player's camera.
struct Aim {
    bool valid = false;
    float sprintWave = 0;
    float sprintBlend = 0;
    float direction[3] = {0,0,0};
    float origin[3] = {0,0,0};
    std::chrono::steady_clock::time_point time;
};
inline Aim& aim() { static Aim value; return value; }
// The physics step phase is driven by the footstep countdown, including
// bullet time. Smooth its tick-sized updates without a separate oscillator.
inline float sprintSwing(Aim& a, double dt, bool reset, bool sprinting, double stepPhase) {
    const float wave=float(std::sin(stepPhase*6.283185307179586));
    if(reset){a.sprintWave=wave;a.sprintBlend=0;return 0;}
    const double elapsed=std::max(0.0,std::min(dt,0.25));
    const float target=sprinting?1.0f:0.0f;
    a.sprintBlend+=(target-a.sprintBlend)*float(-std::expm1(-elapsed/0.12));
    a.sprintWave+=(wave-a.sprintWave)*float(-std::expm1(-elapsed/0.025));
    if(!sprinting && a.sprintBlend<0.001f)a.sprintBlend=0;
    return a.sprintWave*a.sprintBlend*0.09f;
}
inline void follow(const GLfloat* modelview, double dt, bool reset) {
    auto& a=aim();auto& f=frame();
    const float target[3]={-modelview[2],-modelview[6],-modelview[10]};
    float origin[3];
    for(int i=0;i<3;++i)
        origin[i]=-(modelview[i*4]*modelview[12]+modelview[i*4+1]*modelview[13]+modelview[i*4+2]*modelview[14]);
    float alignment=0,travel=0;
    for(int i=0;i<3;++i){alignment+=a.direction[i]*target[i];float delta=origin[i]-a.origin[i];travel+=delta*delta;}
    reset=reset || !a.valid || dt>0.25 || alignment<0.5f || travel>2048.0f*2048.0f;
    const float blend=reset?1.0f:float(-std::expm1(-std::max(0.0,dt)/0.050));
    float length2=0;
    for(int i=0;i<3;++i){a.direction[i]+=(target[i]-a.direction[i])*blend;length2+=a.direction[i]*a.direction[i];a.origin[i]=origin[i];}
    const float inverseLength=1.0f/std::sqrt(std::max(length2,0.000001f));
    for(float& v:a.direction)v*=inverseLength;
    float eye[3]={0,0,0};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)eye[row]+=modelview[col*4+row]*a.direction[col];
    float x=eye[0]/std::max(-eye[2],0.1f),y=eye[1]/std::max(-eye[2],0.1f);
    const float sway=std::max(0,std::min(200,int(graphics_preferences->flashlight_sway)))/100.0f;
    const bool sprintMoving=current_player && current_player->sprinting &&
        (current_player->variables.velocity!=0 || current_player->variables.perpendicular_velocity!=0);
    const double stepPhase=current_player ? double(current_player->variables.step_phase)/FIXED_ONE : 0.0;
    x+=sprintSwing(a,dt,reset,sprintMoving,stepPhase);
    x*=sway;y*=sway;
    // Limit lag to eight degrees so quick turns never lose the beam entirely.
    const float slope=std::sqrt(x*x+y*y),scale=slope>0.14054f?0.14054f/slope:1.0f;
    // Aim slightly across the scene from the lower-right lamp: three degrees
    // inward and two degrees upward. Apply after sway limiting so this resting
    // angle remains stable. A centred lamp has no sideways bias.
    const float sideOffset=std::max(0,std::min(100,int(graphics_preferences->flashlight_offset)))/100.0f;
    f.depth[2]=x*scale-0.052408f*sideOffset;
    f.depth[3]=y*scale+0.034921f;
    a.valid=true;
}
inline void capture(const GLfloat* projection, const GLfloat* modelview, bool allowed) {
    auto& f=frame();
    f.shadowParams[0]=0;
    f.shadowParams[2]=graphics_preferences->flashlight_distance_softness ? 1.0f : 0.0f;
    f.shadowParams[3]=32.0f*std::max(0,std::min(100,int(graphics_preferences->flashlight_max_softness)))/100.0f;
    f.params[0]=allowed && graphics_preferences->flashlight_enabled && SprintathonFlashlightState::switchedOn() ?
        std::max(0,std::min(200,int(graphics_preferences->flashlight_brightness)))*0.015f : 0.0f;
    f.params[1]=0.38f*std::max(5,std::min(200,int(graphics_preferences->flashlight_width)))/100.0f;
    auto& a=aim();
    if(f.params[0]<=0.0f){a.valid=false;return;}
    const auto now=std::chrono::steady_clock::now();
    if(graphics_preferences->flashlight_flicker)
        f.params[0]*=flickerBrightness(std::chrono::duration<double>(now.time_since_epoch()).count());
    const double dt=a.valid?std::chrono::duration<double>(now-a.time).count():0.0;
    follow(modelview,dt,!a.valid);a.time=now;
    f.params[2]=16384.0f*100.0f/std::max(50,std::min(200,int(graphics_preferences->flashlight_falloff)));
    f.params[3]=std::max(0,std::min(400,int(graphics_preferences->flashlight_offset)))/100.0f;
    // Physical camera-space offset, independent of FOV and viewport. The
    // screen-edge placement kept the light almost on the camera axis, hiding
    // shadows behind their casters even at large Offset settings.
    f.projection[0]=192.0f*f.params[3];
    f.projection[1]=-128.0f*f.params[3];
    f.projection[2]=0.48f*std::max(0,std::min(100,int(graphics_preferences->flashlight_origin_width)));
    f.projection[3]=-32.0f*f.params[3];
    f.depth[0]=projection[10];f.depth[1]=projection[14];
    GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    for(int i=0;i<4;++i)f.viewport[i]=float(viewport[i]);
}
inline void bindShadowTexture() {
    GLint active;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
    glActiveTextureARB(GL_TEXTURE0_ARB+9);
    glBindTexture(GL_TEXTURE_2D,frame().shadowParams[0]>0.0f?frame().shadowTexture:0);
    glActiveTextureARB(active);
}
inline void bind(Shader* shader) {
    const auto& f=frame();
    bindShadowTexture();
    glUniformMatrix4fvARB(glGetUniformLocationARB(glGetHandleARB(GL_PROGRAM_OBJECT_ARB),"flashlightShadowMatrix"),1,GL_FALSE,f.shadowMatrix);
    shader->setVector4(Shader::U_FlashlightShadowParams,f.shadowParams[0],f.shadowParams[1],f.shadowParams[2],f.shadowParams[3]);
    shader->setVector4(Shader::U_FlashlightParams,f.params[0],f.params[1],f.params[2],f.params[3]);
    if(f.params[0]<=0.0f)return;
    shader->setVector4(Shader::U_FlashlightProjection,f.projection[0],f.projection[1],f.projection[2],f.projection[3]);
    shader->setVector4(Shader::U_FlashlightDepth,f.depth[0],f.depth[1],f.depth[2],f.depth[3]);
    shader->setVector4(Shader::U_FlashlightViewport,f.viewport[0],f.viewport[1],f.viewport[2],f.viewport[3]);
}
inline void bindRaw(const GLint* locations) {
    const auto& f=frame();
    bindShadowTexture();
    glUniform1iARB(locations[4],9);
    glUniformMatrix4fvARB(locations[5],1,GL_FALSE,f.shadowMatrix);
    glUniform4fvARB(locations[6],1,f.shadowParams);
    glUniform4fvARB(locations[0],1,f.params);
    if(f.params[0]<=0.0f)return;
    glUniform4fvARB(locations[1],1,f.projection);
    glUniform4fvARB(locations[2],1,f.depth);
    glUniform4fvARB(locations[3],1,f.viewport);
}
}
#endif
