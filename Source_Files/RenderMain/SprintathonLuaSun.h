#ifndef SPRINTATHON_LUA_SUN_H
#define SPRINTATHON_LUA_SUN_H
#include "cseries.h"
#include "preferences.h"
#include <cmath>
namespace SprintathonLuaSun {
enum Field { Enabled, Azimuth, Elevation, Size, Brightness, Shafts, Glare, Bloom, Red, Green, Blue, Count };
struct State { bool set[Count]={}; double value[Count]={}; };
inline State& state(){static State s;return s;}
inline void reset(){state()=State();}
inline double get(Field f) {
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
}
#endif
