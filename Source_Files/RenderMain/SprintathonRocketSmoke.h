#ifndef SPRINTATHON_ROCKET_SMOKE_H
#define SPRINTATHON_ROCKET_SMOKE_H
#include "SprintathonFlashlightShadow.h"
#include "SprintathonExplosionEvents.h"
#include <cmath>
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
uniform float splashMode;
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
 if(splashMode<1.5 && radial>=1.0)discard;
 if(splashMode>.5){
  float raw=texture2DRect(sceneDepth,gl_FragCoord.xy).r;
  float sceneZ=abs(depthProjection.y/(2.0*raw-1.0+depthProjection.x));
  // Surface-aligned foam and spray sheets use their true interpolated depth.
  float intersection=clamp((sceneZ-eyeDepth)/max(puff.x*.08,1.0),0.0,1.0);
  float body=0.0;
  if(splashMode>2.5){
   float r=length(q);
   float grain=noise(q*18.0+vec2(puff.z));
   float ring=abs(r-(.78+.025*sin(atan(q.y,q.x)*9.0+puff.z)));
   body=(1.0-smoothstep(.025,.14,ring))*smoothstep(.23,.62,grain)*.7;
   body+=.12*(1.0-smoothstep(.0,.72,r))*grain;
  }else if(splashMode>1.5){
   // Break up the sheet in both directions instead of stretching noise
   // into continuous vertical fibres. Coarse foam contains finer spray holes.
   float y=uv.y;
   vec2 flow=vec2(q.x*4.5,y*13.0-min(puff.y,.28)*8.0)+vec2(puff.z);
   vec2 warp=vec2(noise(flow*.48),noise(flow*.48+17.0))-.5;
   flow+=warp*1.8;
   float coarse=noise(flow);
   float fine=noise(flow*3.1+vec2(coarse));
   float micro=noise(flow*7.7);
   float width=.50+.22*sin(y*3.14159)-.22*y;
   float centre=(noise(vec2(y*6.0-puff.y*2.0,puff.z))-.5)*.24*y;
   float torn=(coarse-.5)*.52+(fine-.5)*.20;
   // Bullet plumes flare outward from a narrow impact point.
   float taper=splashMode>2.1 ? mix(.16,1.0,smoothstep(0.0,.55,y)) : 1.0;
   float edge=1.0-smoothstep(width-.07,width+.09,abs(q.x-centre)/taper+torn);
   float tip=1.0-smoothstep(.70,.98,y+(coarse-.5)*.23);
   float holes=smoothstep(.29+y*.10,.65,coarse*.40+fine*.42+micro*.18);
   // A brief dense impact core breaks up into the existing spray.
   float impact=(1.0-smoothstep(.04,.18,puff.y))*(1.0-smoothstep(.08,.32,y));
   body=edge*tip*mix(holes,.95,impact)*(.85+micro*.15);
   body*=smoothstep(smokeStyle.x,smokeStyle.x+.035,y);
  }else{
   float grain=noise(q*vec2(9.0,3.0)+vec2(puff.z));
   body=(1.0-smoothstep(.18,1.0,radial+(grain-.5)*.16))*mix(.5,1.0,grain);
  }
  gl_FragColor=vec4(vec3(.90,.96,1.0),body*intersection*puff.w);
  return;
 }
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
 if(!depth || width<=0 || height<=0 || (!SprintathonExplosionEvents::enabled() && !SprintathonExplosionEvents::splashesEnabled()))return;
 const auto shader=program();if(!shader)return;
 struct Puff{float x,y,z,r,age,seed,alpha,lifetime,brightness,splash;float right[3],up[3];};std::vector<Puff> puffs;
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
      float((b.start*17+unsigned(b.object)*13)%997),b.rocket?.62f:.30f,b.rocket?2.0f:1.5f,b.rocket?1.25f:1.0f,0.0f,{radius,0,0},{0,radius,0}});
 }
 // Large connected spray sheets, ballistic streaks and water-aligned foam.
 for(const auto& b:SprintathonExplosionEvents::state().splashes){
  if(now<b.start || now-b.start>=(b.bullet?15:54))continue;
  const float age=float(now-b.start)/TICKS_PER_SECOND;
  world_point3d from=camera->origin,to=b.position;
  if(line_is_obstructed(camera->origin_polygon_index,&from,b.polygon,&to))continue;
  const float size=WORLD_ONE*(b.bullet?.60f:(b.rocket?1.60f:1.10f));
  const float motionAge=age*(b.bullet?3.6f:1.0f);
  const float fade=std::max(0.0f,1.0f-motionAge/1.8f);
  const float seed=float((b.start*17+unsigned(b.polygon)*13+unsigned(b.position.x)*31+unsigned(b.position.y)*73)%997);
  auto add=[&](float x,float y,float z,float width,float height,float alpha,float seed,float mode,
               float vx,float vy,float vz){
   const float delta[3]={float(b.position.x-camera->origin.x)+x,
       float(b.position.y-camera->origin.y)+y,float(b.position.z-camera->origin.z)+z};
   float eye[3]={},axis[3]={};
   const float velocity[3]={vx,vy,vz};
   for(int r=0;r<3;++r)for(int c=0;c<3;++c){
    eye[r]+=modelview[c*4+r]*delta[c];axis[r]+=modelview[c*4+r]*velocity[c];
   }
   if(-eye[2]<=1.0f)return;
   Puff p={eye[0],eye[1],eye[2],width,age,seed,alpha,1.8f,1.0f,mode,{0,0,0},{0,0,0}};
   if(mode>2.5f){
    for(int r=0;r<3;++r){p.right[r]=modelview[r]*width;p.up[r]=modelview[4+r]*height;}
   }else if(mode>1.5f){
    // Hide the submerged portion without rescaling its texture coordinates.
    p.lifetime=std::max(0.0f,(height-z)/(2.0f*height));
    // Cylindrical billboard: keep the vertical axis in world space.
    const float norm=std::sqrt(modelview[8]*modelview[8]+modelview[9]*modelview[9]);
    p.right[0]=norm>.001f?modelview[9]/norm*width:width;
    p.right[1]=norm>.001f?-modelview[8]/norm*width:0;
    for(int r=0;r<3;++r)p.up[r]=(b.bullet?axis[r]:modelview[8+r])*height;
    if(b.bullet){
     const float norm=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]);
     p.right[0]=norm>.001f?axis[1]/norm*width:width;
     p.right[1]=norm>.001f?-axis[0]/norm*width:0;
    }
   }else{
    const float norm=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]);
    const float dx=norm>.001f?axis[0]/norm:0,dy=norm>.001f?axis[1]/norm:1;
    p.right[0]=dy*width;p.right[1]=-dx*width;
    p.up[0]=dx*height;p.up[1]=dy*height;
   }
   puffs.push_back(p);
  };
  if(b.bullet){
   // Two overlapping foam sheets form a full, irregular directional plume.
   const float variation=std::sin(seed*1.37f)*.5f+.5f;
   const float forward=1.2f+4.8f*(1.0f-b.steepness);
   const float lift=3.2f+b.steepness*1.8f;
   const float spread=.35f+variation*.55f;
   const float jetAge=.17f*(1.0f-std::exp(-age*20.0f));
   const float jetHeight=size*lift*jetAge;
   const float jetReach=size*forward*jetAge;
   const float jetAlpha=std::max(0.0f,1.0f-age/.34f);
   if(jetHeight>0 && jetAlpha>0){
    for(int k=0;k<2;++k){
     const float scale=k==0?1.0f:.78f;
     const float side=k==0?0.0f:(variation-.5f)*size*.20f;
     add(b.dx*jetReach*.5f*scale-b.dy*side,
         b.dy*jetReach*.5f*scale+b.dx*side,jetHeight*.5f*scale,
         size*(.32f+variation*.12f)*scale,jetHeight*.5f*scale,
         jetAlpha*(k==0?1.25f:.95f),seed+k*37,2.25f,
         b.dx*forward/lift-b.dy*side/(jetHeight*.5f*scale),
         b.dy*forward/lift+b.dx*side/(jetHeight*.5f*scale),1);
    }
   }
   for(int k=0;k<4;++k){
    const float jitter=std::sin(seed+k*23.71f)*.5f+.5f;
    const float side=(std::sin(seed*2.17f+k*41.3f))*spread;
    const float speed=forward*(.55f+jitter*.9f);
    const float vx=b.dx*speed-b.dy*side,vy=b.dy*speed+b.dx*side;
    const float vz=lift*(.60f+jitter*.55f);
    const float travel=.17f*(1.0f-std::exp(-age*20.0f));
    const float travelSpeed=3.4f*std::exp(-age*20.0f);
    const float verticalSpeed=vz*travelSpeed-18.0f*age;
    const float z=size*(vz*travel-9.0f*age*age);
    if(z<0)continue;
    const float radius=size*(.009f+jitter*.011f);
    const float projectedSpeed=std::sqrt((vx*vx+vy*vy)*travelSpeed*travelSpeed+verticalSpeed*verticalSpeed);
    add(size*vx*travel,size*vy*travel,z+radius,radius,
        radius*(1.0f+std::min(4.0f,projectedSpeed*.35f)),fade,
        seed+k*11,1.0f,vx*travelSpeed,vy*travelSpeed,verticalSpeed);
   }
   const float ringRadius=size*(.18f+age*2.5f);
   add(0,0,size*.018f,ringRadius,ringRadius,fade*1.0f,seed,3.0f,0,0,0);
   continue;
  }
  // Grow only during launch. Then translate the intact sheet downward
  // under gravity, letting its submerged portion disappear at the surface.
  const float rise=1.0f-std::exp(-std::min(motionAge,.18f)*22.0f);
  const float collapse=std::max(0.0f,1.0f-std::max(0.0f,motionAge-.18f)/1.0f);
  const float plumeHeight=size*(.15f+1.9f*rise);
  const float fallTime=std::max(0.0f,motionAge-.18f);
  const float fall=size*2.8f*fallTime*fallTime;
  if(fall<plumeHeight && collapse>0){
   for(int k=0;k<(b.bullet?1:3);++k){
    const float angle=seed+k*2.3999632f;
    const float h=plumeHeight*(1.0f-k*.12f);
    const float w=size*(.56f+age*.28f)*(1.0f-k*.10f);
    add(std::cos(angle)*size*.08f,std::sin(angle)*size*.08f,h*.5f+size*.015f-fall,
        w,h*.5f,collapse*.95f,seed+k*31,2.0f,0,0,1);
   }
  }
  // Droplets share the sheet launch and fall, rather than trailing its rise.
  const float launchVelocity=motionAge<.18f?41.8f*std::exp(-motionAge*22.0f):0.0f;
  for(int k=0;k<(b.bullet?4:56);++k){
   const float jitter=std::sin(seed+float(k)*23.71f)*.5f+.5f;
   const float angle=k*2.3999632f+seed;
   const float speed=.35f+jitter*.85f;
   const float heightScale=.65f+jitter*.45f;
   const float vz=launchVelocity*heightScale-5.6f*fallTime;
   const float z=size*1.9f*rise*heightScale-fall;
   if(z<0)continue;
   const float reach=size*(.035f+motionAge*speed);
   const float radius=size*(b.bullet?.028f:(.004f+jitter*.006f));
   add(std::cos(angle)*reach,std::sin(angle)*reach,z+radius,
       radius,radius*(1.0f+std::min(4.0f,.35f*std::sqrt(speed*speed+vz*vz))),fade,seed+k*11,1.0f,
       std::cos(angle)*speed,std::sin(angle)*speed,vz);
  }
  const float ringRadius=size*(.24f+motionAge*1.35f);
  add(0,0,size*.018f,ringRadius,ringRadius,fade*1.5f,seed,3.0f,0,0,0);
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
 const GLint puffLocation=loc("puff"),styleLocation=loc("smokeStyle"),splashLocation=loc("splashMode");
 for(const auto& p:puffs){
  glUniform1fARB(splashLocation,p.splash);
  glUniform2fARB(styleLocation,p.lifetime,p.brightness);
  glUniform4fARB(puffLocation,p.r,p.age,p.seed,p.alpha);
  glBegin(GL_QUADS);
  const float corners[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
  for(const auto& c:corners){
   glTexCoord2f((c[0]+1)*.5f,(c[1]+1)*.5f);
   glVertex3f(p.x+c[0]*p.right[0]+c[1]*p.up[0],
       p.y+c[0]*p.right[1]+c[1]*p.up[1],p.z+c[0]*p.right[2]+c[1]*p.up[2]);
  }
  glEnd();
 }
 glUseProgramObjectARB(previous);glPopAttrib();glActiveTextureARB(active);
}
}
#endif
