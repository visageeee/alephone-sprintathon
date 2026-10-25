#ifndef SPRINTATHON_SHOCKWAVE_EVENTS_H
#define SPRINTATHON_SHOCKWAVE_EVENTS_H
#include "map.h"
#include "projectiles.h"
#include "effects.h"
#include <vector>
namespace SprintathonShockwaveEvents {
struct Wave {world_point3d position;long tick;bool rocket;uint64_t motionTick;};
struct Queue {std::vector<Wave> waves;int level=-1;long tick=-1;};
inline Queue& queue(){static Queue q;return q;}
inline void sync(){
 auto& q=queue();
 if(q.level!=dynamic_world->current_level_number || dynamic_world->tick_count<q.tick)q.waves.clear();
 q.level=dynamic_world->current_level_number;q.tick=dynamic_world->tick_count;
}
inline void emit(short type,short effect,const world_point3d& position){
 const bool rocket=type==_projectile_rocket || effect==_effect_rocket_explosion;
 const bool grenade=type==_projectile_grenade || effect==_effect_grenade_explosion;
 if(!rocket && !grenade)return;
 sync();auto& q=queue();if(q.waves.size()>=8)q.waves.erase(q.waves.begin());
 q.waves.push_back({position,dynamic_world->tick_count,rocket,sprintathon_projectile_render_tick()});
}
}
#endif
