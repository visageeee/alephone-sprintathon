#ifndef SPRINTATHON_COMBAT_MASS_H
#define SPRINTATHON_COMBAT_MASS_H
#include "monsters.h"
#include <algorithm>
#include <cmath>

namespace SprintathonCombatMass {
// Gameplay resistance, not kilograms. Unknown/scenario types use body size.
inline double mass(short type,double radius,double height,double unit) {
    switch(type){
        case _monster_fighter_minor: case _monster_fighter_major:
        case _monster_fighter_minor_projectile: case _monster_fighter_major_projectile:
        case _monster_tiny_fighter: return .7;
        case _civilian_crew: case _civilian_science: case _civilian_security:
        case _civilian_assimilated: case _civilian_fusion_crew: case _civilian_fusion_science:
        case _civilian_fusion_security: case _civilian_fusion_assimilated:
        case _monster_tiny_bob: return .8;
        case _monster_trooper_minor: case _monster_trooper_major: return 1.5;
        case _monster_hunter_minor: case _monster_hunter_major:
        case _monster_mother_of_all_hunters: return 5.0;
        case _monster_cyborg_minor: case _monster_cyborg_major:
        case _monster_cyborg_flame_minor: case _monster_cyborg_flame_major:
        case _monster_mother_of_all_cyborgs: return 8.0;
        case _monster_juggernaut_minor: case _monster_juggernaut_major: return 100.0;
        default: return std::max(.75,std::min(3.0,(radius/(unit/3.0))*(height/unit)));
    }
}
inline double damageScale(double mass,bool blast) {
    return std::min(blast?3.0:1.6,1.0/mass);
}
struct Motion {double x,y,z;};
inline Motion combine(double oldX,double oldY,double oldZ,double addX,double addY,double addZ,
    double horizontalLimit,double verticalLimit) {
    Motion result{oldX+addX,oldY+addY,std::max(-verticalLimit,std::min(verticalLimit,oldZ+addZ))};
    const double length=std::hypot(result.x,result.y);
    if(length>horizontalLimit){result.x*=horizontalLimit/length;result.y*=horizontalLimit/length;}
    return result;
}
}
#endif
