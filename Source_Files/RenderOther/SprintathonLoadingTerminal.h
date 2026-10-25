#ifndef SPRINTATHON_LOADING_TERMINAL_H
#define SPRINTATHON_LOADING_TERMINAL_H
#include "FontHandler.h"
#include "screen.h"
#include "map.h"
#include <vector>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
namespace SprintathonLoadingTerminal {
struct Line{std::string text;Uint32 time;bool stage;};
struct MapEdge{float x0,y0,x1,y1;};
struct State{
 std::vector<MapEdge> geometry;
 float mapLeft=0,mapTop=0,mapRight=0,mapBottom=0;
 std::deque<Line> lines;std::string stage;
 Uint32 started=0,lastLine=0,lastScroll=0;
 int percent=0,fontSize=0;bool fontReady=false,complete=false;
 Uint32 completedAt=0,glitchAt=0;
 int glitchCount=0,nextGlitch=0,glitchThreshold[2]={0,0},revealedPercent=0;
 bool glitchStarted=false;
 FontSpecifier font{};
 State(){font.NameSet="Monaco";font.Size=18;font.Style=styleNormal;font.AdjustLineHeight=0;font.File="mono";}
};
inline State& state(){static State s;return s;}
inline void append(const std::string& text,bool stage,Uint32 now){
 auto& s=state();s.lines.push_back({text,Uint32(now-s.started),stage});
 while(s.lines.size()>128)s.lines.pop_front();
 s.lastLine=s.lastScroll=now;
}
inline void captureGeometry(){
 auto& s=state();s.geometry.clear();
 if(!dynamic_world)return;
 const int count=std::min(int(LineList.size()),int(dynamic_world->line_count));
 const int endpoints=std::min(int(EndpointList.size()),int(dynamic_world->endpoint_count));
 for(int i=0;i<count;++i){
  const auto& edge=LineList[i];
  const int a=edge.endpoint_indexes[0],b=edge.endpoint_indexes[1];
  if(a<0 || b<0 || a>=endpoints || b>=endpoints)continue;
  const auto& p=EndpointList[a].vertex;const auto& q=EndpointList[b].vertex;
  const MapEdge e={float(p.x),float(p.y),float(q.x),float(q.y)};
  if(s.geometry.empty()){s.mapLeft=s.mapRight=e.x0;s.mapTop=s.mapBottom=e.y0;}
  s.mapLeft=std::min(s.mapLeft,std::min(e.x0,e.x1));
  s.mapRight=std::max(s.mapRight,std::max(e.x0,e.x1));
  s.mapTop=std::min(s.mapTop,std::min(e.y0,e.y1));
  s.mapBottom=std::max(s.mapBottom,std::max(e.y0,e.y1));
  s.geometry.push_back(e);
 }
}
inline void start(){
 auto& s=state();s.lines.clear();s.stage.clear();s.percent=0;s.complete=false;s.completedAt=0;
 s.started=s.lastLine=s.lastScroll=SDL_GetTicks();
 // Separate cosmetic seed: never advance gameplay randomness.
 Uint32 seed=s.started^0x93A5F12Du;seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
 s.glitchCount=1+int(seed&1u);s.nextGlitch=0;s.glitchStarted=false;s.revealedPercent=0;
 s.glitchThreshold[0]=18+int((seed>>1)%25u);
 s.glitchThreshold[1]=61+int((seed>>9)%25u);
 captureGeometry();
 append("continuity fragment / address unknown",true,s.started);
 append("afferent... [signal incomplete]",false,s.started);
}
inline void status(const std::string& message){
 auto& s=state();
 std::string next=message.compare(0,9,"Loading: ")==0?message.substr(9):message;
 if(s.complete || next==s.stage)return;
 s.stage=next;
 append("> "+next,true,SDL_GetTicks());
}
inline void progress(int value){
 auto& s=state();if(s.complete)return;s.percent=std::max(0,std::min(100,value));
 const Uint32 now=SDL_GetTicks();
 s.revealedPercent=std::max(s.revealedPercent,s.percent);
 if(s.percent<100 && s.nextGlitch<s.glitchCount && s.percent>=s.glitchThreshold[s.nextGlitch]){
  s.glitchAt=now;s.glitchStarted=true;++s.nextGlitch;
 }
 // A heartbeat describes real ongoing work, without invented file names.
 if(!s.stage.empty() && Uint32(now-s.lastLine)>=1000){
  char text[240];std::snprintf(text,sizeof(text),"  %s  /  preload %d%%",s.stage.c_str(),s.percent);
  append(text,false,now);
 }
}
inline void finish(){
 auto& s=state();if(s.complete)return;
 s.complete=true;s.percent=100;s.revealedPercent=100;s.glitchStarted=false;s.completedAt=SDL_GetTicks();
 append("pattern recovered / identity --",true,s.completedAt);
 append("efferent... [carrier present]",true,s.completedAt);
}
inline std::string completionText(Uint32 now){
 const std::string message="loading complete...";
 const size_t characters=std::min(message.size(),size_t((Uint32(now-state().completedAt)>180?Uint32(now-state().completedAt)-180:0)/28));
 return message.substr(0,characters);
}
// Cosmetic interference only: never consume the simulation random stream.
inline bool signalGlitch(Uint32 now){
 const auto& s=state();return s.glitchStarted && !s.complete && Uint32(now-s.glitchAt)<160;
}
inline std::string signalText(Uint32 now){
 const bool garbled=signalGlitch(now);
 if(!garbled)now=state().started;
 Uint32 bits=(now/90)^0xA17E03D5u;
 auto next=[&](){bits^=bits<<13;bits^=bits>>17;bits^=bits<<5;return bits;};
 const unsigned a=unsigned(next()&0xffffu),b=unsigned(next()&0xffffu),c=unsigned(next()&0xffffu);
 const char* fragments[]={"recall / --", "r_ec.ll / ??", "[carrier]", "self / ----", "s.lf / ????", "[no address]"};
 char text[128];std::snprintf(text,sizeof(text),"%04X : %04X : %04X  /  %s",a,b,c,garbled?fragments[(now/35)%6]:"continuity / --");
 return text;
}
inline void rectangle(float x,float y,float w,float h,float r,float g,float b){
 glDisable(GL_TEXTURE_2D);glColor3f(r,g,b);
 glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
}
inline void draw(){
 auto& s=state();const int width=MainScreenPixelWidth(),height=MainScreenPixelHeight();
 if(width<=0 || height<=0)return;
 // Native drawable pixels, independent of game render scale or scenario HUD size.
 GLint mode,active,client,buffer,element,unpack,fbo=0;
 glGetIntegerv(GL_MATRIX_MODE,&mode);glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
 glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE_ARB,&client);
 glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&unpack);
 glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB,&buffer);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB,&element);
 const auto program=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
 if(glBindFramebufferEXT)glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&fbo);
 glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT|GL_CLIENT_PIXEL_STORE_BIT);
 if(glBindFramebufferEXT)glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
 glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
 glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
 glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
 glUseProgramObjectARB(0);glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
 // Fixed-function text must not inherit additional enabled texture units.
 GLint units=1;glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB,&units);
 for(int i=units-1;i>=0;--i){
  glActiveTextureARB(GL_TEXTURE0_ARB+i);
  glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE_ARB);
  glDisable(GL_TEXTURE_3D);glDisable(GL_TEXTURE_CUBE_MAP_ARB);
  glDisable(GL_TEXTURE_GEN_S);glDisable(GL_TEXTURE_GEN_T);
 }
 glClientActiveTextureARB(GL_TEXTURE0_ARB);
 glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();
 glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
 glViewport(0,0,width,height);glDrawBuffer(GL_BACK);
 glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_ALPHA_TEST);
 glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
 glDisable(GL_FOG);glDisable(GL_LIGHTING);glDisable(GL_BLEND);
 glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);
 glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
 glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width,height,0,-1,1);
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 const int size=std::max(12,std::min(32,height/60));
 if(!s.fontReady){s.font.Size=size;s.font.Init();s.fontReady=true;s.fontSize=size;}
 else if(s.fontSize!=size){s.font.OGL_Reset(false);s.font.Size=size;s.font.Update();s.fontSize=size;}
 const int line=std::max(size+6,int(s.font.LineSpacing)+5);
 const int margin=std::max(18,width/30);
 auto text=[&](int x,int y,const std::string& value,float r,float g,float b){
  if(!s.font.Info)return;
  std::string clipped=value.substr(0,240);
  const int available=std::max(0,width-margin-x);
  while(!clipped.empty() && s.font.TextWidth(clipped.c_str())>available)clipped.pop_back();
  glColor3f(r,g,b);glPushMatrix();glTranslated(x,y,0);s.font.OGL_Render(clipped.c_str());glPopMatrix();
 };
 rectangle(0,0,width,height,0,0,0);
 text(margin,margin+line,"UESCTerm 802.11 / REMOTE SIGNAL",1.0f,0.65f,0.18f);
 const Uint32 signalTime=SDL_GetTicks();
 const float signalGain=signalGlitch(signalTime)?0.48f:1.0f;
 text(margin,margin+line*2,signalText(signalTime),signalGain,0.58f*signalGain,0.12f*signalGain);
 const int top=margin+line*3,bottom=height-margin-line*4;
 rectangle(margin,top-8,width-margin*2,1,0.48f,0.24f,0.035f);
 // Scroll history only; headings and progress remain fixed.
 const int rows=std::max(1,(bottom-top-line)/line);
 const bool showMap=width>=800 && !s.geometry.empty();
 const int logRight=showMap?width*55/100:width-margin;
 const int first=std::max(0,int(s.lines.size())-rows);
 const float phase=std::min(1.0f,float(Uint32(SDL_GetTicks()-s.lastScroll))/160.0f);
 const int shift=int((1-phase)*(1-phase)*line);
 glEnable(GL_SCISSOR_TEST);glScissor(margin,std::max(0,height-bottom),std::max(0,logRight-margin),std::max(0,bottom-top));
 for(int i=first;i<int(s.lines.size());++i){
  const auto& entry=s.lines[i];char stamp[32];
  std::snprintf(stamp,sizeof(stamp),"%6.2f",double(entry.time)/1000);
  // Keep the newest line at the bottom; incoming lines rise through the clip edge.
  const int y=bottom-line/3-(int(s.lines.size())-1-i)*line+shift;
  std::string entryText=std::string(stamp)+"  "+entry.text;
  while(!entryText.empty() && s.font.TextWidth(entryText.c_str())>logRight-margin)entryText.pop_back();
  text(margin,y,entryText,entry.stage?1.0f:0.72f,entry.stage?0.58f:0.36f,entry.stage?0.12f:0.07f);
 }
 glDisable(GL_SCISSOR_TEST);
 if(showMap){
  const int x=width*59/100,w=width-margin-x;
  const int y=top+line*2,h=std::max(1,bottom-y-line*2);
  const float scale=std::min(float(w)/std::max(1.0f,s.mapRight-s.mapLeft),
                             float(h)/std::max(1.0f,s.mapBottom-s.mapTop));
  const float cx=(s.mapLeft+s.mapRight)*0.5f,cy=(s.mapTop+s.mapBottom)*0.5f;
  const Uint32 mapTime=SDL_GetTicks();
  const float gain=signalGlitch(mapTime)?0.38f:0.28f;
  text(x,top+line,"SPATIAL FRAGMENT / "+signalText(mapTime).substr(0,18),0.55f,0.30f,0.06f);
  glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_LINE_SMOOTH);glLineWidth(1.0f);
  glColor3f(gain,gain*0.58f,gain*0.12f);
  glBegin(GL_LINES);
  const size_t visible=s.geometry.size()*size_t(s.revealedPercent)/100;
  for(size_t i=0;i<visible;++i){
   const auto& e=s.geometry[i];
   glVertex2f(x+w*0.5f+(e.x0-cx)*scale,y+h*0.5f+(e.y0-cy)*scale);
   glVertex2f(x+w*0.5f+(e.x1-cx)*scale,y+h*0.5f+(e.y1-cy)*scale);
  }
  glEnd();
 }
 rectangle(margin,bottom+line/2,width-margin*2,1,0.48f,0.24f,0.035f);
 const int footer=height-margin-line*2;
 const std::string current=s.complete?std::string("awaiting handoff..."):
     (s.stage.empty()?std::string("recovering signal..."):s.stage);
 text(margin,footer,"> "+current+((SDL_GetTicks()/350)%2?" _":""),1.0f,0.58f,0.12f);
 rectangle(margin,footer+line/2,width-margin*2,4,0.16f,0.075f,0.01f);
 rectangle(margin,footer+line/2,(width-margin*2)*s.percent/100.0f,4,1.0f,0.58f,0.12f);
 rectangle(0,height-margin-line,width,margin+line,0.12f,0.055f,0.008f);
 char summary[160];
 if(s.complete)std::snprintf(summary,sizeof(summary),"LINK RESTORED / ORIGIN UNKNOWN");
 else std::snprintf(summary,sizeof(summary),"SYSTEM LOAD %3d%%    ELAPSED %.1fs    %d x %d",s.percent,
               double(Uint32(SDL_GetTicks()-s.started))/1000,width,height);
 text(margin,height-margin,summary,1.0f,0.65f,0.18f);
 // Completion stays centered using the full message width, even while typing.
 const Uint32 now=SDL_GetTicks();
 if(s.complete){
  const int labelWidth=s.font.TextWidth("loading complete... _");
  const int boxWidth=std::min(width-margin*2,labelWidth+line*3);
  const int boxHeight=line*3;
  const int x=(width-boxWidth)/2,y=(height-boxHeight)/2;
  rectangle(x,y,boxWidth,boxHeight,0.75f,0.38f,0.06f);
  rectangle(x+2,y+2,boxWidth-4,boxHeight-4,0.035f,0.016f,0.002f);
  text((width-labelWidth)/2,height/2+size/3,
       completionText(now)+((now/250)%2?" _":""),1.0f,0.65f,0.18f);
 }
 // Uniform scanlines at native resolution. One batched draw keeps
 // the scanline treatment inexpensive even on a high-resolution display.
 glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);
 glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 // Exactly one dark native pixel row followed by one untouched row.
 const int pitch=2,darkRows=1;
 glColor4f(0,0,0,0.49f);
 glBegin(GL_QUADS);
 for(int y=0;y<height;y+=pitch){
  glVertex2f(0,y);glVertex2f(width,y);glVertex2f(width,y+darkRows);glVertex2f(0,y+darkRows);
 }
 glEnd();
 // A brief dimming flutter, then a steady image while the message types.
 const Uint32 ending=Uint32(now-s.completedAt);
 if(s.complete && ending<180){
  const float dim=(ending<50 || (ending>=100 && ending<140))?0.55f:0.12f;
  glColor4f(0,0,0,dim);
  glBegin(GL_QUADS);glVertex2f(0,0);glVertex2f(width,0);
  glVertex2f(width,height);glVertex2f(0,height);glEnd();
 }
 glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();
 glMatrixMode(GL_TEXTURE);glPopMatrix();glMatrixMode(mode);
 if(glBindFramebufferEXT)glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
 glUseProgramObjectARB(program);
 glPopClientAttrib();glPopAttrib();
 glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,unpack);
 glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,element);
 glClientActiveTextureARB(client);glActiveTextureARB(active);
}
}
#endif
