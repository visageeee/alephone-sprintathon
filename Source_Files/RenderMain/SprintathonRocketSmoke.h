#ifndef SPRINTATHON_ROCKET_SMOKE_H
#define SPRINTATHON_ROCKET_SMOKE_H
#include "SprintathonFlashlightShadow.h"
#include "SprintathonExplosionEvents.h"
namespace SprintathonRocketSmoke {
inline GLhandleARB program(){
 static SDL_GLContext context=nullptr;static GLhandleARB shader=0;static bool tried=false;
 if(context!=SDL_GL_GetCurrentContext()){context=SDL_GL_GetCurrentContext();shader=0;tried=false;}
 if(tried)return shader;tried=true;
 const char* vertex=R"FX(#version 120
uniform vec4 viewProjection;
uniform vec2 depthProjection;
varying vec2 uv;
varying float eyeDepth;
void main(){
 vec3 p=gl_Vertex.xyz;
 gl_Position=vec4(viewProjection.xy*p.xy-viewProjection.zw*p.z,
     depthProjection.x*p.z+depthProjection.y,-p.z);
 uv=gl_MultiTexCoord0.xy;eyeDepth=-p.z;
}
)FX";
 const char* fragment=R"FX(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect sceneDepth;
uniform vec2 depthProjection;
uniform vec2 smokeStyle; // lifetime in seconds, brightness
uniform vec4 puff; // radius, age, stable seed, opacity
varying vec2 uv;
varying float eyeDepth;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){
 vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),
     mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
void main(){
 vec2 q=uv*2.0-1.0;
 float radial=dot(q,q);
 if(radial>=1.0)discard;
 float age=puff.y;
 float angle=age*.55+puff.z;
 float c=cos(angle),s=sin(angle);
 vec2 p=mat2(c,-s,s,c)*q;
 float coarse=noise(p*3.6+vec2(puff.z,-age*.45));
 float fine=noise(p*8.7+vec2(coarse*1.3,puff.z+age*.2));
 float density=smoothstep(.18,.72,coarse*.7+fine*.3);
 float edge=1.0-smoothstep(.25,1.0,radial+(coarse-.5)*.28);
 float raw=texture2DRect(sceneDepth,gl_FragCoord.xy).r;
 float sceneZ=abs(depthProjection.y/(2.0*raw-1.0+depthProjection.x));
 float front=eyeDepth-sqrt(max(0.0,1.0-radial))*puff.x*.45;
 float intersection=clamp((sceneZ-front)/max(puff.x*.6,1.0),0.0,1.0);
 float life=(1.0-smoothstep(.4,smokeStyle.x,age))*smoothstep(0.0,.04,age);
 float alpha=density*edge*intersection*life*puff.w;
 vec3 color=mix(vec3(.055,.052,.048),vec3(.24,.23,.21),fine*.65+coarse*.35);
 gl_FragColor=vec4(color*smokeStyle.y,alpha);
}
)FX";
 const auto v=SprintathonFlashlightShadow::compile(GL_VERTEX_SHADER_ARB,vertex);
 const auto f=SprintathonFlashlightShadow::compile(GL_FRAGMENT_SHADER_ARB,fragment);
 if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return 0;}
 shader=glCreateProgramObjectARB();glAttachObjectARB(shader,v);glAttachObjectARB(shader,f);glLinkProgramARB(shader);
 glDeleteObjectARB(v);glDeleteObjectARB(f);GLint ok=0;glGetObjectParameterivARB(shader,GL_OBJECT_LINK_STATUS_ARB,&ok);
 if(!ok){glDeleteObjectARB(shader);shader=0;std::fprintf(stderr,"Rocket smoke shader failed; retaining original trails\n");}
 return shader;
}
inline void draw(const view_data* camera,GLuint depth,int width,int height,
    const GLfloat* projection,const GLfloat* modelview){
 if(!depth || width<=0 || height<=0 || !SprintathonExplosionEvents::enabled())return;
 const auto shader=program();if(!shader)return;
 struct Puff{float x,y,z,r,age,seed,alpha,lifetime,brightness;};std::vector<Puff> puffs;
 const auto now=sprintathon_projectile_render_tick();
 for(const auto& b:SprintathonExplosionEvents::state().trails){
  if(now<b.start || now-b.start>=(b.rocket?60:45))continue;
  const float age=float(now-b.start)/TICKS_PER_SECOND;
  world_point3d from=camera->origin,to=b.position;
  if(line_is_obstructed(camera->origin_polygon_index,&from,b.polygon,&to))continue;
  const float delta[3]={float(b.position.x-camera->origin.x),float(b.position.y-camera->origin.y),
      float(b.position.z-camera->origin.z)+age*WORLD_ONE*.07f};
  float eye[3]={};for(int r=0;r<3;++r)for(int c=0;c<3;++c)eye[r]+=modelview[c*4+r]*delta[c];
  if(-eye[2]<=1.0f)continue;
  const float radius=WORLD_ONE*(b.rocket?.10f:.0525f)*(1.0f+age*2.0f);
  puffs.push_back({eye[0],eye[1],eye[2],radius,age,
      float((b.start*17+unsigned(b.object)*13)%997),b.rocket?.62f:.30f,b.rocket?2.0f:1.5f,b.rocket?1.25f:1.0f});
 }
 if(puffs.empty())return;
 std::sort(puffs.begin(),puffs.end(),[](const Puff& a,const Puff& b){return a.z<b.z;});
 GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
 const auto previous=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
 glPushAttrib(GL_ALL_ATTRIB_BITS);
 glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);
 glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_FOG);
 glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
 glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glUseProgramObjectARB(shader);
 auto loc=[&](const char* name){return glGetUniformLocationARB(shader,name);};
 glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,depth);
 glUniform1iARB(loc("sceneDepth"),0);
 glUniform2fARB(loc("depthProjection"),projection[10],projection[14]);
 glUniform4fARB(loc("viewProjection"),projection[0],projection[5],projection[8],projection[9]);
 const GLint puffLocation=loc("puff"),styleLocation=loc("smokeStyle");
 for(const auto& p:puffs){
  glUniform2fARB(styleLocation,p.lifetime,p.brightness);
  glUniform4fARB(puffLocation,p.r,p.age,p.seed,p.alpha);
  glBegin(GL_QUADS);
  glTexCoord2f(0,0);glVertex3f(p.x-p.r,p.y-p.r,p.z);
  glTexCoord2f(1,0);glVertex3f(p.x+p.r,p.y-p.r,p.z);
  glTexCoord2f(1,1);glVertex3f(p.x+p.r,p.y+p.r,p.z);
  glTexCoord2f(0,1);glVertex3f(p.x-p.r,p.y+p.r,p.z);
  glEnd();
 }
 glUseProgramObjectARB(previous);glPopAttrib();glActiveTextureARB(active);
}
}
#endif
