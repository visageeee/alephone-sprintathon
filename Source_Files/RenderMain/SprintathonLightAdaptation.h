#ifndef SPRINTATHON_LIGHT_ADAPTATION_H
#define SPRINTATHON_LIGHT_ADAPTATION_H
#include <chrono>
#include <cstdlib>
namespace SprintathonLightAdaptation {
// An explicit process-local override takes precedence, preserving diagnostics.
inline bool enabled(){const char* v=std::getenv("SPRINTATHON_LIGHT_ADAPTATION");
 return v && (v[0]=='0' || v[0]=='1') ? v[0]=='1' : graphics_preferences->sprintathon_light_adaptation;}
inline GLhandleARB build(const char* fragment){
 const char* vertex=R"LA(#version 120
varying vec2 uv;
void main(){uv=gl_MultiTexCoord0.xy;gl_Position=gl_Vertex;}
)LA";
 const auto v=SprintathonFlashlightShadow::compile(GL_VERTEX_SHADER_ARB,vertex);
 const auto f=SprintathonFlashlightShadow::compile(GL_FRAGMENT_SHADER_ARB,fragment);
 if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return 0;}
 const auto p=glCreateProgramObjectARB();glAttachObjectARB(p,v);glAttachObjectARB(p,f);glLinkProgramARB(p);
 glDeleteObjectARB(v);glDeleteObjectARB(f);GLint ok=0;glGetObjectParameterivARB(p,GL_OBJECT_LINK_STATUS_ARB,&ok);
 if(!ok){glDeleteObjectARB(p);return 0;}return p;
}
inline void quad(){glBegin(GL_QUADS);
 glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
 glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();}
struct State {
 SDL_GLContext context=nullptr;GLuint textures[2]={},fbo=0;GLhandleARB meter=0,apply=0;
 bool tried=false,valid=false;int index=0,level=-1;long tick=-1;
 std::chrono::steady_clock::time_point last;
};
inline void draw(GLuint scene,int width,int height){
 if(!enabled() || !scene || width<=0 || height<=0)return;
 static State s;
 if(s.context!=SDL_GL_GetCurrentContext()){s=State();s.context=SDL_GL_GetCurrentContext();}
 if(!s.tried){s.tried=true;
  const char* meter=R"LA(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect scene;
uniform sampler2D history;
uniform vec2 dimensions;
uniform float dt,reset;
varying vec2 uv;
void main(){
 float total=0.0;
 // Fixed central grid, excluding the weapon and most peripheral sky.
 for(int y=0;y<8;++y)for(int x=0;x<8;++x){
  vec2 p=vec2(.12,.30)+(vec2(float(x),float(y))+.5)/8.0*vec2(.76,.58);
  vec3 c=texture2DRect(scene,p*dimensions).rgb;
  float l=dot(c,vec3(.2126,.7152,.0722));
  total+=log(clamp(l,.025,.65));
 }
 float measured=exp(total/64.0);
 vec2 previous=texture2D(history,vec2(.5)).rg;
 float filtered=mix(previous.g,measured,1.0-exp(-dt/.45));
 if(reset>.5)filtered=measured;
 float target=clamp(pow(.16/max(filtered,.025),.40),.85,1.75);
 float exposure=mix(previous.r,target,1.0-exp(-dt/(target>previous.r?.90:.30)));
 if(reset>.5)exposure=1.0;
 gl_FragColor=vec4(exposure,filtered,0,1);
}
)LA";
  const char* apply=R"LA(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect scene;
uniform sampler2D history;
uniform vec2 dimensions;
varying vec2 uv;
void main(){
 vec4 c=texture2DRect(scene,uv*dimensions);
 float exposure=texture2D(history,vec2(.5)).r;
 // Roll off the boost near white, preserving black and highlight detail.
 vec3 boost=c.rgb*exposure;
 if(exposure>1.0)boost=c.rgb+(exposure-1.0)*c.rgb*(vec3(1.0)-clamp(c.rgb,0.0,1.0));
 gl_FragColor=vec4(boost,c.a);
}
)LA";
  s.meter=build(meter);s.apply=build(apply);
 }
 if(!s.meter || !s.apply)return;
 GLint previousFbo=0,active=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&previousFbo);glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
 const auto previousProgram=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
 glPushAttrib(GL_ALL_ATTRIB_BITS);
 glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);
 glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_FOG);
 glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
 glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
 glActiveTextureARB(GL_TEXTURE1_ARB);
 bool ready=true;
 if(!s.fbo){
  glGenFramebuffersEXT(1,&s.fbo);glGenTextures(2,s.textures);
  const GLfloat initial[4]={1,.16f,0,1};
  for(int i=0;i<2;++i){glBindTexture(GL_TEXTURE_2D,s.textures[i]);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F_ARB,1,1,0,GL_RGBA,GL_FLOAT,initial);
  }
 }
 glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,s.fbo);
 glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,s.textures[1-s.index],0);
 glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
 ready=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
 if(ready){
  const auto now=std::chrono::steady_clock::now();
  const float elapsed=s.valid?std::chrono::duration<float>(now-s.last).count():0;
  const bool reset=!s.valid || elapsed>.5f || s.level!=dynamic_world->current_level_number || dynamic_world->tick_count<s.tick;
  s.last=now;s.level=dynamic_world->current_level_number;s.tick=dynamic_world->tick_count;s.valid=true;
  glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,scene);
  glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_2D,s.textures[s.index]);
  glUseProgramObjectARB(s.meter);
  glUniform1iARB(glGetUniformLocationARB(s.meter,"scene"),0);glUniform1iARB(glGetUniformLocationARB(s.meter,"history"),1);
  glUniform2fARB(glGetUniformLocationARB(s.meter,"dimensions"),width,height);
  glUniform1fARB(glGetUniformLocationARB(s.meter,"dt"),std::min(elapsed,.1f));
  glUniform1fARB(glGetUniformLocationARB(s.meter,"reset"),reset?1:0);
  glViewport(0,0,1,1);quad();s.index=1-s.index;
 }
 glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,previousFbo);
 // Restore the scene viewport and its draw-buffer selection before compositing.
 glPopAttrib();
 if(ready){
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);
  glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_FOG);
  glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,scene);
  glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_2D,s.textures[s.index]);
  glUseProgramObjectARB(s.apply);
  glUniform1iARB(glGetUniformLocationARB(s.apply,"scene"),0);glUniform1iARB(glGetUniformLocationARB(s.apply,"history"),1);
  glUniform2fARB(glGetUniformLocationARB(s.apply,"dimensions"),width,height);quad();glPopAttrib();
 }
 glUseProgramObjectARB(previousProgram);glActiveTextureARB(active);
}
}
#endif
