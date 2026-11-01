// Explicit projectile splash binding v1
// Distinct grenade plume v2
// Directional grenade splashes v1
#ifndef SPRINTATHON_EXPLOSION_EVENTS_H
#define SPRINTATHON_EXPLOSION_EVENTS_H
#include "map.h"
#include "media.h"
#include <cmath>
#include "effects.h"
#include "projectiles.h"
#include "preferences.h"
#include <vector>
#include <algorithm>
#include <cstdlib>
namespace SprintathonExplosionEvents {
inline bool enabled(){return graphics_preferences && graphics_preferences->enhanced_explosions;}
inline bool splashesEnabled(){return graphics_preferences && graphics_preferences->enhanced_splashes;}
struct Blast {world_point3d position;short polygon,effect,object;uint64_t start;bool rocket;};
struct Splash {world_point3d position;short polygon;uint64_t start;bool rocket,bullet;float dx,dy,steepness;short effect;bool grenade;};
struct State {std::vector<Blast> blasts,trails;std::vector<Splash> splashes;std::vector<short> splashSprites;long tick=-1;int level=-1;};
inline State& state(){static State s;return s;}
inline void sync(){auto& s=state();const auto now=sprintathon_projectile_render_tick();
 if(!enabled()){s.blasts.clear();s.trails.clear();}
 if(!splashesEnabled()){s.splashes.clear();s.splashSprites.clear();}
 if(s.level!=dynamic_world->current_level_number || dynamic_world->tick_count<s.tick){s.blasts.clear();s.trails.clear();s.splashes.clear();s.splashSprites.clear();}
 s.level=dynamic_world->current_level_number;s.tick=dynamic_world->tick_count;
 s.blasts.erase(std::remove_if(s.blasts.begin(),s.blasts.end(),[&](const Blast& b){return now<b.start || now-b.start>=75;}),s.blasts.end());
 s.splashes.erase(std::remove_if(s.splashes.begin(),s.splashes.end(),[&](const Splash& b){return now<b.start || now-b.start>=(b.bullet?15:54);}),s.splashes.end());
 s.trails.erase(std::remove_if(s.trails.begin(),s.trails.end(),[&](const Blast& b){
     return now<b.start || (now-b.start>=(b.rocket?60:45) && b.effect<0);
 }),s.trails.end());
}
// Visual-only surface impacts; never consume gameplay RNG or allocate map objects.
inline bool emitSplash(const world_point3d& surface,short polygon,bool rocket,bool bullet=false,float dx=0,float dy=0,float dz=-1,bool grenade=false){
 if(!splashesEnabled())return false;
 sync();auto& s=state();
 const auto count=std::count_if(s.splashes.begin(),s.splashes.end(),
     [&](const Splash& b){return b.bullet==bullet;});
 if(count>=(bullet?12:6)){
  const auto oldest=std::find_if(s.splashes.begin(),s.splashes.end(),[&](const Splash& b){return b.bullet==bullet;});
  s.splashes.erase(oldest);
 }
 const float horizontal=std::sqrt(dx*dx+dy*dy);
 const float length=std::sqrt(dx*dx+dy*dy+dz*dz);
 const float steepness=length>.001f?std::abs(dz)/length:1.0f;
 if(horizontal>.001f){dx/=horizontal;dy/=horizontal;}else{dx=1;dy=0;}
 s.splashes.push_back({surface,polygon,sprintathon_projectile_render_tick(),rocket,bullet,dx,dy,steepness,-1,grenade});
 return true;
}
// Keep the original effect alive for animation sounds and normal cleanup.
inline void replaceSplashSprite(short effect){
 if(effect>=0 && splashesEnabled() && std::find(state().splashSprites.begin(),state().splashSprites.end(),effect)==state().splashSprites.end())state().splashSprites.push_back(effect);
}
inline bool replacesSplash(short effect){
 const auto& sprites=state().splashSprites;
 return effect>=0 && splashesEnabled() && std::find(sprites.begin(),sprites.end(),effect)!=sprites.end();
}
// Bind the effect callback to the splash emitted by this exact impact.
inline int& pendingSplashIndex(){static int index=-1;return index;}
struct ScopedSplashBinding {
 int previous;
 explicit ScopedSplashBinding(bool emitted):previous(pendingSplashIndex()){
  pendingSplashIndex()=emitted && !state().splashes.empty()?int(state().splashes.size()-1):-1;
 }
 ~ScopedSplashBinding(){pendingSplashIndex()=previous;}
 ScopedSplashBinding(const ScopedSplashBinding&)=delete;
 ScopedSplashBinding& operator=(const ScopedSplashBinding&)=delete;
};
// Called for every new effect, including delayed and scenario-remapped splashes.
inline void observeLiquidEffect(short type,short effect,short polygon,const world_point3d& position){
 if(!splashesEnabled() || effect<0)return;
 const int pending=pendingSplashIndex();
 if(pending>=0 && pending<int(state().splashes.size())){
  state().splashes[pending].effect=effect;
  replaceSplashSprite(effect);
  return;
 }
 if(polygon<0)return;
 sync();
 const short media_index=get_polygon_data(polygon)->media_index;
 if(media_index<0)return;
 const auto* liquid=get_media_data(media_index);
 if(!liquid || liquid->type==_media_lava || std::abs(int(position.z)-int(liquid->height))>WORLD_ONE/16)return;
 bool matched=false,small=false;
 for(short kind=0;kind<NUMBER_OF_MEDIA_DETONATION_TYPES;++kind){
  short mapped=-1;get_media_detonation_effect(media_index,kind,&mapped);
  if(mapped>=0 && mapped==type){matched=true;small=kind==_small_media_detonation_effect;break;}
 }
 if(!matched)return;
 // Reuse the directional event already emitted by the projectile impact.
 for(auto it=state().splashes.rbegin();it!=state().splashes.rend();++it){
  if(it->effect<0 && it->start==sprintathon_projectile_render_tick() &&
     it->position.x==position.x && it->position.y==position.y){
   it->effect=effect;replaceSplashSprite(effect);return;
  }
 }
 world_point3d surface=position;surface.z=liquid->height;
 if(emitSplash(surface,polygon,false,small)){
  state().splashes.back().effect=effect;replaceSplashSprite(effect);
 }
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
 auto& sprites=state().splashSprites;
 sprites.erase(std::remove(sprites.begin(),sprites.end(),effect),sprites.end());
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
