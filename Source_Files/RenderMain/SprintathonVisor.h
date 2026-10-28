#ifndef SPRINTATHON_VISOR_H
#define SPRINTATHON_VISOR_H
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include "preferences.h"
// Render after the weapon/post effects, before the HUD. Controlled by preferences.
namespace SprintathonVisor {
inline float strength(){return input_preferences ? std::max(0, std::min(300,
    int(input_preferences->sprintathon_visor_strength)))/100.0f : 0.0f;}
inline bool enabled(){return input_preferences && input_preferences->sprintathon_visor_effects && strength()>0;}
struct State {
    bool initialized=false, submerged=false, wettable=false;
    int level=-1,tick=-1,object=-1;unsigned long long generation=0;
    float wetAge=20;unsigned splash=0;
    float sun[4]={0,0,0,0};
    int lightCount=0;
    float visorLightPositions[16]={}, lightColors[16]={};
    void reset(){*this=State{};}
    void update(int now,int map,unsigned long long mapGeneration,int actor,bool under,bool water,int /*energy*/,bool allowed){
        if(!allowed){reset();return;}
        const bool fresh=!initialized || level!=map || generation!=mapGeneration || object!=actor || now<tick || now-tick>60;
        if(fresh){reset();initialized=true;level=map;generation=mapGeneration;object=actor;tick=now;submerged=under;wettable=water;}
        const float dt=std::min(0.1f,std::max(0,now-tick)/30.0f);tick=now;
        if(!fresh && submerged && !under && wettable){wetAge=0;++splash;}
        else wetAge+=dt;
        if(under){wetAge=20;wettable=water;}
        submerged=under;

    }
    bool active()const{return initialized && !submerged;}
};
inline State& state(){static State s;return s;}
inline void projectSun(const float* projection,const float* modelview,const float* direction,float brightness,float angularRadius){
    auto& s=state();std::fill(s.sun,s.sun+4,0.0f);
    if(!s.initialized || s.submerged || brightness<=0)return;
    float eye[4]={},clip[4]={};
    for(int row=0;row<4;++row)for(int col=0;col<3;++col)eye[row]+=modelview[col*4+row]*direction[col];
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)clip[row]+=projection[col*4+row]*eye[col];
    if(clip[3]<=0.05f || eye[2]>=0)return;
    const float x=clip[0]/clip[3],y=clip[1]/clip[3];
    if(std::abs(x)>.98f || std::abs(y)>.98f)return;
    s.sun[0]=x*.5f+.5f;s.sun[1]=y*.5f+.5f;
    s.sun[2]=std::abs(projection[5])*angularRadius/(2*clip[3]);
    s.sun[3]=std::min(1.5f,brightness);
}
// Camera-relative coordinates use the same rotation as scene geometry.
inline void addLight(const float* projection,const float* modelview,const float* delta,
                     float r,float g,float b,float radius){
    auto& s=state();if(!s.active() || radius<=0)return;
    float eye[3]={};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)eye[row]+=modelview[col*4+row]*delta[col];
    const float depth=-eye[2];if(depth<64)return;
    float x=(projection[0]*eye[0]/depth-projection[8])*.5f+.5f;
    float y=(projection[5]*eye[1]/depth-projection[9])*.5f+.5f;
    if(x<.01f || x>.99f || y<.01f || y>.99f)return;
    const float d2=delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2];
    float falloff=std::max(0.0f,1.0f-d2/(radius*radius));
    falloff=falloff*falloff/(1.0f+4.0f*d2/(radius*radius));
    r*=falloff;g*=falloff;b*=falloff;
    const float score=std::max(r,std::max(g,b));if(score<.002f)return;
    int slot=0;while(slot<s.lightCount && score<=s.lightColors[slot*4+3])++slot;
    if(slot>=4)return;
    for(int i=std::min(s.lightCount,3);i>slot;--i)for(int k=0;k<4;++k){
        s.visorLightPositions[i*4+k]=s.visorLightPositions[(i-1)*4+k];s.lightColors[i*4+k]=s.lightColors[(i-1)*4+k];
    }
    s.lightCount=std::min(4,s.lightCount+1);
    // Small world-space bias tolerates emitters lying on their own surface.
    const float z=std::max(1.0f,depth-8.0f);
    s.visorLightPositions[slot*4]=x;s.visorLightPositions[slot*4+1]=y;
    s.visorLightPositions[slot*4+2]=(-projection[10]+projection[14]/z)*.5f+.5f;
    s.visorLightPositions[slot*4+3]=0;
    s.lightColors[slot*4]=r;s.lightColors[slot*4+1]=g;
    s.lightColors[slot*4+2]=b;s.lightColors[slot*4+3]=score;
}
inline const char* fragment(){return R"VISOR(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect sceneTexture;
uniform sampler2DRect depthTexture;
uniform sampler2D scratchTexture;
uniform vec2 size;
uniform vec4 effects; // wet age, strength, splash seed, unused
uniform vec4 sun;
uniform int lightCount;
uniform vec4 lights[4];
uniform vec4 lightColors[4];
varying vec2 uv;
float hash(float n){return fract(sin(n*127.1+31.7)*43758.5453);}
float sourceVisibility(){
    float result=0.0;
    vec2 center=sun.xy*size;
    float radius=max(1.0,sun.z*size.y)*.5;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 at=clamp(center+vec2(float(x),float(y))*radius,vec2(.5),size-vec2(.5));
        vec3 hot=texture2DRect(sceneTexture,at).rgb;
        result+=step(.9995,texture2DRect(depthTexture,at).r)*smoothstep(.4,.85,max(hot.r,max(hot.g,hot.b)));
    }
    return result/9.0;
}
// Sliding drop heads and thin trails, inspired by the supplied rain-on-glass
// shader. One surfacing event drains away; no ongoing rain or scene-wide blur.
float dropLayer(vec2 p,float age,float seed){
    vec2 grid=vec2(7.5,2.1);
    vec2 moving=p*grid+vec2(0.0,age*1.9);
    float column=floor(moving.x);
    moving.y+=hash(column+seed)*2.0;
    vec2 cell=floor(moving),st=fract(moving)-.5;
    float n=hash(cell.x*37.0+cell.y*113.0+seed);
    float x=(hash(n*89.0)-.5)*.5;
    x+=sin(p.y*17.0+n*9.0)*.045;
    vec2 local=vec2((st.x-x)*1.9,st.y+.24);
    float head=1.0-smoothstep(.05,.28,length(local));
    float trail=(1.0-smoothstep(.015,.06,abs(st.x-x)))*smoothstep(-.25,-.1,st.y)*(1.0-smoothstep(.05,.5,st.y));
    return max(head,trail*.25)*(.5+.5*n);
}
vec2 visorSurface(vec2 p){
    // Wrap the glass around the horizontal axis of the view. Preserve all
    // texture borders so the peripheral wear never clamps into long streaks.
    float aspect=size.x/size.y;
    float nx=clamp(p.x/(aspect*.5),-1.0,1.0);
    float wrappedX=asin(nx*.985)/asin(.985);
    return vec2(wrappedX*aspect*.5,p.y+.30*nx*nx*cos(3.141593*p.y));
}
float waterHeight(vec2 screenPoint,float age){
    vec2 p=visorSurface(screenPoint);
    p.x/=1.0+.32*(.5-screenPoint.y);
    float clearFront=.65-age*.58;
    float drain=1.0-smoothstep(clearFront-.15,clearFront+.15,p.y);
    float drops=dropLayer(p,age,effects.z*17.0)+.45*dropLayer(p*1.7,age*1.13,effects.z*17.0+71.0);
    // Thin continuously with elapsed runoff time and toward the visor bottom.
    float fade=(1.0-smoothstep(.15,2.4,age))*mix(.35,1.0,smoothstep(-.6,.25,p.y));
    return drops*drain*fade;
}
float brightness(vec3 c){return max(c.r,max(c.g,c.b));}
void main(){
    vec2 p=uv*size;
    vec2 q=(p-size*.5)/size.y;
    vec2 refractOffset=vec2(0.0);float rim=0.0;
    if(effects.x<2.4){
        float age=max(effects.x,0.0);
        float e=.0015;
        float h=waterHeight(q,age);
        vec2 gradient=vec2(waterHeight(q+vec2(e,0.0),age)-waterHeight(q-vec2(e,0.0),age),
                           waterHeight(q+vec2(0.0,e),age)-waterHeight(q-vec2(0.0,e),age))/(2.0*e);
        refractOffset=clamp(gradient*size.y*.0012*(1.0+.35*dot(q,q)),vec2(-size.y*.018),vec2(size.y*.018));
        rim=clamp(dot(gradient,vec2(-.4,.7))*.004,-.025,.045)*h;
    }
    vec4 scene=texture2DRect(sceneTexture,clamp(p+refractOffset*effects.y,vec2(.5),size-vec2(.5)));
    vec3 color=scene.rgb+vec3(rim*effects.y);
    vec2 glassUV=visorSurface(q)/vec2(size.x/size.y,1.0)+.5;
    vec3 wear=texture2D(scratchTexture,glassUV).rgb;
    if(max(wear.r,max(wear.g,wear.b))>.005){
        float illumination=0.0;
        if(sun.w>0.0){
            vec2 sunPoint=(sun.xy*size-size*.5)/size.y;
            illumination=exp(-length(q-sunPoint)*3.0)*sourceVisibility()*sun.w;
        }
        // Reveal nearby wear around visible highlights, not only when a
        // scratch lands directly on an almost-white pixel. Works without sun.
        float localLight=smoothstep(.40,.85,brightness(scene.rgb));
        vec2 reach=vec2(size.y*.025);
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            if(x==0 && y==0)continue;
            vec2 at=clamp(p+vec2(float(x),float(y))*reach,vec2(.5),size-vec2(.5));
            float highlight=smoothstep(.40,.85,brightness(texture2DRect(sceneTexture,at).rgb));
            localLight=max(localLight,highlight*.65);
        }
        illumination=max(illumination,localLight*.65);
        vec3 lighting=vec3(1.0,.94,.80)*illumination;
        for(int i=0;i<4;++i){
            if(i>=lightCount)break;
            float visible=step(lights[i].z,texture2DRect(depthTexture,lights[i].xy*size).r);
            vec2 lightPoint=(lights[i].xy*size-size*.5)/size.y;
            lighting+=lightColors[i].rgb*visible*exp(-length(q-lightPoint)*3.0)*1.8;
        }
        color+=effects.y*min(lighting,vec3(1.5))*(wear.r*.43+wear.g*.22+wear.b*.14);
    }
    gl_FragColor=vec4(color,scene.a);
}
)VISOR";}
inline GLhandleARB program(){
    static SDL_GLContext context=nullptr;static GLhandleARB value=0;static bool attempted=false;
    if(context!=SDL_GL_GetCurrentContext()){context=SDL_GL_GetCurrentContext();value=0;attempted=false;}
    if(attempted)return value;
    attempted=true;
    const char* vertex="#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}";
    const auto v=SprintathonFlashlightShadow::compile(GL_VERTEX_SHADER_ARB,vertex);
    const auto f=SprintathonFlashlightShadow::compile(GL_FRAGMENT_SHADER_ARB,fragment());
    if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return 0;}
    value=glCreateProgramObjectARB();glAttachObjectARB(value,v);glAttachObjectARB(value,f);glLinkProgramARB(value);
    glDeleteObjectARB(v);glDeleteObjectARB(f);GLint ok=0;glGetObjectParameterivARB(value,GL_OBJECT_LINK_STATUS_ARB,&ok);
    if(!ok){std::fprintf(stderr,"Visor shader link failed\n");glDeleteObjectARB(value);value=0;}return value;
}
// Cache three independent wear masks: scratches, dust and soft wipe marks.
// No image files or per-frame procedural pattern generation.
inline std::vector<unsigned char> makeScratchMask(int width,int height){
    std::vector<unsigned char> pixels(width*height*3,0);
    unsigned seed=0x51a7e9u;
    auto random=[&](){seed=1664525u*seed+1013904223u;return float(seed>>8)/16777216.0f;};
    auto stamp=[&](float cx,float cy,float rx,float ry,float angle,int channel,float opacity){
        float radius=std::max(rx,ry)+1,co=std::cos(angle),si=std::sin(angle);
        int x0=std::max(0,int(cx-radius)),x1=std::min(width-1,int(cx+radius));
        int y0=std::max(0,int(cy-radius)),y1=std::min(height-1,int(cy+radius));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            float dx=x+.5f-cx,dy=y+.5f-cy;
            float u=(dx*co+dy*si)/rx,v=(-dx*si+dy*co)/ry;
            float r=u*u+v*v;if(r>=1)continue;
            float edge=std::max(std::abs(float(x)/width-.5f),std::abs(float(y)/height-.5f));
            float clear=std::max(0.0f,std::min(1.0f,(edge-.12f)/.18f));
            float coverage=(1-r)*(1-r)*opacity*clear;
            auto& value=pixels[(y*width+x)*3+channel];
            value=std::max(value,static_cast<unsigned char>(std::min(1.0f,coverage)*255));
        }
    };
    for(int i=0;i<96;++i){
        float x=random()*width,y=random()*height,a=random()*6.283185f;
        float l=(.004f+.14f*std::pow(random(),2.0f))*height;
        float thick=(.45f+1.3f*std::pow(random(),3.0f))*height/1024;
        float bend=(random()-.5f)*1.8f,opacity=.35f+.65f*random();
        int steps=std::max(2,int(l*2));
        for(int j=0;j<=steps;++j){float t=float(j)/steps;
            float side=std::sin(t*3.141593f)*l*bend*.18f;
            stamp(x+std::cos(a)*l*t-std::sin(a)*side,y+std::sin(a)*l*t+std::cos(a)*side,
                thick+0.6f,thick+0.6f,0,0,opacity*(.4f+.6f*std::sin(t*3.141593f)));
        }
    }
    for(int i=0;i<700;++i){
        float x=random()*width,y=random()*height,r=(.6f+1.8f*random())*height/1024;
        stamp(x,y,r,r,0,1,.3f+.7f*random());
    }
    for(int i=0;i<22;++i){
        float x=random()*width,y=random()*height,rx=(.025f+.06f*random())*height;
        float ry=(.008f+.018f*random())*height,a=random()*6.283185f;
        stamp(x,y,rx,ry,a,2,.25f+.5f*random());
    }
    return pixels;
}
inline GLuint scratchTexture(){
    static SDL_GLContext context=nullptr;static GLuint texture=0;
    if(context!=SDL_GL_GetCurrentContext()){context=SDL_GL_GetCurrentContext();texture=0;}
    if(texture)return texture;
    // Three MiB of cached RGB masks; no external artwork dependency.
    const int width=1024,height=1024;
    const auto pixels=makeScratchMask(width,height);
    GLint binding=0,buffer=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&buffer);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    glBindTexture(GL_TEXTURE_2D,GLuint(binding));glPopClientAttrib();glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,GLuint(buffer));
    return texture;
}
inline void draw(GLuint color,GLuint depth,int width,int height){
    if(!state().active() || !color || !depth || width<=0 || height<=0)return;
    const auto shader=program();if(!shader)return;
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);const auto previous=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_FOG);
    for(int i=0;i<6;++i)glDisable(GL_CLIP_PLANE0+i);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glUseProgramObjectARB(shader);
    auto loc=[&](const char* name){return glGetUniformLocationARB(shader,name);};
    glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,color);glUniform1iARB(loc("sceneTexture"),0);
    glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,depth);glUniform1iARB(loc("depthTexture"),1);
    glActiveTextureARB(GL_TEXTURE2_ARB);glBindTexture(GL_TEXTURE_2D,scratchTexture());glUniform1iARB(loc("scratchTexture"),2);
    glUniform2fARB(loc("size"),float(width),float(height));
    glUniform4fARB(loc("effects"),state().wetAge,strength(),float(state().splash),0);
    glUniform4fvARB(loc("sun"),1,state().sun);
    glUniform1iARB(loc("lightCount"),state().lightCount);
    glUniform4fvARB(loc("lights[0]"),4,state().visorLightPositions);
    glUniform4fvARB(loc("lightColors[0]"),4,state().lightColors);
    glBegin(GL_QUADS);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,0,0);glVertex2f(-1,-1);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,1,0);glVertex2f(1,-1);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,1,1);glVertex2f(1,1);
    glMultiTexCoord2fARB(GL_TEXTURE0_ARB,0,1);glVertex2f(-1,1);
    glEnd();glUseProgramObjectARB(previous);glPopAttrib();glActiveTextureARB(active);
}
}
#endif
