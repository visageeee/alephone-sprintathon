#ifndef SPRINTATHON_TIME_OF_DAY_H
#define SPRINTATHON_TIME_OF_DAY_H
#include <algorithm>
#include <cmath>
namespace SprintathonTimeOfDay {
// Session-only override: never alter map data or saved graphics preferences.
struct State { bool active=false; int minutes=720; };
inline State& state() { static State s; return s; }
inline void reset() { state().active=false; }
inline void configure(bool active,int minutes) {
 state().active=active; state().minutes=std::max(0,std::min(1439,minutes));
}
struct Color { double r,g,b; };
inline Color mix(Color a,Color b,double t) {
 return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};
}
struct Sample { Color zenith,horizon,ambient,direct; double elevation,brightness,fogDepth; };
inline Sample sample(int minutes) {
 struct Key { double hour; Color zenith,horizon,ambient,direct; double brightness,fogDepth; };
 static const Key keys[]={
  {0,{.008,.015,.055},{.025,.045,.11},{.06,.10,.24},{0,0,0},0,80},
  {5,{.025,.035,.10},{.16,.10,.20},{.12,.13,.25},{0,0,0},0,32},
  {6,{.18,.19,.36},{.85,.39,.20},{.40,.27,.30},{1,.38,.16},.45,14},
  {8,{.23,.44,.75},{.72,.78,.83},{.62,.72,.85},{1,.82,.60},1,24},
  {12,{.12,.34,.72},{.68,.80,.95},{.65,.78,1},{1,.95,.84},1,100},
  {16,{.16,.32,.62},{.78,.65,.49},{.67,.64,.65},{1,.76,.48},1,80},
  {18,{.16,.10,.28},{.90,.26,.10},{.39,.20,.29},{1,.25,.08},.45,65},
  {19,{.025,.025,.095},{.18,.055,.15},{.13,.11,.26},{0,0,0},0,70},
  {24,{.008,.015,.055},{.025,.045,.11},{.06,.10,.24},{0,0,0},0,80}
 };
 const double h=std::max(0,std::min(1440,minutes))/60.0;
 int i=0;while(i<7 && h>keys[i+1].hour)++i;
 const Key &a=keys[i],&b=keys[i+1];double t=(h-a.hour)/(b.hour-a.hour);
 // Smooth color transitions without an abrupt dawn or dusk boundary.
 t=t*t*(3-2*t);
 return {mix(a.zenith,b.zenith,t),mix(a.horizon,b.horizon,t),
  mix(a.ambient,b.ambient,t),mix(a.direct,b.direct,t),
  65*std::sin((h-6)*6.283185307179586/24),
  a.brightness+(b.brightness-a.brightness)*t,a.fogDepth+(b.fogDepth-a.fogDepth)*t};
}
inline Sample sample() { return sample(state().minutes); }
}
#endif
