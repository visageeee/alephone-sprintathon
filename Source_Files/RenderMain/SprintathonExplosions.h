#ifndef SPRINTATHON_EXPLOSIONS_H
#define SPRINTATHON_EXPLOSIONS_H
#include "SprintathonFlashlightShadow.h"
#include "SprintathonExplosionEvents.h"
#include "SprintathonRocketSmoke.h"
namespace SprintathonExplosions {
inline GLhandleARB program(){
 static SDL_GLContext context=nullptr;static GLhandleARB shader=0;static bool tried=false;
 if(context!=SDL_GL_GetCurrentContext()){context=SDL_GL_GetCurrentContext();shader=0;tried=false;}
 if(tried)return shader;tried=true;
 const char* vertex=R"FX(#version 120
void main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}
)FX";
 const char* fragment=R"FX(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect sceneColor,sceneDepth;
uniform vec2 imageSize,depthProjection;
uniform vec4 viewProjection;
uniform vec4 centres[6],parameters[6];
uniform vec3 worldX,worldY,worldUp;
uniform int count;
uniform bool emissionOnly;
float hash(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float noise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
 mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);}
void main(){
 vec2 pixel=gl_TexCoord[0].xy*imageSize;
 float raw=texture2DRect(sceneDepth,pixel).r;
 float sceneZ=abs(depthProjection.y/(2.0*raw-1.0+depthProjection.x));
 vec3 ray=normalize(vec3((pixel/imageSize*2.0-1.0+viewProjection.zw)/viewProjection.xy,-1.0));
 float stop=sceneZ/max(-ray.z,.001);
 vec3 color=vec3(0.0);
 if(!emissionOnly)color=texture2DRect(sceneColor,pixel).rgb;
 for(int b=0;b<6;++b){
  if(b>=count)break;
  vec3 centre=centres[b].xyz;float radius=centres[b].w,age=parameters[b].x;
  // A handful of ballistic embers, evaluated once per pixel, not per march step.
  vec3 sparks=vec3(0.0);
  if(age>.025 && age<.85){
   float growth=.12+1.32*(1.0-exp(-age*5.0))+.08*min(age,1.2);
   float scale=radius/growth;
   for(int k=0;k<14;++k){
    float seed=parameters[b].y+float(k)*19.7;
    vec3 direction=normalize(vec3(hash(vec3(seed,1,2))-.5,
        hash(vec3(seed,3,4))-.5,hash(vec3(seed,5,6))-.3));
    vec3 velocity=(worldX*direction.x+worldY*direction.y+worldUp*direction.z)*
        scale*(1.8+hash(vec3(seed))*1.6);
    vec3 head=centre+velocity*age-worldUp*scale*age*age*.8;
    vec3 tail=head-(velocity-worldUp*scale*age*1.6)*.018;
    vec3 segment=head-tail;
    vec3 perpendicular=segment-ray*dot(segment,ray);
    float u=clamp(-dot(tail-ray*dot(tail,ray),perpendicular)/
        max(dot(perpendicular,perpendicular),.0001),0.0,1.0);
    vec3 point=tail+segment*u;
    float distanceAlong=dot(point,ray);
    if(distanceAlong>0.0 && distanceAlong<stop){
     float width=max(scale*.0035,distanceAlong/imageSize.y*.7);
     float d=length(point-ray*distanceAlong)/width;
     sparks+=vec3(7.0,3.8,1.0)*exp(-d*d*1.6)*
         (1.0-smoothstep(.35,.85,age));
    }
   }
  }
  float along=dot(centre,ray),side=dot(centre,centre)-along*along;
  // Initial ignition flash: a compact luminous volume, independent of the
  // fire parcels and clipped against the same scene depth as the explosion.
  if(age<.10){
   float growth=.12+1.32*(1.0-exp(-age*5.0))+.08*min(age,1.2);
   float flashRadius=radius/growth*.32;
   float chordSquared=flashRadius*flashRadius-max(side,0.0);
   if(chordSquared>0.0){
    float halfChord=sqrt(chordSquared);
    float front=max(0.0,along-halfChord);
    float back=min(stop,along+halfChord);
    float thickness=max(0.0,back-front)/flashRadius;
    float pulse=1.0-smoothstep(.015,.10,age);
    float edge=1.0-smoothstep(.0,1.0,max(side,0.0)/(flashRadius*flashRadius));
    sparks+=vec3(16.0,14.0,10.0)*pulse*edge*(1.0-exp(-thickness*3.0));
   }
  }
  if(side>=radius*radius){color+=sparks;continue;}
  float halfLength=sqrt(max(0.0,radius*radius-side));
  float begin=max(0.0,along-halfLength),end=min(stop,along+halfLength);
  if(end<=begin){color+=sparks;continue;}
  // Five independently travelling, stretched parcels of burning gas.
  // Compute their trajectories once, outside the integration loop.
  vec3 parcelCentre[5],parcelAxis[5];
  float parcelRadius[5];
  for(int k=0;k<5;++k){
   float seed=parameters[b].y+float(k)*31.73;
   vec3 axis=normalize(vec3(hash(vec3(seed,11,4))-.5,
       hash(vec3(seed,7,13))-.5,hash(vec3(seed,17,2))-.4));
   float speed=mix(.34,.52,hash(vec3(seed,8,19)));
   float travel=speed*(1.0-exp(-age*(3.0+float(k)*.45)));
   parcelCentre[k]=axis*travel;
   parcelAxis[k]=axis;
   parcelRadius[k]=mix(.33,.43,hash(vec3(seed,2,23)))*
       (1.0-.32*smoothstep(.18,.75,age));
  }
  float stepSize=(end-begin)/28.0;
  vec3 accumulated=vec3(0.0);float opacity=0.0;
  // A finite blast: folded burning layers, followed by a faint residue.
  float burn=1.0-smoothstep(.28,.78,age);
  float residue=smoothstep(.25,.65,age)*(1.0-smoothstep(.85,1.8,age));
  float jitter=hash(vec3(floor(pixel),parameters[b].y));
  for(int j=0;j<28;++j){
   vec3 local=(ray*(begin+(float(j)+.45+.1*jitter)*stepSize)-centre)/radius;
   // Fix the turbulence to world axes so it does not turn with the camera.
   vec3 p=vec3(dot(local,worldX),dot(local,worldY),dot(local,worldUp));
   float radial=length(p);
   // Differential rotation and a rolling cross-section deform the gas,
   // instead of translating an almost unchanged noise pattern upward.
   vec3 advected=p;
   float twist=age*(1.8+1.4*p.z)+.35*sin(p.z*5.0-age*3.0);
   float cs=cos(twist),sn=sin(twist);
   advected.xy=mat2(cs,-sn,sn,cs)*advected.xy;
   float roll=age*1.1+advected.x*1.8;
   cs=cos(roll);sn=sin(roll);
   advected.yz=mat2(cs,-sn,sn,cs)*advected.yz;
   advected+=.10*sin(advected.yzx*6.0+vec3(age*2.0,-age*1.7,age*1.3));
   vec3 flow=advected*4.8+vec3(parameters[b].y)+vec3(0.0,0.0,-age*.8);
   float coarse=noise(flow);
   float detail=noise(flow*2.47+coarse*1.6);
   float ridge=1.0-abs(2.0*detail-1.0);
   float boundary=mix(.40,.64,smoothstep(0.0,.55,age))+(coarse-.5)*.44+(detail-.5)*.12;
   float thickness=max(.055,stepSize/radius*.65);
   float sheet=1.0-smoothstep(thickness,thickness*2.0,abs(radial-boundary));
   float breakup=mix(1.0,smoothstep(.25,.65,coarse),smoothstep(.12,.65,age));
   float filaments=sheet*mix(.12,1.0,smoothstep(.45,.9,ridge))*breakup;
   float envelope=1.0-smoothstep(.82,1.0,radial);
   // Smoke keeps its rolling domain. Fire uses independent expanding cells,
   // with small-scale boiling rather than coherent spiral rotation.
   float smokeDensity=(sheet*.25+
       smoothstep(.34,.64,coarse)*(1.0-smoothstep(boundary-.12,boundary+.10,radial))*.7)*
       (residue+.35*burn)*envelope;
   float fireDensity=0.0,hotPatch=0.0,hot=0.0,soot=0.0;
   if(burn>0.0){
    float shape=100.0;
    vec3 material=vec3(0.0);
    // Union of asymmetrical moving volumes, rather than a spherical shell.
    for(int k=0;k<5;++k){
     vec3 q=p-parcelCentre[k];
     float axial=dot(q,parcelAxis[k]);
     q-=parcelAxis[k]*axial*.10;
     q/=parcelRadius[k];
     float distanceToParcel=length(q);
     if(distanceToParcel<shape){shape=distanceToParcel;material=q+float(k)*13.7;}
    }
    // Detail travels with each parcel. Slow internal evolution only.
    material=mat3(.36,.48,-.8,-.8,.60,0.0,.48,.64,.60)*material;
    vec3 domain=material*2.6+vec3(parameters[b].y);
    float cells=noise(domain);
    float fine=noise(domain*2.71+vec3(age*.7,-age*.4,age*.3));
    float ridges=1.0-abs(2.0*fine-1.0);
    float displaced=shape+(cells-.5)*.85+(ridges-.5)*.28;
    float body=1.0-smoothstep(.73,1.12,displaced);
    // Cavities grow through the volume as it disperses.
    float holes=mix(1.0,smoothstep(.28,.58,cells),
        smoothstep(.12,.65,age));
    hotPatch=smoothstep(.43,.78,cells*.55+ridges*.45);
    fireDensity=body*holes*burn*envelope;
    soot=body*burn*envelope*(1.0-hotPatch)*1.3;
    hot=smoothstep(.65,.90,cells*.35+ridges*.65);
   }
   float extinction=fireDensity*5.0+smokeDensity*.85+soot*4.0;
   float alpha=1.0-exp(-extinction*stepSize/radius);
   vec3 fire=mix(vec3(6.5,2.0,.055),vec3(13.0,10.5,4.5),hot);
   float flameWeight=fireDensity*hotPatch*hotPatch/
       max(fireDensity+smokeDensity+soot,.001);
   vec3 smoke=mix(vec3(.012,.011,.010),vec3(.065,.060,.055),coarse);
   smoke+=vec3(.32,.09,.012)*burn*ridge;
   vec3 source=emissionOnly ? fire*clamp(flameWeight,0.0,1.0)*smoothstep(.48,.85,hotPatch) :
       mix(smoke,fire,clamp(flameWeight,0.0,1.0));
   accumulated+=(1.0-opacity)*alpha*source;
   opacity+=(1.0-opacity)*alpha;
   if(opacity>.97)break;
  }
  color=color*(1.0-opacity)+accumulated+sparks;
 }
 gl_FragColor=vec4(emissionOnly ? color*.21 : color,1.0);
}
)FX";
 const auto v=SprintathonFlashlightShadow::compile(GL_VERTEX_SHADER_ARB,vertex);
 const auto f=SprintathonFlashlightShadow::compile(GL_FRAGMENT_SHADER_ARB,fragment);
 if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return 0;}
 shader=glCreateProgramObjectARB();glAttachObjectARB(shader,v);glAttachObjectARB(shader,f);glLinkProgramARB(shader);
 glDeleteObjectARB(v);glDeleteObjectARB(f);GLint ok=0;glGetObjectParameterivARB(shader,GL_OBJECT_LINK_STATUS_ARB,&ok);
 if(!ok){glDeleteObjectARB(shader);shader=0;std::fprintf(stderr,"Enhanced explosion shader failed; retaining original sprites\n");}
 return shader;
}
inline bool replacementReady(){return SprintathonExplosionEvents::enabled() && program()!=0;}
inline bool update(bool allowed){
 SprintathonExplosionEvents::sync();if(!allowed || !SprintathonExplosionEvents::enabled())return false;
 const auto now=sprintathon_projectile_render_tick();
 if(SprintathonRocketSmoke::program())
  for(const auto& b:SprintathonExplosionEvents::state().trails)
   if(now>=b.start && now-b.start<(b.rocket?60:45))return true;
 if(replacementReady())
  for(const auto& b:SprintathonExplosionEvents::state().blasts)
   if(now>=b.start && now-b.start<54)return true;
 return false;
}
inline void draw(const view_data* camera,GLuint color,GLuint depth,int width,int height,const GLfloat* projection,const GLfloat* modelview,bool emissionOnly=false){
 if(!color || !depth || width<=0 || height<=0 || !replacementReady())return;
 struct Item {float x,y,z,r,age,seed;};std::vector<Item> items;
 const auto now=sprintathon_projectile_render_tick();
 for(const auto& b:SprintathonExplosionEvents::state().blasts){
  world_point3d from=camera->origin,to=b.position;
  if(line_is_obstructed(camera->origin_polygon_index,&from,b.polygon,&to))continue;
  const float age=float(now-b.start)/TICKS_PER_SECOND;
  if(age>=1.8f)continue;
  // World-space rise, not screen-space rise, preserves camera roll.
  const float lift=std::max(0.0f,age-.35f)*WORLD_ONE*.24f;
  const float delta[3]={float(b.position.x-camera->origin.x),float(b.position.y-camera->origin.y),float(b.position.z-camera->origin.z)+lift};
  float eye[3]={};for(int r=0;r<3;++r)for(int c=0;c<3;++c)eye[r]+=modelview[c*4+r]*delta[c];
  const float radius=(b.rocket?.85f:.60f)*WORLD_ONE*(.12f+1.32f*(1-std::exp(-age*5.0f))+.08f*std::min(age,1.2f));
  if(-eye[2]+radius<=1)continue;
  items.push_back({eye[0],eye[1],eye[2],radius,age,float((b.start*17+unsigned(b.object)*13)%997)});
 }
 if(items.empty())return;
 std::sort(items.begin(),items.end(),[](const Item& a,const Item& b){return a.z<b.z;});
 GLfloat centres[24]={},parameters[24]={};int count=0;
 for(const auto& b:items){centres[count*4]=b.x;centres[count*4+1]=b.y;centres[count*4+2]=b.z;centres[count*4+3]=b.r;
 parameters[count*4]=b.age;parameters[count*4+1]=b.seed;if(++count==6)break;}
 GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);const auto previous=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
 glPushAttrib(GL_ALL_ATTRIB_BITS);
 if(emissionOnly){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);}
 else glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
 glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_FOG);
 glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
 glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);const auto shader=program();glUseProgramObjectARB(shader);
 auto loc=[&](const char* name){return glGetUniformLocationARB(shader,name);};
 glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,color);glUniform1iARB(loc("sceneColor"),0);
 glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,depth);glUniform1iARB(loc("sceneDepth"),1);
 glUniform1iARB(loc("emissionOnly"),emissionOnly ? 1 : 0);
 glUniform2fARB(loc("imageSize"),width,height);glUniform2fARB(loc("depthProjection"),projection[10],projection[14]);
 glUniform4fARB(loc("viewProjection"),projection[0],projection[5],projection[8],projection[9]);
 glUniform3fARB(loc("worldX"),modelview[0],modelview[1],modelview[2]);
 glUniform3fARB(loc("worldY"),modelview[4],modelview[5],modelview[6]);
 glUniform3fARB(loc("worldUp"),modelview[8],modelview[9],modelview[10]);
 glUniform4fvARB(loc("centres"),count,centres);glUniform4fvARB(loc("parameters"),count,parameters);glUniform1iARB(loc("count"),count);
 glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
 glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
 glUseProgramObjectARB(previous);glPopAttrib();glActiveTextureARB(active);
}
}
#endif
