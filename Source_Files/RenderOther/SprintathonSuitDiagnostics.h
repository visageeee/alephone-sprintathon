#ifndef SPRINTATHON_SUIT_DIAGNOSTICS_H
#define SPRINTATHON_SUIT_DIAGNOSTICS_H
#include <algorithm>
#include <string>
#include <cstdio>
#include <deque>
#include "SprintathonSuitConsole.h"
namespace SprintathonSuitDiagnostics {
enum Category {Startup,Idle,Atmosphere,Media,Impact,Oxygen,Movement,Teleport,Weapons,Items,Powerups,Saves,Death,Flicker,CategoryCount};
static const unsigned AllCategories=(1u<<CategoryCount)-1;
struct Input {
 unsigned categories=AllCategories;
 int tick=0,level=0,actor=-1,energy=0,oxygen=100,medium=0,x=0,y=0,facing=0;
 bool teleporting=false;
 bool allowed=false,vacuum=false,under=false,sprint=false,zeroG=false;
};
struct State {
 bool initialized=false;Input previous;
 unsigned categories=AllCategories;Category activeCategory=Startup;
 bool enabled(Category c)const{return (categories&(1u<<c))!=0;}
 int started=0,priority=0,sprintStart=-1,bootAt=0,idleAt=0,serial=0,analysis=0;
 bool atmospherePending=false;
 unsigned analyzed=0;
 struct Notice {int tick,mode;Category category;std::string heading,text;};
 std::deque<Notice> notices;
 void notify(int now,const char* heading,const char* text,int mode=0,Category category=Powerups){
  if(!enabled(category))return;
  if(!initialized || !previous.allowed || previous.energy<=0)return;
  for(auto& n:notices)if(n.heading==heading && n.text==text){n.tick=now;return;}
  if(notices.size()>=4)notices.pop_front();
  notices.push_back(Notice{now,mode,category,heading,text});
 }
 struct Pickup {int type,tick,count;bool weapon;std::string name;};
 std::deque<Pickup> pickups;
 void pickup(int type,const char* name,bool newWeapon,int now,int level,int actor){
  if(!enabled(newWeapon?Weapons:Items))return;
  if(!initialized || !previous.allowed || previous.energy<=0 || previous.level!=level || previous.actor!=actor)return;
  for(auto& p:pickups)if(p.type==type){++p.count;p.tick=now;p.weapon=p.weapon||newWeapon;return;}
  if(pickups.size()>=6)pickups.pop_front();
  pickups.push_back(Pickup{type,now,1,newWeapon,name});
 }
 int cooldown[6]={};std::string label,detail;
 void reset(){*this=State{};}
 void show(int rank,int now,const char* heading,const char* text,int mode=0,Category category=Startup){
  if(!enabled(category))return;
  activeCategory=category;label=heading;detail=text;started=now;priority=rank;analysis=mode;++serial;
  SprintathonSuitConsole::state().append(label+" // "+reading());
 }
 void post(int kind,int rank,int now,const char* heading,const char* text,Category category){
  if(!enabled(category))return;
  if(now<cooldown[kind] || (!label.empty() && now-started<150 && rank<priority))return;
  show(rank,now,heading,text,0,category);cooldown[kind]=now+300;
 }
 void environment(const Input& in){
  if(!enabled(in.under?Media:Atmosphere))return;
  const unsigned bit=1u<<(in.under?std::max(1,std::min(4,in.medium)):(in.vacuum?6:5));
  if(analyzed&bit)return;
  analyzed|=bit;
  if(in.under){
   if(in.medium==1)show(1,in.tick,"MEDIUM ANALYSIS // EST.","",1,Media);
   else if(in.medium==2)show(1,in.tick,"CONTAMINATED // EST.","",2,Media);
   else if(in.medium==3)show(2,in.tick,"THERMAL WARNING // EST.","",3,Media);
   else show(1,in.tick,"SPECTRAL MATCH FAILED","",4,Media);
  }else if(in.vacuum)show(2,in.tick,"ATMOSPHERE CHECK","NO EXTERNAL PRESSURE // SEAL ACTIVE",0,Atmosphere);
  else show(1,in.tick,"ATMOSPHERE ANALYSIS // EST.","",5,Atmosphere);
 }
 void update(const Input& in){
  if(!in.allowed){reset();return;}
  categories=in.categories;
  bool fresh=!initialized || previous.level!=in.level || previous.actor!=in.actor || in.tick<previous.tick || (previous.energy<=0 && in.energy>0);
  if(fresh){
   const unsigned retained=initialized && previous.level==in.level && in.tick>=previous.tick?analyzed:0;
   reset();categories=in.categories;analyzed=retained;initialized=true;previous=in;bootAt=in.tick;idleAt=in.tick;
   for(auto& c:cooldown)c=in.tick;
   show(0,in.tick,"RESUME VECTOR","CONTINUITY CHECK... ACCEPTED",6);atmospherePending=true;
  }
  if(!enabled(activeCategory)){label.clear();detail.clear();priority=0;analysis=0;}
  int now=in.tick;
  if(!fresh && previous.teleporting && !in.teleporting)
   notify(now,"POSITION DISCONTINUITY","REACQUIRING // LOCAL FRAME RESTORED",0,Teleport);
  if(!label.empty() && now-started>=150){label.clear();detail.clear();priority=0;analysis=0;}
  if(in.energy<=0){
   if(fresh || previous.energy>0)show(4,now,"ERROR TYPE 11","",7,Death);
   previous=in;return;
  }
  if(in.x!=previous.x || in.y!=previous.y || in.facing!=previous.facing || in.sprint || in.energy!=previous.energy)idleAt=now;
  while(!notices.empty() && (now-notices.front().tick>450 || !enabled(notices.front().category)))notices.pop_front();
  if(label.empty() && !notices.empty()){
   const Notice n=notices.front();notices.pop_front();
   show(1,now,n.heading.c_str(),n.text.c_str(),n.mode,n.category);idleAt=now;
  }
  // Retry unseen environment analysis after higher-priority messages finish.
  if(now-bootAt>=150 && label.empty()){environment(in);atmospherePending=false;}
  if(in.sprint){if(sprintStart<0)sprintStart=now;
   if(now-sprintStart>=90){post(0,0,now,"ACTUATORS","SUSTAINED LOAD",Movement);sprintStart=now+300;}}
  else sprintStart=-1;
  if((fresh && in.zeroG) || (!fresh && in.zeroG!=previous.zeroG))post(2,1,now,"INERTIAL SYSTEM",in.zeroG?"GRAVITY REFERENCE LOST":"GRAVITY REFERENCE RESTORED",Movement);
  if(!fresh && previous.energy-in.energy>=25){
   std::string t="SHIELD TRANSIENT // "+std::to_string(std::max(0,in.energy*100/150))+"% BASE CAPACITY";
   post(4,2,now,"IMPACT WARNING",t.c_str(),Impact);
  }
  if(in.oxygen<=20 && (fresh || previous.oxygen>20 || now>=cooldown[5]))post(5,3,now,"O2 WARNING","RESERVE LOW // SEEK REPLENISHMENT",Oxygen);
  while(!pickups.empty() && (now-pickups.front().tick>300 || !enabled(pickups.front().weapon?Weapons:Items)))pickups.pop_front();
  if(label.empty() && !pickups.empty()){
   const Pickup p=pickups.front();pickups.pop_front();
   const std::string text=p.name+(p.count>1?" // x"+std::to_string(p.count):"");
   show(1,now,p.weapon?"WEAPON ACQUIRED":"ITEM ACQUIRED",text.c_str(),0,p.weapon?Weapons:Items);idleAt=now;
  }
  if(enabled(Idle) && label.empty() && now-idleAt>=1800+(serial%4)*300){
   const char* heads[]={"BACKGROUND PROCESS // ACTIVE","MEMORY CHECK","SIGNAL CHECK","INTEGRITY CHECK","PENDING INSTRUCTION"};
   const char* texts[]={"NO OPERATOR INPUT","PREVIOUS STATE UNAVAILABLE","LOCAL RESPONSE ONLY","DISCREPANCY WITHIN TOLERANCE","SOURCE UNRESOLVED"};
   unsigned choice=(unsigned(now)/30+unsigned(serial)*17)%5;
   show(0,now,heads[choice],texts[choice],choice==1 || choice==4?6:0,Idle);idleAt=now;
  }
  previous=in;
 }
 unsigned code(int salt=0)const{
  unsigned phase=unsigned(std::min(60,std::max(0,previous.tick-started))/8);
  unsigned value=unsigned(started)*1664525u+unsigned(serial)*1013904223u+phase*2246822519u+unsigned(salt)*3266489917u;
  return (value^(value>>16))&65535u;
 }
 std::string hex()const{char b[48];std::snprintf(b,sizeof(b),"0x%04X // 0x%04X // 0x%04X",code(),code(1),code(2));return b;}
 std::string reading()const{
  if(analysis==7)return hex();
  if(analysis==6){char b[20];std::snprintf(b,sizeof(b)," // 0x%04X",code());return detail+b;}
  if(!analysis)return detail;
  // Fictional bounded sensor estimates; composition totals are exactly 100%.
  int phase=std::max(0,previous.tick-started)/10;
  int drift=(phase%8<=4?phase%8:8-phase%8)-2;
  char b[128];
  if(analysis==1)std::snprintf(b,sizeof(b),"H2O %.2f%% // SOLUTES %.2f%%",98.72+drift*.01,1.28-drift*.01);
  else if(analysis==2)std::snprintf(b,sizeof(b),"H2O %.2f%% ORGANICS %.2f%% OTHER 0.43%%",91.36+drift*.01,8.21-drift*.01);
  else if(analysis==3)std::snprintf(b,sizeof(b),"Si %.2f%% O %.2f%% Fe/Mg/OTHER 31.76%%",22.41+drift*.01,45.83-drift*.01);
  else if(analysis==4)std::snprintf(b,sizeof(b),"CONFIDENCE %.3f // 0x%04X",.031+drift*.001,code());
  else std::snprintf(b,sizeof(b),"N2 %.2f%% O2 %.2f%% CO2 0.04%% OTHER 0.93%%",78.08+drift*.01,20.95-drift*.01);
  return b;
 }
 float opacity()const{
  if(label.empty())return 0;
  int age=std::max(0,previous.tick-started);
  if(analysis!=7 && priority>=2 && age<24 && (age/6)%2)return .20f;
  // Cosmetic, deterministic short dimming pulses; never perturb gameplay RNG.
  if(enabled(Flicker) && previous.energy>0 && previous.energy<=37){
   unsigned t=unsigned(previous.tick),phase=t%47u;
   if(phase<2u || (phase==9u && ((t/47u)%3u)==0u))return previous.energy<=15?.45f:.65f;
  }
  return 1;
 }
 float detailOpacity()const{int age=std::max(0,previous.tick-started);return analysis==7 && age<60 && (age/8)%2?.3f:1.0f;}
 float scrollProgress()const{float t=std::max(0.0f,std::min(1.0f,(previous.tick-started-120)/30.0f));return t*t*(3-2*t);}
 std::string typed()const{std::string t=reading();return analysis==7?t:t.substr(0,std::min(t.size(),size_t(std::max(0,previous.tick-started))*2));}
 std::string heading()const{return priority>=2?label:label.substr(0,std::min(label.size(),size_t(std::max(0,previous.tick-started))*2));}
};
inline State& state(){static State s;return s;}
}
#endif
