#ifndef SPRINTATHON_HUD_CURVE_H
#define SPRINTATHON_HUD_CURVE_H
#include "cseries.h"
#ifdef HAVE_OPENGL
#include "OGL_Headers.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
namespace SprintathonHUDCurve {
inline bool& capturing(){static bool value=false;return value;}
inline void alphaBlend(){
    if(capturing())glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    else glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
inline float strength(){
    const char* p=std::getenv("SPRINTATHON_HUD_CURVE");if(!p)return 0;
    float v=std::strtof(p,nullptr);return std::isfinite(v)?std::max(0.0f,std::min(1.0f,v)):0;
}
struct Target {
    SDL_GLContext context=nullptr;
    GLuint fbo=0,texture=0,depth=0;GLhandleARB shader=0;
    int width=0,height=0;GLint previous=0,active=0;GLhandleARB program=0;
};
inline Target& target(){static Target t;return t;}
inline const char* fragment(){return R"HUD(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect hud;
uniform vec2 size;
uniform float strength;
varying vec2 uv;
void main(){
    float side=uv.x<.5?1.0:-1.0;
    float edge=min(uv.x,1.0-uv.x);
    float bottom=1.0-smoothstep(.24,.42,uv.y);
    float outer=1.0-smoothstep(.28,.43,edge);
    float rise=max(0.0,uv.y-.012);
    float bow=1.0-exp(-pow(max(0.0,edge-.01)/.14,2.0));
    vec2 source=uv;
    // The complete bottom strip is identity, not just the outer corner.
    // Above it, stretch the inner panel upward into a visible outward bow.
    source.y-=rise*.32*bow*bottom*outer*strength;
    source.x+=side*edge*.12*smoothstep(.012,.20,uv.y)*bottom*outer*strength;
    if(source.x<0.0 || source.x>1.0 || source.y<0.0 || source.y>1.0){gl_FragColor=vec4(0.0);return;}
    gl_FragColor=texture2DRect(hud,source*size);
}
)HUD";}
inline GLhandleARB compile(GLenum kind,const char* code){
    GLhandleARB s=glCreateShaderObjectARB(kind);glShaderSourceARB(s,1,&code,nullptr);glCompileShaderARB(s);
    GLint ok=0;glGetObjectParameterivARB(s,GL_OBJECT_COMPILE_STATUS_ARB,&ok);
    if(!ok){glDeleteObjectARB(s);return 0;}return s;
}
inline bool prepare(int w,int h){
    auto& t=target();auto context=SDL_GL_GetCurrentContext();
    if(t.context!=context){t=Target{};t.context=context;}
    if(!t.shader){
        const char* vertex="#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}";
        auto v=compile(GL_VERTEX_SHADER_ARB,vertex),f=compile(GL_FRAGMENT_SHADER_ARB,fragment());
        if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return false;}
        t.shader=glCreateProgramObjectARB();glAttachObjectARB(t.shader,v);glAttachObjectARB(t.shader,f);glLinkProgramARB(t.shader);
        glDeleteObjectARB(v);glDeleteObjectARB(f);GLint ok=0;glGetObjectParameterivARB(t.shader,GL_OBJECT_LINK_STATUS_ARB,&ok);
        if(!ok){glDeleteObjectARB(t.shader);t.shader=0;return false;}
    }
    if(t.width==w && t.height==h && t.fbo)return true;
    if(t.fbo)glDeleteFramebuffersEXT(1,&t.fbo);
    if(t.texture)glDeleteTextures(1,&t.texture);
    if(t.depth)glDeleteRenderbuffersEXT(1,&t.depth);
    t.fbo=t.texture=t.depth=0;t.width=t.height=0;
    GLint buffer=0,renderbuffer=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&buffer);glGetIntegerv(GL_RENDERBUFFER_BINDING_EXT,&renderbuffer);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
    glGenTextures(1,&t.texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,t.texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,buffer);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffersEXT(1,&t.fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,t.texture,0);
    glGenRenderbuffersEXT(1,&t.depth);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,t.depth);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_DEPTH24_STENCIL8_EXT,w,h);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,t.depth);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_STENCIL_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,t.depth);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,renderbuffer);
    bool ok=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.previous);
    if(ok){t.width=w;t.height=h;}return ok;
}
inline bool begin(int w,int h){
    if(strength()<=0 || w<=0 || h<=0 || capturing())return false;
    auto& t=target();
    // Context reset must precede saving state.
    if(t.context!=SDL_GL_GetCurrentContext()){t=Target{};t.context=SDL_GL_GetCurrentContext();}
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&t.previous);glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&t.active);t.program=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glActiveTextureARB(GL_TEXTURE0_ARB);
    if(!prepare(w,h)){glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.previous);glPopAttrib();glActiveTextureARB(t.active);return false;}
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.fbo);glViewport(0,0,w,h);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glStencilMask(~0u);
    glClearColor(0,0,0,0);glClearStencil(0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glUseProgramObjectARB(0);capturing()=true;return true;
}
inline void end(){
    if(!capturing())return;
    capturing()=false;auto& t=target();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.previous);
    glPopAttrib(); // Restore destination draw buffer, viewport and sRGB before compositing.
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(0,0,t.width,t.height);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_FOG);
    for(int i=0;i<6;++i)glDisable(GL_CLIP_PLANE0+i);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgramObjectARB(t.shader);glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,t.texture);
    glUniform1iARB(glGetUniformLocationARB(t.shader,"hud"),0);
    glUniform2fARB(glGetUniformLocationARB(t.shader,"size"),float(t.width),float(t.height));
    glUniform1fARB(glGetUniformLocationARB(t.shader,"strength"),strength());
    glBegin(GL_QUADS);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,0,0);glVertex2f(-1,-1);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,1,0);glVertex2f(1,-1);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,1,1);glVertex2f(1,1);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,0,1);glVertex2f(-1,1);
    glEnd();glUseProgramObjectARB(t.program);glPopAttrib();glActiveTextureARB(t.active);
}
}
#endif
#endif
