#ifndef SPRINTATHON_LUA_SUN_H
#define SPRINTATHON_LUA_SUN_H
#include "cseries.h"
#include "SprintathonTimeOfDay.h"
#include "preferences.h"
#include <cmath>
#include <algorithm>
namespace SprintathonLuaSun {
enum Field { Enabled, Azimuth, Elevation, Size, Brightness, Shafts, Glare, Bloom, Red, Green, Blue, Count };
struct Point {double azimuth=0,elevation=0;};
struct State {
 bool set[Count]={}; double value[Count]={};
 Point path[8]; bool declared=false,locked=false;
};
inline State& state(){static State s;return s;}
inline void reset(){state()=State();}
inline double get(Field f) {
 if(SprintathonTimeOfDay::state().active) {
  const auto day=SprintathonTimeOfDay::sample();
  switch(f) {
  case Enabled:return day.elevation>=0;
  case Azimuth:return 135;
  case Elevation:return day.elevation;
  case Brightness:case Shafts:return day.brightness;
  case Red:return day.direct.r;
  case Green:return day.direct.g;
  case Blue:return day.direct.b;
  default:break;
  }
 }
 const auto& s=state();if(s.set[f])return s.value[f];
 const auto& c=graphics_preferences->OGL_Configure;
 static const double sizes[]={0.25,0.5,1,2,4};
 static const double colors[][3]={{1,.91,.72},{1,1,1},{1,.55,.18},{1,.22,.12},{.65,.82,1}};
 int size=graphics_preferences->visible_sun_size;size=size<0?0:(size>4?4:size);
 int color=graphics_preferences->visible_sun_color;color=color<0?0:(color>4?4:color);
 switch(f){
 case Enabled:return 1;
 case Azimuth:return c.LandscapeLightShaftDirection;
 case Elevation:return c.LandscapeLightShaftElevation;
 case Size:return sizes[size];
 case Brightness:return graphics_preferences->visible_sun_brightness/100.0;
 case Shafts:return c.LandscapeLightShaftStrength/100.0;
 case Red:case Green:case Blue:return colors[color][f-Red];
 default:return 1;
 }
}
inline void set(Field f,double v){state().set[f]=true;state().value[f]=v;}
inline double angleDelta(double a,double b){
 double d=std::fmod(b-a+540.0,360.0)-180.0;return d;
}
inline void pathPosition(double position){
 double t=std::max(0.0,std::min(1.0,position))*7.0;
 int lo=int(std::floor(t)),hi=std::min(lo+1,7);double f=t-lo;
 const auto& s=state();
 set(Azimuth,s.path[lo].azimuth+angleDelta(s.path[lo].azimuth,s.path[hi].azimuth)*f);
 set(Elevation,s.path[lo].elevation*(1-f)+s.path[hi].elevation*f);
}
struct Blend {int lo=0,hi=0;double fraction=0;};
inline Blend pathBlend(){
 const auto& s=state();Blend result;double best=1e100;
 const double az=get(Azimuth),el=get(Elevation);
 for(int i=0;i<7;++i){
  double dx=angleDelta(s.path[i].azimuth,s.path[i+1].azimuth);
  double dy=s.path[i+1].elevation-s.path[i].elevation;
  double px=angleDelta(s.path[i].azimuth,az),py=el-s.path[i].elevation;
  double length2=dx*dx+dy*dy;
  double t=length2>1e-12?std::max(0.0,std::min(1.0,(px*dx+py*dy)/length2)):0;
  double distance=(px-dx*t)*(px-dx*t)+(py-dy*t)*(py-dy*t);
  if(distance<best){best=distance;result.lo=i;result.hi=i+1;result.fraction=t;}
 }
 // Off-path directions use a nearest existing bake; never generate a new one.
 if(best>1e-6){
  best=1e100;
  for(int i=0;i<8;++i){double dx=angleDelta(s.path[i].azimuth,az),dy=el-s.path[i].elevation;
   double d=dx*dx+dy*dy;if(d<best){best=d;result.lo=result.hi=i;result.fraction=0;}}
 }
 return result;
}
}
#endif
