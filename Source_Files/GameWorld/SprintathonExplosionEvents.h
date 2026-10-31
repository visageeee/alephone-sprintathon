#ifndef SPRINTATHON_EXPLOSION_EVENTS_H
#define SPRINTATHON_EXPLOSION_EVENTS_H
#include "map.h"
#include "effects.h"
#include "projectiles.h"
#include "preferences.h"
#include <vector>
#include <algorithm>
#include <cstdlib>
namespace SprintathonExplosionEvents {
inline bool enabled(){return graphics_preferences && graphics_preferences->enhanced_explosions;}
struct Blast {world_point3d position;short polygon,effect,object;uint64_t start;bool rocket;};
struct State {std::vector<Blast> blasts,trails;long tick=-1;int level=-1;};
inline State& state(){static State s;return s;}
inline void sync(){auto& s=state();const auto now=sprintathon_projectile_render_tick();
 if(!enabled()){s.blasts.clear();s.trails.clear();}
 if(s.level!=dynamic_world->current_level_number || dynamic_world->tick_count<s.tick){s.blasts.clear();s.trails.clear();}
 s.level=dynamic_world->current_level_number;s.tick=dynamic_world->tick_count;
 s.blasts.erase(std::remove_if(s.blasts.begin(),s.blasts.end(),[&](const Blast& b){return now<b.start || now-b.start>=75;}),s.blasts.end());
 s.trails.erase(std::remove_if(s.trails.begin(),s.trails.end(),[&](const Blast& b){
     return now<b.start || (now-b.start>=(b.rocket?60:45) && b.effect<0);
 }),s.trails.end());
}
// Only the projectile spawner supplies this context. Effect numbers may be
// reassigned by scenarios and do not identify the weapon that made the trail.
inline short& trailSource(){static short source=-1;return source;}
struct ScopedTrailSource {
 short previous;
 explicit ScopedTrailSource(short type):previous(trailSource()){trailSource()=type;}
 ~ScopedTrailSource(){trailSource()=previous;}
 ScopedTrailSource(const ScopedTrailSource&)=delete;
 ScopedTrailSource& operator=(const ScopedTrailSource&)=delete;
};
inline void emit(short type,short effect,short object,short polygon,const world_point3d& position){
 if(!enabled())return;
 if(trailSource()>=0){
  const short source=trailSource();
  const bool rocket=source==_projectile_rocket || source==_projectile_juggernaut_rocket;
  const bool grenade=source==_projectile_grenade || source==_projectile_trooper_grenade;
  if(!rocket && !grenade)return;
  sync();auto& s=state();
  if(s.trails.size()<96)s.trails.push_back({position,polygon,effect,object,
      sprintathon_projectile_render_tick(),rocket});
  return;
 }
 if((type!=_effect_rocket_explosion && type!=_effect_grenade_explosion))return;
 sync();auto& s=state();
 // At capacity, leave new effects on their original rendering path.
 if(s.blasts.size()>=6)return;
 s.blasts.push_back({position,polygon,effect,object,sprintathon_projectile_render_tick(),type==_effect_rocket_explosion});
}
inline void detach(short effect){
 for(auto& b:state().blasts)if(b.effect==effect)b.effect=-1;
 for(auto& b:state().trails)if(b.effect==effect)b.effect=-1;
}
inline bool replacesTrail(short effect){
 if(effect<0 || !enabled())return false;
 for(const auto& b:state().trails)if(b.effect==effect)return true;
 return false;
}
inline bool replaces(short effect){if(effect<0 || !enabled())return false;
 for(const auto& b:state().blasts)if(b.effect==effect)return true;
 return false;
}
}
#endif
