/*
PHYSICS.C

	Copyright (C) 1991-2001 and beyond by Bungie Studios, Inc.
	and the "Aleph One" developers.
 
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html

Wednesday, May 11, 1994 9:32:16 AM

Saturday, May 21, 1994 11:36:31 PM
	missing effects due to map (i.e., gravity and collision detection).  last day in san
	jose after WWDC.
Sunday, May 22, 1994 11:14:55 AM
	there are two viable methods of running a synchronized network game.  the first is doom's,
	where each player shares with each other player only his control information for that tick
	(this imposes a maximum frame rate, as the state-of-the-world will be advanced at the same
	time on all machines).  the second is the continuous lag-tolerant model where each player
	shares absolute information with each other player as often as possible and local machines
	do their best at guessing what everyone else in the game is doing until they get better
	information.  whichever choice is made will change the physics drastically.  we're going to
	take the latter approach, and cache the KeyMap at interrupt time to be batch-processed
	later at frame time.

Feb. 4, 2000 (Loren Petrich):
	Changed halt() to assert(false) for better debugging

Feb 6, 2000 (Loren Petrich):
	Added access to size of physics-definition structure

Feb 20, 2000 (Loren Petrich):
	Fixed chase-cam behavior: DROP_DEAD_HEIGHT is effectively zero for it.
	Also, set up-and-down bob to zero when it is active.

Aug 31, 2000 (Loren Petrich):
	Added stuff for unpacking and packing

May 16, 2002 (Woody Zenfell):
    Letting user decide whether to auto-recenter when running

 June 14, 2003 (Woody Zenfell):
	update_player_physics_variables() can now operate in a reduced-impact mode
		that changes less of the game state.  Useful for partial-game-state
		save-and-restore code (as used by prediction mechanism).
*/

/*
running backwards shouldn’t mean doom in a fistfight

//who decides on the physics model, anyway?  static_world-> or player->
//falling through gridlines and crapping on elevators has to do with variables->flags being wrong after the player dies
//absolute (or nearly-absolute) positioning information for yaw, pitch and velocity
//the physics model is too soft (more noticable at high frame rates)
//we can continually boot ourselves out of nearly-orthogonal walls by tiny amounts, resulting in a slide
//it’s fairly obvious that players can still end up in walls
//the recenter key should work faster
*/

#ifdef DEBUG
//#define DIVERGENCE_CHECK
#endif

#include "cseries.h"
#include "render.h"
#include "map.h"
#include "player.h"
#include "interface.h"
#include "monsters.h"
#include "scenery.h"
#include "preferences.h"
#include "projectiles.h"

#define DONT_REPEAT_DEFINITIONS
#include "monster_definitions.h"

#include "media.h"

// LP addition:
#include "ChaseCam.h"
#include "Packing.h"

#include <string.h>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <vector>
#include "vbl.h"
#include "../RenderMain/SprintathonTerrainRuntime.h"

/* ---------- constants */

#define COEFFICIENT_OF_ABSORBTION 2
#define SMALL_ENOUGH_VELOCITY (constants->climbing_acceleration)
#define CLOSE_ENOUGH_TO_FLOOR WORLD_TO_FIXED(WORLD_ONE/16)

#define AIRBORNE_HEIGHT WORLD_TO_FIXED(WORLD_ONE/16)

#define DROP_DEAD_HEIGHT WORLD_TO_FIXED(WORLD_ONE_HALF)

#define FLAGS_WHICH_PREVENT_RECENTERING (_turning|_looking|_sidestepping|_looking_vertically|_look_dont_turn|_sidestep_dont_turn)

/* ---------- private prototypes */

/* needed for set_player_position -SB */
#include "SprintathonZeroG.h"

/*static*/ struct physics_constants *get_physics_constants_for_model(short physics_model, uint32 action_flags);
// Preserve tangential movement at character contact instead of cancelling XY.
// Only the player moves here; heavy character yielding remains in push_character.
static bool sprintathon_slide_player(short player_index, short blocker,
    world_point3d& next, world_distance height, world_distance& floor,
    world_distance& ceiling, short& supporting, world_distance& ledge)
{
    const auto* player = get_player_data(player_index);
    if (!input_preferences->sprintathon_enabled || !input_preferences->sprintathon_enhanced_enemy_ai ||
        game_is_networked || game_is_being_replayed() || PLAYER_IS_DEAD(player) || blocker == NONE)
        return false;
    const auto* obstacle = get_object_data(blocker);
    if (GET_OBJECT_OWNER(obstacle) != _object_is_monster ||
        MONSTER_IS_PLAYER(get_monster_data(obstacle->permutation))) return false;
    const auto* monster = get_monster_data(player->monster_index);
    const auto* body = get_object_data(monster->object_index);
    double vx = double(next.x)-body->location.x, vy = double(next.y)-body->location.y;
    const double limit = std::sqrt(vx*vx+vy*vy);
    if (limit < 1) return false;
    world_distance radius, collision_height;
    get_monster_dimensions(player->monster_index, &radius, &collision_height);
    struct Contact { double nx, ny, clearance; };
    std::vector<Contact> contacts;
    std::vector<short> nearby;
    possible_intersecting_monsters(&nearby, LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE, body->polygon, true);
    for (short index : nearby) {
        if (index == monster->object_index) continue;
        const auto* peer = get_object_data(index);
        world_distance peer_radius, peer_height;
        if (GET_OBJECT_OWNER(peer) == _object_is_monster)
            get_monster_dimensions(peer->permutation, &peer_radius, &peer_height);
        else if (GET_OBJECT_OWNER(peer) == _object_is_scenery)
            get_scenery_dimensions(peer->permutation, &peer_radius, &peer_height);
        else continue;
        if (next.z+collision_height <= peer->location.z || next.z >= int(peer->location.z)+peer_height) continue;
        double nx = double(body->location.x)-peer->location.x;
        double ny = double(body->location.y)-peer->location.y;
        double distance = std::sqrt(nx*nx+ny*ny);
        const double separation = int(radius)+peer_radius;
        if (distance > separation+limit+2) continue;
        if (distance < 1) { nx = monster->object_index < index ? 1 : -1; ny = 0; distance = 1; }
        nx /= distance; ny /= distance;
        const double clearance = distance < separation ?
            std::min(limit*0.1, (separation-distance)*0.2+1) : separation-distance+1;
        contacts.push_back({nx, ny, clearance});
        if (index == blocker) {
            const double tangent = -vx*ny+vy*nx;
            // Keep intentional sideways input. Direct pressure gets only a
            // small passing bias, rather than a full-speed automatic dodge.
            if (std::abs(tangent) < limit*0.2) {
                const int side = tangent < -0.01 ? -1 : tangent > 0.01 ? 1 :
                    ((obstacle->permutation & 1) ? 1 : -1);
                const double extra = side*limit*0.2-tangent;
                vx -= ny*extra; vy += nx*extra;
            }
        }
    }
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (const auto& c : contacts) {
            const double into = c.clearance-(vx*c.nx+vy*c.ny);
            if (into > 0.01) { vx += c.nx*into; vy += c.ny*into; changed = true; }
        }
        const double length = std::sqrt(vx*vx+vy*vy);
        if (length > limit) { vx *= limit/length; vy *= limit/length; }
        if (!changed) break;
    }
    const double x = body->location.x+vx, y = body->location.y+vy;
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) return false;
    world_point3d candidate = next;
    candidate.x = static_cast<world_distance>(std::round(x));
    candidate.y = static_cast<world_distance>(std::round(y));
    world_distance new_floor = floor, new_ceiling = ceiling, new_ledge = ledge;
    short new_supporting = supporting;
    keep_line_segment_out_of_walls(body->polygon, &get_object_data(monster->object_index)->location,
        &candidate, WORLD_ONE/3, height, &new_floor, &new_ceiling, &new_supporting, &new_ledge);
    // Wall clipping can push a slide back into a character: check again.
    world_distance object_floor = INT16_MIN;
    if (legal_player_move(player->monster_index, &candidate, &object_floor) != NONE ||
        (candidate.x == body->location.x && candidate.y == body->location.y)) return false;
    next = candidate; floor = new_floor; ceiling = new_ceiling;
    supporting = new_supporting; ledge = new_ledge;
    return true;
}

/*static*/ void instantiate_physics_variables(struct physics_constants *constants, struct physics_variables *variables, short player_index, bool first_time, bool take_action);
static void physics_update(struct physics_constants *constants, struct physics_variables *variables, struct player_data *player, uint32 action_flags);

/* ---------- globals */

/* import constants, structures and globals for physics models */
#include "physics_models.h"

/* ---------- code */

#ifdef DIVERGENCE_CHECK
#define SAVED_POINT_COUNT 8192
static world_point3d *saved_points;
static angle *saved_thetas;
static short saved_point_count, saved_point_iterations= 0;
static bool saved_divergence_warning;
#endif

static struct physics_constants physics_models[NUMBER_OF_PHYSICS_MODELS];
static fixed_yaw_pitch vir_aim_delta = {0, 0};

static constexpr int SPRINTATHON_BULLET_TIME_PERCENT= 35;

static _fixed sprintathon_scale_for_bullet_time(_fixed value)
{
	return static_cast<_fixed>(
		(static_cast<int64_t>(value)*SPRINTATHON_BULLET_TIME_PERCENT)/100);
}

static constexpr _fixed EXPERIMENTAL_MAXIMUM_ELEVATION =
	(QUARTER_CIRCLE * FIXED_ONE * 2) / 3; // 60 degrees

static _fixed sprintathon_mouselook_limit(const physics_constants *constants)
{
	switch (input_preferences->sprintathon_mouselook_mode)
	{
		case 1: return (QUARTER_CIRCLE * FIXED_ONE) / 2;       // 45 degrees
		case 2: return EXPERIMENTAL_MAXIMUM_ELEVATION;         // 60 degrees
		case 3: return (QUARTER_CIRCLE * FIXED_ONE * 5) / 6;   // 75 degrees
		case 4: return QUARTER_CIRCLE * FIXED_ONE - FIXED_ONE; // about 89.3 degrees
		default: return constants->maximum_elevation;           // scenario/original limit
	}
}

/* every other field in the player structure should be valid when this call is made */
void initialize_player_physics_variables(
	short player_index)
{
	if (player_index == local_player_index)
    { SprintathonZeroG::reset(); resync_virtual_aim(); }
	
	struct player_data *player= get_player_data(player_index);
	struct monster_data *monster= get_monster_data(player->monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	struct physics_variables *variables= &player->variables;
	struct physics_constants *constants= get_physics_constants_for_model(static_world->physics_model, 0);

//#ifdef DEBUG
	obj_set(*variables, 0x80);
//#endif

	variables->head_direction= 0;
	variables->adjusted_yaw= variables->direction= INTEGER_TO_FIXED(object->facing);
	variables->adjusted_pitch= variables->elevation= 0;
	variables->angular_velocity= variables->vertical_angular_velocity= 0;
	variables->velocity= 0, variables->perpendicular_velocity= 0;
	variables->position.x= WORLD_TO_FIXED(object->location.x);
	variables->position.y= WORLD_TO_FIXED(object->location.y);
	variables->position.z= WORLD_TO_FIXED(object->location.z);
	variables->last_position= variables->position;
	variables->last_direction= variables->direction;
	/* .floor_height, .ceiling_height and .media_height will be calculated by instantiate, below */

	variables->external_angular_velocity= 0;
	variables->external_velocity.i= variables->external_velocity.j= variables->external_velocity.k= 0;
	variables->wall_push_i= variables->wall_push_j= 0;
	variables->ledge_height= INT16_MAX;
	variables->actual_height= constants->height;
	variables->jump_grace_ticks= 0;
	player->jump_buffer_ticks= 0;
	player->dodge_last_direction= 0;
	player->dodge_tap_window= 0;
	player->dodge_key_was_down= false;
	player->dodge_command_was_down= false;
	player->dodge_ticks_remaining= 0;
	player->dodge_bullet_time_phase= 0;
	player->back_dodge_recovery_ticks= 0;
	player->dodge_auto_bullet_time= false;
	player->cartwheel_requested= false;
	player->cartwheel_active= false;
	player->cartwheel_direction= 0;
	player->cartwheel_ticks_remaining= 0;
	player->cartwheel_camera_roll= 0;
	player->backflip_requested= false;
	player->backflip_active= false;
	player->backflip_ticks_remaining= 0;
	player->backflip_camera_pitch= 0;
	player->scoped_pistol_recoil_ticks= 0;
	player->crouch_key_was_down= false;
	player->reload_key_was_down= false;
	player->slide_punch_pending= false;
	player->slide_ticks_remaining= 0;
	player->slide_roll_tap_window= 0;
	player->slide_roll_ticks_remaining= 0;
	player->slide_roll_fraction= 0;
	player->slide_time_fraction= 0;
	player->slide_roll_forward_was_down= false;
	player->slide_roll_used= false;
	player->slide_roll_queued= false;
	player->slide_recovery_ticks= 0;
	player->flying_kick_active= false;
	player->flying_kick_requested= false;
	player->flying_kick_ticks= 0;
	player->flying_kick_landing_ticks= 0;
	player->flying_kick_exit_ticks= 0;
	player->flying_kick_recovery_pending= false;
	player->wall_kick_rearm_pending= false;
	player->wall_kick_cooldown_ticks= 0;
	player->wall_run_jump_cooldown_ticks= 0;
	player->flying_kick_oxygen_recharge_delay= 0;
	player->footstep_ticks_remaining= 0;
	player->footstep_alternate= false;
	player->sprintathon_camera_roll= 0;
	player->sprintathon_camera_pitch= 0;
	player->sprintathon_strafe_roll= 0;
	player->sprintathon_strafe_phase= 0;
	
	variables->step_phase= 0;
	variables->step_amplitude= 0;
	
	variables->action= _player_stationary;
	variables->old_flags= variables->flags= 0; /* not recentering, not above ground, not below ground (i.e., on floor) */

	/* setup shadow variables in player_data structure */
	instantiate_physics_variables(get_physics_constants_for_model(static_world->physics_model, 0),
		&player->variables, player_index, true, true);

#ifdef DIVERGENCE_CHECK
	if (!saved_point_iterations)
	{
		saved_points= new world_point3d[SAVED_POINT_COUNT];
		saved_thetas= new angle[SAVED_POINT_COUNT];
	}
	saved_point_count= 0;
	saved_point_iterations+= 1;
	saved_divergence_warning= false;
#endif
}

// Read-only short collision probes. Ordinary portals do not provide a
// foothold: only a surface that blocks the player's collision body can kick.
static SprintathonZeroGMath::V sprintathon_zero_g_contact(player_data* player,SprintathonZeroGMath::V toward)
{
    using SprintathonZeroGMath::V;
    auto& variables=player->variables;
    auto* object=get_object_data(get_monster_data(player->monster_index)->object_index);
    const double reach=WORLD_ONE/8.0;
    double closest=reach+1;V normal{};
    auto consider=[&](double gap,V n){
        if(n.dot(toward)<-.65 && gap>=-2 && gap<=reach && gap<closest){closest=gap;normal=n;}
    };
    consider(object->location.z-FIXED_TO_WORLD(variables.floor_height),{0,0,1});
    consider(FIXED_TO_WORLD(variables.ceiling_height-variables.actual_height)-object->location.z,{0,0,-1});
    for(int i=0;i<8;++i){
        const double angle=i*SprintathonZeroGMath::pi/4;
        const double dx=std::cos(angle)*reach,dy=std::sin(angle)*reach;
        const int x=int(object->location.x)+int(std::lround(dx));
        const int y=int(object->location.y)+int(std::lround(dy));
        if(x<INT16_MIN || x>INT16_MAX || y<INT16_MIN || y>INT16_MAX)continue;
        world_point3d target=object->location;
        target.x=static_cast<world_distance>(x);target.y=static_cast<world_distance>(y);
        const world_point3d intended=target;
        world_distance floor,ceiling;short support;
        keep_line_segment_out_of_walls(object->polygon,&object->location,&target,
            WORLD_ONE/3,FIXED_TO_WORLD(variables.actual_height),&floor,&ceiling,&support);
        V correction{double(target.x)-intended.x,double(target.y)-intended.y,0};
        const double length=correction.length();
        if(length>1){
            V n=correction*(1/length);
            const double approach=-(dx*n.x+dy*n.y);
            if(approach>1)consider(std::max(0.0,approach-length),n);
        }
    }
    if(SprintathonZeroG::debug())std::fprintf(stderr,
        "ZeroG contact: toward=(%.3f,%.3f,%.3f) normal=(%.3f,%.3f,%.3f) gap=%.1f pos=(%d,%d,%d) floor=%d ceiling=%d body=%d\n",
        toward.x,toward.y,toward.z,normal.x,normal.y,normal.z,closest,
        int(object->location.x),int(object->location.y),int(object->location.z),
        int(FIXED_TO_WORLD(variables.floor_height)),int(FIXED_TO_WORLD(variables.ceiling_height)),int(FIXED_TO_WORLD(variables.actual_height)));
    return normal;
}

void update_player_physics_variables(
	short player_index,
	uint32 action_flags,
	bool predictive)
{
	struct player_data *player= get_player_data(player_index);
	struct physics_variables *variables= &player->variables;
	struct physics_constants *constants= get_physics_constants_for_model(static_world->physics_model, action_flags);
	struct physics_constants slowed_constants;
    const bool zeroTrace=player_index==local_player_index && SprintathonZeroG::traceFrame(action_flags);
    if(zeroTrace)std::fprintf(stderr,
        "ZeroG before: tick=%ld mode=%d active=%d predictive=%d flags=%08lx jump=%d crouch=%d jumpHeld=%d crouchHeld=%d ready=%d pos=(%.4f,%.4f,%.4f) vel=(%.5f,%.5f,%.5f) body=%.4f fixedBody=%.4f\n",
        long(dynamic_world->tick_count),SprintathonZeroG::mode(),int(SprintathonZeroG::active(player)),int(predictive),
        static_cast<unsigned long>(action_flags),int(bool(action_flags&_swim)),int(bool(action_flags&_microphone_button)),
        int(SprintathonZeroG::state().jumpHeld),int(SprintathonZeroG::state().crouchHeld),int(SprintathonZeroG::state().ready),
        variables->position.x/double(FIXED_ONE),variables->position.y/double(FIXED_ONE),variables->position.z/double(FIXED_ONE),
        variables->external_velocity.i/double(FIXED_ONE),variables->external_velocity.j/double(FIXED_ONE),variables->external_velocity.k/double(FIXED_ONE),
        variables->actual_height/double(FIXED_ONE),SprintathonZeroG::state().bodyHeight/double(FIXED_ONE));

	if (sprintathon_bullet_time_active())
	{
		/* Keep velocity limits and view rotation unchanged. Scaling the rates
		 * and displacement instead gives physics a fractional dt while input
		 * and camera sampling continue at the normal 30 Hz cadence. */
		slowed_constants= *constants;
		slowed_constants.acceleration=
			sprintathon_scale_for_bullet_time(constants->acceleration);
		slowed_constants.deceleration=
			sprintathon_scale_for_bullet_time(constants->deceleration);
		slowed_constants.airborne_deceleration=
			sprintathon_scale_for_bullet_time(constants->airborne_deceleration);
		slowed_constants.gravitational_acceleration=
			sprintathon_scale_for_bullet_time(constants->gravitational_acceleration);
		slowed_constants.climbing_acceleration=
			sprintathon_scale_for_bullet_time(constants->climbing_acceleration);
		slowed_constants.external_deceleration=
			sprintathon_scale_for_bullet_time(constants->external_deceleration);
		slowed_constants.step_delta=
			sprintathon_scale_for_bullet_time(constants->step_delta);
		constants= &slowed_constants;
	}

    if(SprintathonZeroG::active(player)){
        SprintathonZeroG::update(player,action_flags,constants,
            sprintathon_bullet_time_active()?sprintathon_scale_for_bullet_time(FIXED_ONE)/double(FIXED_ONE):1.0,
            sprintathon_zero_g_contact);
        if(SprintathonZeroG::state().kickedThisTick)
            sprintathon_play_wall_kick_sound(player->monster_index);
    }
    else physics_update(constants, variables, player, action_flags);
    if(zeroTrace)std::fprintf(stderr,
        "ZeroG integrated: pos=(%.4f,%.4f,%.4f) vel=(%.5f,%.5f,%.5f) kick=%d pose=%d poseTicks=%d exitTicks=%d\n",
        variables->position.x/double(FIXED_ONE),variables->position.y/double(FIXED_ONE),variables->position.z/double(FIXED_ONE),
        variables->external_velocity.i/double(FIXED_ONE),variables->external_velocity.j/double(FIXED_ONE),variables->external_velocity.k/double(FIXED_ONE),
        int(SprintathonZeroG::state().kickedThisTick),int(player->flying_kick_active),int(player->flying_kick_ticks),int(player->flying_kick_exit_ticks));
	instantiate_physics_variables(constants, variables, player_index, false, !predictive);
    if(zeroTrace)std::fprintf(stderr,
        "ZeroG resolved: pos=(%.4f,%.4f,%.4f) vel=(%.5f,%.5f,%.5f) camera=(%d,%d,%d) body=%.4f floor=%.4f ceiling=%.4f flags=%04x\n",
        variables->position.x/double(FIXED_ONE),variables->position.y/double(FIXED_ONE),variables->position.z/double(FIXED_ONE),
        variables->external_velocity.i/double(FIXED_ONE),variables->external_velocity.j/double(FIXED_ONE),variables->external_velocity.k/double(FIXED_ONE),
        int(player->camera_location.x),int(player->camera_location.y),int(player->camera_location.z),
        variables->actual_height/double(FIXED_ONE),variables->floor_height/double(FIXED_ONE),variables->ceiling_height/double(FIXED_ONE),unsigned(variables->flags));


#ifdef DIVERGENCE_CHECK
	if (saved_point_count<SAVED_POINT_COUNT)
	{
		struct object_data *object= get_object_data(get_monster_data(player->monster_index)->object_index);
		world_point3d p= object->location;
		world_point3d *q= saved_points+saved_point_count;
		angle *facing= saved_thetas+saved_point_count;
		
		if (saved_point_iterations==1)
		{
			saved_points[saved_point_count]= p;
			*facing= object->facing;
		}
		else
		{
			if (p.x!=q->x||p.y!=q->y||p.z!=q->z||*facing!=object->facing&&!saved_divergence_warning)
			{
				dprintf("divergence @ tick %d: (%d,%d,%d,%d)!=(%d,%d,%d,%d)", saved_point_count,
					q->x, q->y, q->z, *facing, p.x, p.y, p.z, object->facing);
				saved_divergence_warning= true;
			}
		}
		
		saved_point_count+= 1;
	}
#endif
}

void adjust_player_for_polygon_height_change(
	short monster_index,
	short polygon_index,
	world_distance new_floor_height,
	world_distance new_ceiling_height)
{
	short player_index= monster_index_to_player_index(monster_index);
	struct player_data *player= get_player_data(player_index);
	struct physics_variables *variables= &player->variables;
	struct polygon_data *polygon= get_polygon_data(polygon_index);
	world_distance old_floor_height= polygon->floor_height;

	(void) (new_ceiling_height);

	if (player->supporting_polygon_index==polygon_index)
	{
		if (FIXED_TO_WORLD(variables->position.z)<=old_floor_height) /* must be <= */
		{
			variables->floor_height= variables->position.z= WORLD_TO_FIXED(new_floor_height);
			if (film_profile.fix_sliding_on_platforms && variables->external_velocity.k < 0) 
			{
				variables->external_velocity.k = 0;
			}

			if (PLAYER_IS_DEAD(player)) variables->external_velocity.k= 0;
		}
	}
}

void accelerate_player(
	short monster_index,
	world_distance vertical_velocity,
	angle direction,
	world_distance velocity)
{
	short player_index= monster_index_to_player_index(monster_index);
	struct player_data *player= get_player_data(player_index);
	struct physics_variables *variables= &player->variables;
	struct physics_constants *constants= get_physics_constants_for_model(static_world->physics_model, 0);

	variables->external_velocity.k+= WORLD_TO_FIXED(vertical_velocity);
	variables->external_velocity.k= PIN(variables->external_velocity.k, -constants->terminal_velocity, constants->terminal_velocity);
	
	if (get_monster_definition_external(_monster_marine)->flags & _monster_can_grenade_climb)
	{
		variables->external_velocity.i= (cosine_table[direction]*velocity)>>(TRIG_SHIFT+WORLD_FRACTIONAL_BITS-FIXED_FRACTIONAL_BITS);
		variables->external_velocity.j= (sine_table[direction]*velocity)>>(TRIG_SHIFT+WORLD_FRACTIONAL_BITS-FIXED_FRACTIONAL_BITS);
	} else {
		variables->external_velocity.i+= (cosine_table[direction]*velocity)>>(TRIG_SHIFT+WORLD_FRACTIONAL_BITS-FIXED_FRACTIONAL_BITS);
		variables->external_velocity.j+= (sine_table[direction]*velocity)>>(TRIG_SHIFT+WORLD_FRACTIONAL_BITS-FIXED_FRACTIONAL_BITS);
	}	
}

void get_absolute_pitch_range(
	_fixed *minimum,
	_fixed *maximum)
{
	struct physics_constants *constants= get_physics_constants_for_model(static_world->physics_model, 0);
	const _fixed limit = sprintathon_mouselook_limit(constants);
	*minimum= -limit;
	*maximum= limit;
}

void kill_player_physics_variables(
	short player_index)
{
}

/* return a number in [-FIXED_ONE,FIXED_ONE] (arguably) */
_fixed get_player_forward_velocity_scale(
	short player_index)
{
	struct player_data *player= get_player_data(player_index);
	struct physics_variables *variables= &player->variables;
	struct physics_constants *constants= get_physics_constants_for_model(static_world->physics_model, _run_dont_walk);
	_fixed dx= variables->position.x - variables->last_position.x;
	_fixed dy= variables->position.y - variables->last_position.y;

	return INTEGER_TO_FIXED(((dx*cosine_table[FIXED_INTEGERAL_PART(variables->direction)] +
		dy*sine_table[FIXED_INTEGERAL_PART(variables->direction)])>>TRIG_SHIFT))/constants->maximum_forward_velocity;
}

fixed_yaw_pitch virtual_aim_delta()
{
	return vir_aim_delta;
}

void resync_virtual_aim()
{
	vir_aim_delta = {0, 0};
}

static uint32 process_aim_input_with_residual(uint32 action_flags, fixed_yaw_pitch delta, fixed_yaw_pitch& residual)
{
	// Classic behavior modes
	const bool classic_precision = !input_preferences->extra_mouse_precision;
	const bool classic_limits = input_preferences->classic_aim_speed_limits;
	
	// Classic precision behavior:
	// - round magnitudes within (0, FIXED_ONE) to FIXED_ONE
	// - round toward zero instead of nearest
	// - lock virtual aim to physical aim
	
	auto clamp = [classic_limits](fixed_angle theta, int encoding_bits) -> fixed_angle
	{
		const angle encoding_bias = (1<<encoding_bits)/2;
		const angle encoding_limit = encoding_bias - 1; // encoding supports [-bias, bias-1] but we want +/- symmetry
		const angle limit = classic_limits ? encoding_bias/2 : encoding_limit;
		return A1_PIN(theta, -limit*FIXED_ONE, limit*FIXED_ONE);
	};
	
	auto round = [classic_precision](fixed_angle theta) -> angle
	{
		return classic_precision ?
			SGN(theta) * std::max<angle>(1, std::abs(theta/FIXED_ONE)) :
			(theta + SGN(theta)*FIXED_ONE/2) / FIXED_ONE;
	};
	
	auto encode = [](angle theta, int encoding_bits) -> uint32
	{
		return (theta + (1<<encoding_bits)/2) & ((1<<encoding_bits) - 1);
	};
	
	const physics_variables& local_phys = local_player->variables;
	
	// The delta from the current physical aim to the requested virtual aim
	const fixed_yaw_pitch full_delta = {delta.yaw + residual.yaw, delta.pitch + residual.pitch};
	
	// Process yaw input
	if (!(action_flags & _override_absolute_yaw))
	{
		const fixed_angle target = clamp(full_delta.yaw, ABSOLUTE_YAW_BITS); // the high-precision target delta
		const angle payload = round(target); // the low-precision delta to be encoded
		
		if (payload || local_phys.angular_velocity)
			action_flags = SET_ABSOLUTE_YAW(action_flags, encode(payload, ABSOLUTE_YAW_BITS)) | _absolute_yaw_mode;
		
		// Update virtual yaw
		auto residual_limit = (FIXED_ONE / 2) - 1;
		residual.yaw = classic_precision ? 0 : std::clamp(target - payload * FIXED_ONE, -residual_limit, residual_limit);
		assert(std::abs(residual.yaw) <= residual_limit);
	}
	
	// Explicit and automatic recentering do not occur under absolute pitch mode; therefore we
	// 1) try to always use absolute pitch mode if the user doesn't want auto-recentering; and
	// 2) always avoid absolute pitch mode while an explicit recentering operation is in progress
	// (pitch control is necessarily locked out until the recentering completes; no way to cancel)
	
	const bool explicitly_recentering = local_phys.flags & _RECENTERING_BIT;
	
	// Process pitch input
	if (!(action_flags & _override_absolute_pitch) && !explicitly_recentering)
	{
		const fixed_angle target = clamp(full_delta.pitch, ABSOLUTE_PITCH_BITS); // the high-precision target delta
		const angle payload = round(target); // the low-precision delta to be encoded
		
		if (payload || local_phys.vertical_angular_velocity || dont_auto_recenter())
			action_flags = SET_ABSOLUTE_PITCH(action_flags, encode(payload, ABSOLUTE_PITCH_BITS)) | _absolute_pitch_mode;
		
		// Update virtual pitch
		auto residual_limit = (FIXED_ONE / 2) - 1;
		residual.pitch = classic_precision ? 0 : std::clamp(target - payload * FIXED_ONE, -residual_limit, residual_limit);
		assert(std::abs(residual.pitch) <= residual_limit);
	}
	
	return action_flags;
}


uint32 process_aim_input(uint32 action_flags, fixed_yaw_pitch delta)
{
    return process_aim_input_with_residual(action_flags, delta, vir_aim_delta);
}

static int sprintathon_curved_mouse_pitch(int raw_pitch)
{
    if (input_preferences->sprintathon_mouselook_mode == 0) return raw_pitch;
    const int magnitude = std::min(std::abs(raw_pitch), 15);
    const int excess = std::max(0, magnitude - 4);
    const int curved = magnitude + (excess ? (excess * excess * 48 + 60) / 121 : 0);
    return raw_pitch < 0 ? -curved : curved;
}

fixed_yaw_pitch preview_mouse_aim(fixed_yaw_pitch delta)
{
    fixed_yaw_pitch residual = vir_aim_delta;
    const uint32 flags = process_aim_input_with_residual(0, delta, residual);
    fixed_yaw_pitch result = {residual.yaw - vir_aim_delta.yaw, residual.pitch - vir_aim_delta.pitch};
    if (flags & _absolute_yaw_mode)
        result.yaw = (GET_ABSOLUTE_YAW(flags) - MAXIMUM_ABSOLUTE_YAW / 2) * FIXED_ONE
            + residual.yaw - vir_aim_delta.yaw;
    if (flags & _absolute_pitch_mode)
    {
        const auto constants = get_physics_constants_for_model(static_world->physics_model, 0);
        const fixed_angle maximum = sprintathon_mouselook_limit(constants);
        const fixed_angle min_pitch = FIXED_INTEGERAL_PART(-maximum) * FIXED_ONE;
        const fixed_angle max_pitch = FIXED_INTEGERAL_PART(maximum) * FIXED_ONE;
        const fixed_angle pitch = FIXED_INTEGERAL_PART(local_player->variables.elevation) * FIXED_ONE;
        const fixed_angle movement = sprintathon_curved_mouse_pitch(
            GET_ABSOLUTE_PITCH(flags) - MAXIMUM_ABSOLUTE_PITCH / 2) * FIXED_ONE;
        result.pitch = A1_PIN(pitch + movement + residual.pitch, min_pitch, max_pitch)
            - pitch - vir_aim_delta.pitch;
    }
    return result;
}

/* ---------- private code */

/*static*/ struct physics_constants *get_physics_constants_for_model(
	short physics_model,
	uint32 action_flags)
{
	struct physics_constants *constants;
	
	switch (physics_model)
	{
		case _editor_model:
		case _earth_gravity_model: constants= physics_models + ((action_flags&_run_dont_walk) ? _model_game_running : _model_game_walking); break;
		case _low_gravity_model:
			assert(false);
			break;
		default:
			assert(false);
			break;
	}
	
	return constants;
}

// Cosmetic-free, single-player pushing; no saved or network simulation state.
static void sprintathon_push_character(short player_index, short obstruction,
    const world_point3d& attempted, const physics_variables* variables, bool active)
{
    struct Pressure {
        short object = NONE, player_object = NONE;
        int tick = -1, ticks = 0, side = 1;
    };
    static Pressure pressure[MAXIMUM_NUMBER_OF_PLAYERS];
    auto& state = pressure[player_index];
    const auto* player = get_player_data(player_index);
    const auto* player_monster = get_monster_data(player->monster_index);
    const auto* body = get_object_data(player_monster->object_index);
    if (!active || !input_preferences->sprintathon_enabled ||
        !input_preferences->sprintathon_enhanced_enemy_ai || game_is_networked ||
        game_is_being_replayed() || PLAYER_IS_DEAD(player) || obstruction == NONE ||
        (!variables->velocity && !variables->perpendicular_velocity)) {
        state = Pressure{};
        return;
    }
    auto* object = get_object_data(obstruction);
    if (GET_OBJECT_OWNER(object) != _object_is_monster) { state = Pressure{}; return; }
    auto* target = get_monster_data(object->permutation);
    if (MONSTER_IS_PLAYER(target) || MONSTER_IS_DYING(target)) { state = Pressure{}; return; }
    const double dx = double(attempted.x)-body->location.x;
    const double dy = double(attempted.y)-body->location.y;
    const double distance = std::sqrt(dx*dx+dy*dy);
    if (distance < 1) { state = Pressure{}; return; }
    const int now = dynamic_world->tick_count;
    if (state.object != obstruction || state.player_object != player_monster->object_index ||
        now-state.tick != 1) {
        // Do not accumulate pressure or move a character twice in the same tick.
        if (state.object == obstruction && state.player_object == player_monster->object_index && state.tick == now) return;
        state = Pressure{};
        state.object = obstruction;
        state.player_object = player_monster->object_index;
        const double cross = dx*(double(object->location.y)-body->location.y)-
            dy*(double(object->location.x)-body->location.x);
        state.side = cross < 0 ? -1 : cross > 0 ? 1 : ((object->permutation & 1) ? 1 : -1);
    }
    state.tick = now;
    state.ticks = std::min(state.ticks+1, 6*TICKS_PER_SECOND);
    world_distance radius, height;
    get_monster_dimensions(object->permutation, &radius, &height);
    const double mass = std::max(0.75, std::min(3.0,
        (double(radius)/(WORLD_ONE/3.0))*(double(height)/WORLD_ONE)));
    const double ramp = std::min(1.0, state.ticks/(2.5*TICKS_PER_SECOND*std::sqrt(mass)));
    const double ease = ramp*ramp*(3.0-2.0*ramp);
    const double step = std::min(distance, (WORLD_ONE/1024.0 + WORLD_ONE/32.0*ease)/std::sqrt(mass));
    // Gradually yield to one side, allowing the player to pass without phasing.
    const double sideways = state.side*1.5*ease;
    const double scale = step/(distance*std::sqrt(1+sideways*sideways));
    world_point3d next = object->location;
    const int x = int(next.x)+int(std::lround((dx-dy*sideways)*scale));
    const int y = int(next.y)+int(std::lround((dy+dx*sideways)*scale));
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) return;
    next.x = x; next.y = y;
    world_distance floor = get_polygon_data(object->polygon)->floor_height;
    world_distance ceiling = get_polygon_data(object->polygon)->ceiling_height;
    short supporting = object->polygon;
    keep_line_segment_out_of_walls(object->polygon, &object->location, &next,
        0, height, &floor, &ceiling, &supporting);
    // Do not push characters into steps, low ceilings, or off ledges.
    if (next.z < floor || int(next.z)+height > ceiling ||
        (floor < get_polygon_data(object->polygon)->floor_height-WORLD_ONE/8)) return;
    world_distance object_floor = INT16_MIN;
    if (legal_player_move(object->permutation, &next, &object_floor) != NONE) return;
    const short old_polygon = object->polygon;
    if (translate_map_object(target->object_index, &next, NONE))
        monster_moved(object->permutation, old_polygon);
}

/*static*/ void instantiate_physics_variables(
	struct physics_constants *constants,
	struct physics_variables *variables,
	short player_index,
	bool first_time,
	bool take_action)
{
	struct player_data *player= get_player_data(player_index);
	struct monster_data *monster= get_monster_data(player->monster_index);
	struct object_data *legs= get_object_data(monster->object_index);
	struct object_data *torso= get_object_data(legs->parasitic_object);
	short old_polygon_index= legs->polygon;
	world_point3d new_location;
	world_distance adjusted_floor_height, adjusted_ceiling_height, object_floor;
	world_distance blocked_ledge_height= INT16_MAX;
	bool clipped;
	world_point3d attempted_location;
	_fixed step_height;
	angle facing, elevation;
	_fixed fixed_facing;

	/* convert to world coordinates before doing collision detection */
	new_location.x= FIXED_TO_WORLD(variables->position.x);
	new_location.y= FIXED_TO_WORLD(variables->position.y);
	new_location.z= FIXED_TO_WORLD(variables->position.z);

	/* check for 2d collisions with walls and knock the player back out of the wall (because of
		the way the physics updates work, we don’t worry about collisions with the floor or
		ceiling).  ONLY MODIFY THE PLAYER’S FIXED_POINT3D POSITION IF WE HAD A COLLISION */
	if (PLAYER_IS_DEAD(player)) new_location.z+= FIXED_TO_WORLD(DROP_DEAD_HEIGHT);
	if (take_action && !first_time && player->last_supporting_polygon_index!=player->supporting_polygon_index) changed_polygon(player->last_supporting_polygon_index, player->supporting_polygon_index, player_index);
	player->last_supporting_polygon_index= first_time ? NONE : player->supporting_polygon_index;
	attempted_location = new_location;
	clipped= keep_line_segment_out_of_walls(legs->polygon, &legs->location, &new_location,
		WORLD_ONE/3, FIXED_TO_WORLD(variables->actual_height), &adjusted_floor_height, &adjusted_ceiling_height,
		&player->supporting_polygon_index, &blocked_ledge_height);
	variables->ledge_height= blocked_ledge_height;
	if (PLAYER_IS_DEAD(player)) new_location.z-= FIXED_TO_WORLD(DROP_DEAD_HEIGHT);

	/* check for 2d collisions with solid objects and knock the player back out of the object.
		ONLY MODIFY THE PLAYER’S FIXED_POINT3D POSITION IF WE HAD A COLLISION. */
	object_floor= INT16_MIN;
	{
		short obstruction_index= legal_player_move(player->monster_index, &new_location, &object_floor);
        sprintathon_push_character(player_index, obstruction_index, new_location, variables,
            take_action && !first_time);
        if (obstruction_index != NONE && take_action && !first_time) {
            object_floor = INT16_MIN;
            obstruction_index = legal_player_move(player->monster_index, &new_location, &object_floor);
        }
        if (obstruction_index != NONE && take_action && !first_time &&
            sprintathon_slide_player(player_index, obstruction_index, new_location,
                FIXED_TO_WORLD(variables->actual_height), adjusted_floor_height, adjusted_ceiling_height,
                player->supporting_polygon_index, blocked_ledge_height)) {
            // Preserve contact activation even when we successfully slide past.
            bump_monster(player->monster_index, get_object_data(obstruction_index)->permutation);
            obstruction_index = NONE;
            object_floor = INT16_MIN;
            legal_player_move(player->monster_index, &new_location, &object_floor);
            variables->ledge_height = blocked_ledge_height;
            clipped = true; // copy corrected XY back to the fixed-point state
        }
		
		if (obstruction_index!=NONE)
		{
			struct object_data *object= get_object_data(obstruction_index);
			
			switch (GET_OBJECT_OWNER(object))
			{
				case _object_is_monster:
					if(take_action)
						bump_monster(player->monster_index, object->permutation);
				case _object_is_scenery:
					new_location.x= legs->location.x, new_location.y= legs->location.y;
					clipped= true;
					break;
				
				default:
					assert(false);
					break;
			}
		}
	}


    // Only player support changes here. Walls, objects, monsters and projectiles
    // keep their existing collision rules. Resolve XY before sampling the mesh.
    if(!PLAYER_IS_DEAD(player) && SprintathonTerrainRuntime::enabled()){
        const world_point3d previous_location=legs->location;
        short destination=legs->polygon;
        for(int guard=0;guard<dynamic_world->polygon_count;++guard){
            short edge=find_line_crossed_leaving_polygon(destination,&legs->location,&new_location);
            if(edge==NONE)break;
            short next=find_adjacent_polygon(destination,edge);
            if(next==NONE){destination=NONE;break;}
            destination=next;
            if(guard+1==dynamic_world->polygon_count)destination=NONE;
        }
        double mesh_floor=0;
        if(destination!=NONE && SprintathonTerrainRuntime::height(destination,new_location.x,new_location.y,mesh_floor)){
            world_distance terrain_floor=static_cast<world_distance>(std::lround(mesh_floor));
            // Preserve a genuinely higher adjacent support/solid object.
            if(player->supporting_polygon_index==destination || player->supporting_polygon_index==legs->polygon)
                adjusted_floor_height=terrain_floor;
            else adjusted_floor_height=MAX(adjusted_floor_height,terrain_floor);
            const world_distance support=MAX(adjusted_floor_height,object_floor);
            const world_distance body_height=FIXED_TO_WORLD(variables->actual_height);
            if(int(support)+body_height>adjusted_ceiling_height){
                // A rising slope must not push the player's head through a roof.
                new_location=previous_location;clipped=true;
                adjusted_floor_height=first_time?get_polygon_data(legs->polygon)->floor_height:FIXED_TO_WORLD(variables->floor_height);
                adjusted_ceiling_height=first_time?get_polygon_data(legs->polygon)->ceiling_height:FIXED_TO_WORLD(variables->ceiling_height);
                object_floor=INT16_MIN;player->supporting_polygon_index=legs->polygon;
            } else {
                const double old_floor=first_time?get_polygon_data(legs->polygon)->floor_height:FIXED_TO_WORLD(variables->floor_height);
                double corrected=new_location.z;
                if(!SprintathonZeroG::active(player) && SprintathonTerrain::groundContact(previous_location.z,old_floor,new_location.z,support,
                    variables->external_velocity.k,first_time,2.0,corrected)){
                    new_location.z=static_cast<world_distance>(std::lround(corrected));
                    variables->position.z=WORLD_TO_FIXED(new_location.z);
                    if(variables->external_velocity.k<0)variables->external_velocity.k=0;
                    variables->flags&=~(_BELOW_GROUND_BIT|_ABOVE_GROUND_BIT);
                }
                player->supporting_polygon_index=destination;
            }
        }
    }

    if(SprintathonZeroG::active(player)){
        // The normal physics path resolves vertical contact before moving. In
        // zero-g resolve it here against destination geometry and solid objects.
        const int floor=MAX(adjusted_floor_height,object_floor);
        const int roof=int(adjusted_ceiling_height)-FIXED_TO_WORLD(variables->actual_height);
        if(roof<floor){new_location=legs->location;clipped=true;
            variables->external_velocity.i=variables->external_velocity.j=variables->external_velocity.k=0;
        }else{
            if(new_location.z<floor){new_location.z=static_cast<world_distance>(floor);clipped=true;
                if(variables->external_velocity.k<0)variables->external_velocity.k=0;}
            if(new_location.z>roof){new_location.z=static_cast<world_distance>(roof);clipped=true;
                if(variables->external_velocity.k>0)variables->external_velocity.k=0;}
            SprintathonZeroG::correct(player,double(new_location.x)-attempted_location.x,
                double(new_location.y)-attempted_location.y);
        }
    }

	/* translate_map_object will handle crossing polygon boundaries */
	if (translate_map_object(monster->object_index, &new_location, NONE))
	{
		if (old_polygon_index==legs->polygon) clipped= true; /* oops; trans_map_obj destructively changed our position */
		if(take_action)
			monster_moved(player->monster_index, old_polygon_index);
	}

	/*
	 * Remember obstruction for near-surface ledge assistance during the
	 * following physics tick.
	 */
	if (clipped)
		variables->flags |= _HORIZONTAL_COLLISION_BIT;
	else
		variables->flags &= (uint16)~_HORIZONTAL_COLLISION_BIT;

	/* if our move got clipped, copy the new coordinate back into the physics variables */
	if (clipped)
	{
		// Collision correction points away from the wall.
		variables->wall_push_i =
			WORLD_TO_FIXED(new_location.x - attempted_location.x);
		variables->wall_push_j =
			WORLD_TO_FIXED(new_location.y - attempted_location.y);

		variables->position.x= WORLD_TO_FIXED(new_location.x);
		variables->position.y= WORLD_TO_FIXED(new_location.y);
		variables->position.z= WORLD_TO_FIXED(new_location.z);
	}
	else
	{
		variables->wall_push_i= 0;
		variables->wall_push_j= 0;
	}
	
	/* shadow position in player structure, build camera location */
	step_height= (constants->step_amplitude*sine_table[variables->step_phase>>(FIXED_FRACTIONAL_BITS-ANGULAR_BITS+1)])>>TRIG_SHIFT;
	step_height= (step_height*variables->step_amplitude)>>FIXED_FRACTIONAL_BITS;

	player->camera_location= new_location;
	if (PLAYER_IS_DEAD(player) && new_location.z<adjusted_floor_height) new_location.z= adjusted_floor_height;
	player->location= new_location;
	player->camera_location.z += FIXED_TO_WORLD(step_height +
		(input_preferences->sprintathon_enabled ?
			variables->actual_height - FIXED_ONE / 8 :
			variables->actual_height - constants->camera_height));
	player->step_height = FIXED_TO_WORLD(step_height);
	player->camera_polygon_index= legs->polygon;

	/* shadow facing in player structure and object structure */
	fixed_facing= variables->direction+variables->head_direction;
	facing= FIXED_INTEGERAL_PART(fixed_facing), facing= NORMALIZE_ANGLE(facing);
	elevation= FIXED_INTEGERAL_PART(variables->elevation), elevation= NORMALIZE_ANGLE(elevation);
	legs->location.z= player->location.z;
	legs->facing= NORMALIZE_ANGLE(FIXED_INTEGERAL_PART(variables->direction)), torso->facing= player->facing= facing;
	player->elevation= elevation;

	/* initialize floor_height and ceiling_height for next call to physics_update() */
	variables->floor_height= WORLD_TO_FIXED(MAX(adjusted_floor_height, object_floor));
	variables->ceiling_height= WORLD_TO_FIXED(adjusted_ceiling_height);
	{
		short media_index= get_polygon_data(legs->polygon)->media_index;
		// LP change: idiot-proofing
		media_data *media = get_media_data(media_index);
		world_distance media_height= (media_index==NONE || !media) ? INT16_MIN : media->height;

		variables->media_height =
			(media_index == NONE || !media) ?
				0 : WORLD_TO_FIXED(media_height);

		if (player->location.z<media_height) variables->flags|= _FEET_BELOW_MEDIA_BIT; else variables->flags&= (uint16)~_FEET_BELOW_MEDIA_BIT;
		if (player->camera_location.z<media_height) variables->flags|= _HEAD_BELOW_MEDIA_BIT; else variables->flags&= (uint16)~_HEAD_BELOW_MEDIA_BIT;
	}

	// so our sounds come from the right place
	monster->sound_location= player->camera_location;
	monster->sound_polygon_index= player->camera_polygon_index;
}

/* separate physics_constant structures are passed in for running/walking modes */
// ZZZ note: 'player' is only used in this routine for PLAYER_IS_DEAD(player) - as such,
// perhaps this should take an "is_dead" flag as a parameter instead of the player structure.
static void physics_update(
	struct physics_constants *constants,
	struct physics_variables *variables,
	struct player_data *player,
	uint32 action_flags)
{
	fixed_point3d new_position;
	short sine, cosine;
	_fixed delta_z;
	_fixed delta; /* used as a scratch ‘change’ variable */
	
	const bool player_is_local = (player == local_player);
	const bool sprintathon = input_preferences->sprintathon_enabled;
	const bool bullet_time= sprintathon_bullet_time_active();
	bool advance_dodge_animation= true;
	if ((player->dodge_ticks_remaining>0 ||
		 player->back_dodge_recovery_ticks>0 ||
		 player->cartwheel_active || player->backflip_active) && bullet_time)
	{
		player->dodge_bullet_time_phase+= 35;
		if (player->dodge_bullet_time_phase<100)
			advance_dodge_animation= false;
		else
			player->dodge_bullet_time_phase-= 100;
	}
	else
		player->dodge_bullet_time_phase= 0;
	const bool modern_jump = sprintathon && input_preferences->sprintathon_jump;
	const bool modern_crouch = sprintathon && input_preferences->sprintathon_crouch;
	const bool modern_long_jump = modern_jump && modern_crouch && input_preferences->sprintathon_long_jump;
	const bool modern_wall_run = sprintathon && input_preferences->sprintathon_wall_run;
	const bool modern_slide = sprintathon && input_preferences->sprintathon_slide;
	const bool modern_dodge = sprintathon && input_preferences->sprintathon_dodge;
	const bool modern_wall_jump = modern_jump && input_preferences->sprintathon_wall_jump;
	const bool modern_swimming = sprintathon && input_preferences->sprintathon_swimming;
	const bool modern_ledge_grab = modern_jump && input_preferences->sprintathon_ledge_grab;
	const _fixed maximum_elevation = sprintathon_mouselook_limit(constants);
	if (player->dodge_auto_bullet_time &&
		(!modern_dodge || PLAYER_IS_DEAD(player)))
	{
		if (player_is_local)
			set_sprintathon_bullet_time(false, false);
		player->dodge_auto_bullet_time= false;
	}

	if (player->cartwheel_requested)
	{
		const bool lateral_dodge=
			player->dodge_last_direction==-1 ||
			player->dodge_last_direction==1;
		if (modern_dodge && lateral_dodge &&
			player->dodge_ticks_remaining>=4 &&
			!player->cartwheel_active)
		{
			player->cartwheel_active= true;
			player->cartwheel_direction= player->dodge_last_direction;
			player->cartwheel_ticks_remaining= 30;
			player->cartwheel_camera_roll= 0;
		}
		player->cartwheel_requested= false;
	}
	if (player->backflip_requested)
	{
		if (modern_dodge && player->dodge_last_direction==2 &&
			player->dodge_ticks_remaining>=6 &&
			!player->backflip_active)
		{
			player->backflip_active= true;
			player->backflip_ticks_remaining= 30;
			player->backflip_camera_pitch= 0;
		}
		player->backflip_requested= false;
	}

	// A wall run is traversal along a wall, not a reward for hitting it head-on.
	// wall_push points away from the contacted wall. Compare the component of
	// current motion into that normal with motion along the wall, allowing a
	// contact angle of roughly 56 degrees from the wall plane.
	const angle wall_run_facing= FIXED_INTEGERAL_PART(variables->direction);
	const int64_t wall_run_motion_x=
		((static_cast<int64_t>(variables->velocity)*
			cosine_table[wall_run_facing]-
		  static_cast<int64_t>(variables->perpendicular_velocity)*
			sine_table[wall_run_facing])>>TRIG_SHIFT)+
		variables->external_velocity.i;
	const int64_t wall_run_motion_y=
		((static_cast<int64_t>(variables->velocity)*
			sine_table[wall_run_facing]+
		  static_cast<int64_t>(variables->perpendicular_velocity)*
			cosine_table[wall_run_facing])>>TRIG_SHIFT)+
		variables->external_velocity.j;
	const int64_t wall_run_normal_motion=
		wall_run_motion_x*variables->wall_push_i+
		wall_run_motion_y*variables->wall_push_j;
	const int64_t wall_run_tangent_motion=
		wall_run_motion_x*variables->wall_push_j-
		wall_run_motion_y*variables->wall_push_i;
	const int64_t wall_run_normal_magnitude=
		wall_run_normal_motion<0 ? -wall_run_normal_motion :
		wall_run_normal_motion;
	const int64_t wall_run_tangent_magnitude=
		wall_run_tangent_motion<0 ? -wall_run_tangent_motion :
		wall_run_tangent_motion;
	const bool shallow_wall_contact=
		(variables->wall_push_i!=0 || variables->wall_push_j!=0) &&
		wall_run_normal_motion<=0 &&
		wall_run_tangent_magnitude>0 &&
		2*wall_run_normal_magnitude<=3*wall_run_tangent_magnitude;
	// Lean the viewpoint away from the wall during a wall run. The collision
	// correction vector points away from the wall; projecting it onto the
	// player's right vector tells us which way the camera should roll.
	int16 target_wall_run_roll= 0;
	const bool sprint_wall_running=
		modern_wall_run && player->sprinting &&
		(variables->flags&_HORIZONTAL_COLLISION_BIT) &&
		(variables->flags&_ABOVE_GROUND_BIT) &&
		!(variables->flags&_FEET_BELOW_MEDIA_BIT) &&
		shallow_wall_contact;
	const bool sprint_sway_active=
		sprintathon && player->sprinting &&
		(!(variables->flags&_ABOVE_GROUND_BIT) || sprint_wall_running) &&
		!(variables->flags&_FEET_BELOW_MEDIA_BIT);
	if (sprint_sway_active)
	{
		// Use the footstep countdown itself: each sound occurs at an alternating
		// left/right peak, so cadence changes cannot put the two out of phase.
		const int16 countdown= std::min<int16>(player->footstep_ticks_remaining, 6);
		const int16 elapsed= 6-countdown;
		const angle sprint_sway_phase= NORMALIZE_ANGLE(static_cast<angle>(
			(player->footstep_alternate ? QUARTER_CIRCLE : 3*QUARTER_CIRCLE) +
			(static_cast<int32>(elapsed)*HALF_CIRCLE)/6));
		const int16 sprint_sway_amplitude= (FULL_CIRCLE*3)/360;
		target_wall_run_roll= static_cast<int16>(
			(sprint_sway_amplitude*sine_table[sprint_sway_phase])>>TRIG_SHIFT);
	}
	if (sprint_wall_running)
	{
		const angle facing= FIXED_INTEGERAL_PART(variables->direction);
		const int64_t side=
			-static_cast<int64_t>(variables->wall_push_i)*sine_table[facing] +
			 static_cast<int64_t>(variables->wall_push_j)*cosine_table[facing];
		const int16 wall_run_roll= (FULL_CIRCLE*12)/360;
		if (side>0) target_wall_run_roll+= wall_run_roll;
		else if (side<0) target_wall_run_roll-= wall_run_roll;
	}

	/*
	 * Give sidestepping a restrained visual lean, with a stronger bank in the
	 * air. This is deliberately cosmetic and does not change player velocity.
	 * Wall-running and the stronger dodge/slide poses take precedence below.
	 */
	int16 target_strafe_roll= 0;
	const bool airborne= (variables->flags&_ABOVE_GROUND_BIT)!=0;
	if (sprintathon &&
		!(variables->flags&_FEET_BELOW_MEDIA_BIT) &&
		!sprint_wall_running &&
		(airborne || player->sprinting))
	{
		const int8 strafe_direction=
			(action_flags&_sidestepping_left) ? -1 :
			(action_flags&_sidestepping_right) ? 1 : 0;
		const int strafe_roll_degrees=
			airborne ? 5 : 2;
		target_strafe_roll= static_cast<int16>(
			(FULL_CIRCLE*strafe_roll_degrees*strafe_direction)/360);
	}
	const int16 strafe_roll_difference=
		target_strafe_roll-player->sprintathon_strafe_roll;
	bool advance_strafe_roll= true;
	if (bullet_time && strafe_roll_difference!=0)
	{
		player->sprintathon_strafe_phase+=
			SPRINTATHON_BULLET_TIME_PERCENT;
		if (player->sprintathon_strafe_phase<100)
			advance_strafe_roll= false;
		else
			player->sprintathon_strafe_phase-= 100;
	}
	else
		player->sprintathon_strafe_phase= 0;
	if (strafe_roll_difference!=0 && advance_strafe_roll)
	{
		int16 strafe_roll_step=
			strafe_roll_difference/(target_strafe_roll ? 3 : 5);
		if (strafe_roll_step==0)
			strafe_roll_step= strafe_roll_difference>0 ? 1 : -1;
		player->sprintathon_strafe_roll+= strafe_roll_step;
	}
	if (!sprint_wall_running)
		target_wall_run_roll+= player->sprintathon_strafe_roll;

	// Ease in quickly and return a little more gently. Keep a one-unit minimum
	// step so the fixed-angle value always reaches its target.
	int16 target_camera_pitch= 0;
	const bool back_dodging=
		player->dodge_ticks_remaining>0 &&
		player->dodge_last_direction==2;
	if ((modern_slide &&
		 (player->slide_ticks_remaining>0 ||
		  player->flying_kick_landing_ticks>0)) ||
		(sprintathon && player->dodge_ticks_remaining>0))
	{
		// Back-dodging pitches downward without a sideways roll.
		if (player->dodge_ticks_remaining>0)
		{
			if (!back_dodging && !player->cartwheel_active)
				target_wall_run_roll=
					(FULL_CIRCLE*12*player->dodge_last_direction)/360;
		}
		else
			target_wall_run_roll= (FULL_CIRCLE*9)/360;
		target_camera_pitch= back_dodging && !player->backflip_active ?
			(FULL_CIRCLE*11)/360 : (FULL_CIRCLE*7)/360;
		if (player->backflip_active)
			target_camera_pitch= 0;
	}
	// Airborne sprint sway disappears on the first airborne tick.
	if (sprintathon && player->sprinting &&
		(variables->flags&_ABOVE_GROUND_BIT) && !sprint_wall_running &&
		target_camera_pitch==0 && player->sprintathon_strafe_roll==0)
	{
		player->sprintathon_camera_roll= 0;
	}
	const int16 roll_difference= target_wall_run_roll-player->sprintathon_camera_roll;
	if (roll_difference!=0 &&
		(player->dodge_ticks_remaining==0 || advance_dodge_animation))
	{
		int16 roll_step= roll_difference/(target_wall_run_roll ? 3 : 5);
		if (roll_step==0) roll_step= roll_difference>0 ? 1 : -1;
		player->sprintathon_camera_roll+= roll_step;
	}
	const int16 pitch_difference= target_camera_pitch-player->sprintathon_camera_pitch;
	if (pitch_difference!=0 &&
		(player->dodge_ticks_remaining==0 || advance_dodge_animation))
	{
		int16 pitch_step= pitch_difference/(target_camera_pitch ? 3 : 5);
		if (pitch_step==0) pitch_step= pitch_difference>0 ? 1 : -1;
		player->sprintathon_camera_pitch+= pitch_step;
	}

	if (player->cartwheel_active && advance_dodge_animation)
	{
		constexpr int cartwheel_duration= 30;
		if (player->cartwheel_ticks_remaining>0)
		{
			--player->cartwheel_ticks_remaining;
			const int elapsed=
				cartwheel_duration-player->cartwheel_ticks_remaining;
			const int32 progress=
				(static_cast<int32>(elapsed)*FIXED_ONE)/cartwheel_duration;
			const int32 progress_squared= static_cast<int32>(
				(static_cast<int64_t>(progress)*progress)/FIXED_ONE);
			const int32 smoothstep= static_cast<int32>(
				(static_cast<int64_t>(progress_squared)*
				 (3*FIXED_ONE-2*progress))/FIXED_ONE);
			const int32 progress_cubed= static_cast<int32>(
				(static_cast<int64_t>(progress_squared)*progress)/FIXED_ONE);
			const int32 smootherstep= static_cast<int32>(
				(static_cast<int64_t>(progress_cubed)*
				 (10*FIXED_ONE-15*progress+6*progress_squared))/
				 FIXED_ONE);

			// Mostly retain the original curve, with a subtle stronger ease
			// at each end and a little more speed through the middle.
			const int32 eased= static_cast<int32>(
				(3*static_cast<int64_t>(smoothstep)+smootherstep)/4);
			player->cartwheel_camera_roll= static_cast<int32>(
				(static_cast<int64_t>(player->cartwheel_direction)*
				 FULL_CIRCLE*eased)/FIXED_ONE);
		}
		else
		{
			player->cartwheel_active= false;
			player->cartwheel_direction= 0;
			player->cartwheel_camera_roll= 0;
			player->slide_recovery_ticks=
				std::max<uint8>(player->slide_recovery_ticks, 12);
		}
	}
	else if (!player->cartwheel_active)
	{
		player->cartwheel_camera_roll= 0;
	}

	if (player->backflip_active && advance_dodge_animation)
	{
		constexpr int backflip_duration= 30;
		if (player->backflip_ticks_remaining>0)
		{
			--player->backflip_ticks_remaining;
			if (player_is_local && input_preferences->sprintathon_footsteps)
			{
				if (player->backflip_ticks_remaining==5)
					sprintathon_play_footstep_sound(
						player->monster_index, false);
				else if (player->backflip_ticks_remaining==3)
					sprintathon_play_footstep_sound(
						player->monster_index, true);
			}
			const int elapsed=
				backflip_duration-player->backflip_ticks_remaining;
			const int32 progress=
				(static_cast<int32>(elapsed)*FIXED_ONE)/backflip_duration;
			const int32 progress_squared= static_cast<int32>(
				(static_cast<int64_t>(progress)*progress)/FIXED_ONE);
			const int32 smoothstep= static_cast<int32>(
				(static_cast<int64_t>(progress_squared)*
				 (3*FIXED_ONE-2*progress))/FIXED_ONE);
			const int32 progress_cubed= static_cast<int32>(
				(static_cast<int64_t>(progress_squared)*progress)/FIXED_ONE);
			const int32 smootherstep= static_cast<int32>(
				(static_cast<int64_t>(progress_cubed)*
				 (10*FIXED_ONE-15*progress+6*progress_squared))/
				 FIXED_ONE);
			const int32 eased= static_cast<int32>(
				(3*static_cast<int64_t>(smoothstep)+smootherstep)/4);
			player->backflip_camera_pitch= static_cast<int32>(
				(static_cast<int64_t>(FULL_CIRCLE)*eased)/FIXED_ONE);
		}
		else
		{
			player->backflip_active= false;
			player->backflip_camera_pitch= 0;
			player->slide_recovery_ticks=
				std::max<uint8>(player->slide_recovery_ticks, 12);
		}
	}
	else if (!player->backflip_active)
	{
		player->backflip_camera_pitch= 0;
	}
	if (!modern_swimming) variables->flags&= (uint16)~_WATER_MANTLING_BIT;
	if (!modern_ledge_grab) variables->flags&= (uint16)~_DRY_MANTLING_BIT;
	if (!sprintathon || !input_preferences->sprintathon_sprint) player->sprinting= false;

	if (PLAYER_IS_DEAD(player)) /* dead players immediately loose all bodily control */
	{
		int32 dot_product;
		
		cosine= cosine_table[FIXED_INTEGERAL_PART(variables->direction)], sine= sine_table[FIXED_INTEGERAL_PART(variables->direction)];
		dot_product= ((((variables->velocity*cosine)>>TRIG_SHIFT) + variables->external_velocity.i)*cosine +
			(((variables->velocity*sine)>>TRIG_SHIFT) + variables->external_velocity.j)*sine)>>TRIG_SHIFT;

		if (dot_product>0 && dot_product<(constants->maximum_forward_velocity>>4)) dot_product= 0;
		switch (SGN(dot_product))
		{
			case -1: action_flags= _looking_up; break;
			case 1: action_flags= _looking_down; break;
			case 0: action_flags= 0; break;
			default:
				assert(false);
				break;
		}
		
		variables->floor_height-= DROP_DEAD_HEIGHT;
		
		// Prohibit twitching while dead (!)
		if (player_is_local)
			resync_virtual_aim();
	}
	delta_z= variables->position.z-variables->floor_height;

	/*
	 * Keep a short grace period after leaving the ground. This covers
	 * stair transitions and permits jumping just after walking off an edge.
	 */
	constexpr uint8 jump_grace_limit = 4;
	constexpr uint8 jump_buffer_limit = 5;

	const bool touching_ground =
		delta_z <= CLOSE_ENOUGH_TO_FLOOR;

	// Input is sampled before this authoritative ground test. Consume the
	// fresh-crouch latch here so a kick cannot be lost to stale contact flags.
	if (player->flying_kick_requested)
	{
		const bool kick_drains_stamina =
			input_preferences->sprintathon_stamina_kick;
		const int16 minimum_move_oxygen=
			(PLAYER_MAXIMUM_SUIT_OXYGEN*8)/100;
		if (modern_slide && !touching_ground &&
			!(variables->flags&_FEET_BELOW_MEDIA_BIT) &&
			(!kick_drains_stamina ||
			 player->suit_oxygen>=minimum_move_oxygen) &&
			!player->flying_kick_active &&
			(!player->flying_kick_recovery_pending ||
			 (player->wall_kick_rearm_pending &&
			  player->wall_kick_cooldown_ticks==0)) &&
			player->flying_kick_landing_ticks==0)
		{
			player->flying_kick_active= true;
			player->flying_kick_ticks= 0;
			player->flying_kick_exit_ticks= 0;
			player->flying_kick_recovery_pending= false;
			player->wall_kick_rearm_pending= false;
			player->slide_punch_pending= false;
			sprintathon_begin_sweep_attack(player->monster_index);

			// Charge once when the airborne kick is actually accepted, not
			// continuously while crouch remains held.
			if (kick_drains_stamina)
			{
				const int16 flying_kick_oxygen_cost=
					(PLAYER_MAXIMUM_SUIT_OXYGEN*8)/100;
				player->suit_oxygen= std::max<int16>(
					0, player->suit_oxygen-flying_kick_oxygen_cost);
				player->flying_kick_oxygen_recharge_delay= 10;
			}
		}
		player->flying_kick_requested= false;
	}

	if (player->wall_kick_cooldown_ticks>0)
		player->wall_kick_cooldown_ticks--;
	if (player->wall_run_jump_cooldown_ticks>0)
		player->wall_run_jump_cooldown_ticks--;

	if (touching_ground)
	{
		variables->jump_grace_ticks = 0;
	}
	else if (variables->jump_grace_ticks < UINT8_MAX)
	{
		++variables->jump_grace_ticks;
	}

	/*
	 * Remember a fresh Jump press made just before landing. The normal held
	 * latch still prevents repeated jumps, while this short buffer makes a
	 * slightly early press fire on the first authoritative ground tick.
	 */
	const bool fresh_jump_press =
		modern_jump && (action_flags&_swim) &&
		!(variables->flags&_JUMP_HELD_BIT);
	if (fresh_jump_press && !touching_ground &&
		variables->jump_grace_ticks>jump_grace_limit &&
		!(variables->flags&(_FEET_BELOW_MEDIA_BIT |
			_WATER_MANTLING_BIT | _DRY_MANTLING_BIT)))
	{
		player->jump_buffer_ticks= jump_buffer_limit;
	}
	const bool consume_buffered_jump =
		modern_jump && touching_ground &&
		player->jump_buffer_ticks>0;

	/*
	 * Experimental hold-to-crouch. The microphone/aux-trigger action
	 * is reused because the original action packet has no spare bits.
	 */
	if ((modern_crouch || modern_dodge) && !PLAYER_IS_DEAD(player))
	{
		const _fixed standing_height = constants->height;
		const _fixed crouching_height = constants->height / 2;
		const _fixed sliding_height =
			(constants->height * 7) / 16;
		const _fixed back_dodge_height =
			(constants->height * 5) / 16;
		constexpr int back_recovery_duration = 26;
		constexpr int back_pause_duration = 6;
		_fixed back_recovery_height = standing_height;

		if (player->back_dodge_recovery_ticks > 0)
		{
			const int rising_ticks =
				back_recovery_duration - back_pause_duration;
			const int remaining_rise_ticks = std::min<int>(
				player->back_dodge_recovery_ticks,
				rising_ticks);

			back_recovery_height =
				back_dodge_height +
				((standing_height - back_dodge_height) *
				 (rising_ticks - remaining_rise_ticks)) /
				rising_ticks;
		}

		const _fixed target_height =
			back_dodging ? back_dodge_height :
			player->back_dodge_recovery_ticks > 0 ?
				back_recovery_height :
			(player->slide_ticks_remaining > 0 ||
			 player->flying_kick_active ||
			 player->dodge_ticks_remaining > 0) ?
				sliding_height :
			(modern_crouch && (action_flags & _microphone_button)) ?
				crouching_height :
				standing_height;
		const _fixed crouch_step =
			std::max<_fixed>(FIXED_ONE / 64, standing_height / 8);

		if (variables->actual_height > target_height)
		{
			variables->actual_height =
				std::max(
					target_height,
					variables->actual_height - crouch_step);
		}
		else if (variables->actual_height < target_height)
		{
			const bool enough_headroom =
				variables->position.z + standing_height <=
					variables->ceiling_height;

			if (enough_headroom)
			{
				variables->actual_height =
					std::min(
						target_height,
						variables->actual_height + crouch_step);
			}
		}
	}
	else if (!modern_crouch && variables->actual_height<constants->height &&
		variables->position.z+constants->height<=variables->ceiling_height)
	{
		variables->actual_height= constants->height;
	}

	/* process modifier keys (sidestepping and looking) into normal actions */
	if ((action_flags&_turning) && (action_flags&_sidestep_dont_turn) && !(action_flags&_absolute_yaw_mode))
	{
		if (action_flags&_turning_left) action_flags|= _sidestepping_left;
		if (action_flags&_turning_right) action_flags|= _sidestepping_right;
		action_flags&= ~_turning;
	}

	/* Double-tap left, right or backward; dedicated commands stay lateral. */
	// A slide roll needs two fresh forward presses after the slide starts.
	// The held sprint-forward key is latched at slide start, so it cannot
	// count as the first tap. Keep the roll within the remaining slide time.
	constexpr uint8 slide_roll_duration = 30;
	if (player->slide_roll_ticks_remaining > 0)
	{
		player->slide_roll_fraction += bullet_time ? SPRINTATHON_BULLET_TIME_PERCENT : 100;
		if (player->slide_roll_fraction >= 100)
		{
			player->slide_roll_fraction -= 100;
			--player->slide_roll_ticks_remaining;
		}
	}
	// A queued follow-up starts exactly when the current somersault ends.
	// Give the slide enough time to cover the entire second roll.
	if (player->slide_roll_ticks_remaining == 0 && player->slide_roll_queued &&
		modern_slide && player->slide_ticks_remaining > 0)
	{
		player->slide_roll_ticks_remaining = slide_roll_duration;
		player->slide_roll_fraction = 0;
		player->slide_roll_queued = false;
		player->slide_ticks_remaining = std::max<uint8>(
			player->slide_ticks_remaining, slide_roll_duration + 1);
	}
	if (player->slide_roll_tap_window > 0)
		--player->slide_roll_tap_window;
	const bool roll_forward_down =
		(action_flags & _moving_forward) != 0 &&
		(action_flags & _absolute_position_mode) == 0;
	if (!modern_slide || player->slide_ticks_remaining == 0)
	{
		player->slide_roll_tap_window = 0;
		player->slide_roll_queued = false;
		player->slide_roll_forward_was_down = roll_forward_down;
	}
	else
	{
		if (roll_forward_down && !player->slide_roll_forward_was_down &&
			!player->slide_roll_queued &&
			(player->slide_roll_ticks_remaining > 0 ||
			 (!player->slide_roll_used &&
			  player->slide_ticks_remaining > slide_roll_duration + 1)))
		{
			if (player->slide_roll_tap_window > 0)
			{
				player->slide_roll_tap_window = 0;
				if (player->slide_roll_ticks_remaining > 0)
					player->slide_roll_queued = true;
				else
				{
					player->slide_roll_ticks_remaining = slide_roll_duration;
					player->slide_roll_fraction = 0;
					player->slide_roll_used = true;
				}
			}
			else
				player->slide_roll_tap_window = 8;
		}
		player->slide_roll_forward_was_down = roll_forward_down;
	}

	if (player->dodge_tap_window>0)
		--player->dodge_tap_window;
	const int8 direct_dodge_direction=
		(modern_dodge && !(action_flags&_absolute_yaw_mode)) ?
		((action_flags&_looking_left) ? -1 :
		 (action_flags&_looking_right) ? 1 : 0) : 0;
	const int8 sidestep_dodge_direction=
		(action_flags&_sidestepping_left) ? -1 :
		(action_flags&_sidestepping_right) ? 1 : 0;
	const int8 backward_dodge_direction=
		(action_flags&_moving_backward) &&
		!(action_flags&_absolute_position_mode) ? 2 : 0;
	const int8 dodge_direction= direct_dodge_direction!=0 ?
		direct_dodge_direction : sidestep_dodge_direction!=0 ?
		sidestep_dodge_direction : backward_dodge_direction;
	if (direct_dodge_direction!=0)
		action_flags&= ~_looking;
	else
		player->dodge_command_was_down= false;
	if (dodge_direction==0)
	{
		player->dodge_key_was_down= false;
	}
	else if (modern_dodge &&
		(!player->dodge_key_was_down ||
		 (direct_dodge_direction!=0 && !player->dodge_command_was_down)))
	{
		const bool dodge_drains_stamina =
			input_preferences->sprintathon_stamina_dodge;
		const int16 dodge_oxygen_cost =
			(PLAYER_MAXIMUM_SUIT_OXYGEN*8)/100;
		const bool can_dodge=
			touching_ground &&
			(!dodge_drains_stamina ||
			 player->suit_oxygen>=dodge_oxygen_cost) &&
			!(variables->flags&_FEET_BELOW_MEDIA_BIT) &&
			player->dodge_ticks_remaining==0 &&
			player->slide_ticks_remaining==0 &&
			player->slide_recovery_ticks==0 &&
			!player->flying_kick_active &&
			player->flying_kick_landing_ticks==0;
		if (can_dodge &&
			(direct_dodge_direction!=0 ||
			 (player->dodge_last_direction==dodge_direction &&
			  player->dodge_tap_window>0)))
		{
			const angle facing=
				NORMALIZE_ANGLE(FIXED_INTEGERAL_PART(variables->direction));
			const bool backward= dodge_direction==2;
			const _fixed dodge_speed= backward ?
				constants->maximum_backward_velocity*4 :
				(constants->maximum_perpendicular_velocity*5)/2;
			variables->velocity= 0;
			variables->perpendicular_velocity= 0;
			if (backward)
			{
				variables->external_velocity.i=
					-(cosine_table[facing]*dodge_speed)>>TRIG_SHIFT;
				variables->external_velocity.j=
					-(sine_table[facing]*dodge_speed)>>TRIG_SHIFT;
				// A short upward hop; gravity brings the player back down.
				variables->external_velocity.k= FIXED_ONE/16;
				player->back_dodge_recovery_ticks= 0;
			}
			else
			{
				variables->external_velocity.i=
					(-sine_table[facing]*dodge_speed*dodge_direction)>>
					TRIG_SHIFT;
				variables->external_velocity.j=
					(cosine_table[facing]*dodge_speed*dodge_direction)>>
					TRIG_SHIFT;
				variables->external_velocity.k= -FIXED_ONE/32;
			}
			player->dodge_ticks_remaining= backward ? 12 : 8;
			player->dodge_bullet_time_phase= 0;
			player->dodge_last_direction= dodge_direction;
			player->dodge_tap_window= 0;
			if (player_is_local &&
				input_preferences->sprintathon_bullet_time &&
				input_preferences->sprintathon_dodge_bullet_time &&
				!sprintathon_bullet_time_active())
			{
				set_sprintathon_bullet_time(true);
				player->dodge_auto_bullet_time=
					sprintathon_bullet_time_active();
			}
			if (dodge_drains_stamina)
			{
				player->suit_oxygen= std::max<int16>(
					0, player->suit_oxygen-dodge_oxygen_cost);
				player->flying_kick_oxygen_recharge_delay= 10;
			}
		}
		else
		{
			player->dodge_last_direction= dodge_direction;
			player->dodge_tap_window= 8;
		}
		player->dodge_key_was_down= true;
		if (direct_dodge_direction!=0)
			player->dodge_command_was_down= true;
	}
	// Sprintathon reuses Move -> Look as Sprint; restore its legacy behavior when disabled.
	if (!sprintathon && (action_flags&_moving) && (action_flags&_look_dont_turn) &&
		!(action_flags&_absolute_position_mode))
	{
		if (action_flags&_moving_forward) action_flags|= _looking_up;
		if (action_flags&_moving_backward) action_flags|= _looking_down;
		action_flags&= ~_moving;
		action_flags&= ~_absolute_pitch_mode;
	}

	/* handle turning left or right; if we’ve exceeded our maximum velocity lock out user actions
		until we return to a legal range */
	if (action_flags&_absolute_yaw_mode)
	{
		variables->angular_velocity= (GET_ABSOLUTE_YAW(action_flags)-MAXIMUM_ABSOLUTE_YAW/2)<<(FIXED_FRACTIONAL_BITS); // !!!!!!!!!!
	}
	else
	{
		if (variables->angular_velocity<-constants->maximum_angular_velocity||variables->angular_velocity>constants->maximum_angular_velocity) action_flags&= ~_turning;
		switch (action_flags&_turning)
		{
			case _turning_left:
				delta= variables->angular_velocity>0 ? constants->angular_acceleration+constants->angular_deceleration : constants->angular_acceleration;
				variables->angular_velocity= FLOOR(variables->angular_velocity-delta, -constants->maximum_angular_velocity);
				break;
			case _turning_right:
				delta= variables->angular_velocity<0 ? constants->angular_acceleration+constants->angular_deceleration : constants->angular_acceleration;
				variables->angular_velocity= CEILING(variables->angular_velocity+delta, constants->maximum_angular_velocity);
				break;
			
			default: /* slow down */
				variables->angular_velocity= (variables->angular_velocity>=0) ?
					FLOOR(variables->angular_velocity-constants->angular_deceleration, 0) :
					CEILING(variables->angular_velocity+constants->angular_deceleration, 0);
				break;
		}
		
		/* handling looking left/right */
		switch (action_flags&_looking)
		{
			case _looking_left:
				variables->head_direction= FLOOR(variables->head_direction-constants->fast_angular_velocity, -constants->fast_angular_maximum);
				break;
			case _looking_right:
				variables->head_direction= CEILING(variables->head_direction+constants->fast_angular_velocity, constants->fast_angular_maximum);
				break;
			case _looking: /* do nothing if both keys are down */
				break;
			
			default: /* recenter head */
				variables->head_direction= (variables->head_direction>=0) ?
					FLOOR(variables->head_direction-constants->fast_angular_velocity, 0) :
					CEILING(variables->head_direction+constants->fast_angular_velocity, 0);
		}
		
		// Let yaw controls resync virtual yaw
		if (player_is_local && (action_flags & (_turning|_looking)) != 0)
			vir_aim_delta.yaw = 0;
	}

	if (action_flags&_absolute_pitch_mode)
	{
        variables->vertical_angular_velocity = sprintathon_curved_mouse_pitch(
            GET_ABSOLUTE_PITCH(action_flags) - MAXIMUM_ABSOLUTE_PITCH / 2) * FIXED_ONE;
	}
	else
	{
		/* if the user touched the recenter key, set the recenter flag and override all up/down
			keypresses with our own */
		if (action_flags&_looking_center) variables->flags|= _RECENTERING_BIT;
		if (variables->flags&_RECENTERING_BIT)
		{
			action_flags&= ~_looking_vertically;
			action_flags|= variables->elevation<0 ? _looking_up : _looking_down;
		}
	
		/* handle looking up and down; if we’re moving at our terminal velocity forward or backward,
			without any side-to-side motion, recenter our head vertically */

		if (!(action_flags&FLAGS_WHICH_PREVENT_RECENTERING)) /* can’t recenter if any of these are true */
		{
			if (((action_flags&_moving_forward) && (variables->velocity==constants->maximum_forward_velocity)) ||
				((action_flags&_moving_backward) && (variables->velocity==-constants->maximum_backward_velocity)))
			{
				if (variables->elevation<0)
				{
					variables->elevation= CEILING(variables->elevation+constants->angular_recentering_velocity, 0);
				}
				else
				{
					variables->elevation= FLOOR(variables->elevation-constants->angular_recentering_velocity, 0);
				}
				
				// Let auto-recentering resync virtual pitch
				if (player_is_local)
					vir_aim_delta.pitch = 0;
			}
		}

		switch (action_flags&_looking_vertically)
		{
			case _looking_down:
				delta= variables->vertical_angular_velocity>0 ? constants->angular_acceleration+constants->angular_deceleration : constants->angular_acceleration;
				variables->vertical_angular_velocity= FLOOR(variables->vertical_angular_velocity-delta, PLAYER_IS_DEAD(player) ? -(constants->maximum_angular_velocity>>3) : -constants->maximum_angular_velocity);
				break;
			case _looking_up:
				delta= variables->vertical_angular_velocity<0 ? constants->angular_acceleration+constants->angular_deceleration : constants->angular_acceleration;
				variables->vertical_angular_velocity= CEILING(variables->vertical_angular_velocity+delta, PLAYER_IS_DEAD(player) ? (constants->maximum_angular_velocity>>3) : constants->maximum_angular_velocity);
				break;
			
			default: /* if no key is being held down, decelerate; if the player is moving try and return to phi==0 */
				variables->vertical_angular_velocity= (variables->vertical_angular_velocity>=0) ?
					FLOOR(variables->vertical_angular_velocity-constants->angular_deceleration, 0) :
					CEILING(variables->vertical_angular_velocity+constants->angular_deceleration, 0);
				break;
		}
		
		// Let pitch controls resync virtual pitch
		if (player_is_local && (action_flags & _looking_vertically) != 0)
			vir_aim_delta.pitch = 0;
	}

	/* if we’re on the ground (or rising up from it), allow movement; if we’re flying through
		the air, don’t let the player adjust his velocity in any way */
	if (delta_z<=0 ||
		(sprintathon ?
			((modern_swimming && (variables->flags&_FEET_BELOW_MEDIA_BIT)) ||
			 (modern_swimming && (variables->flags&_WATER_MANTLING_BIT)) ||
			 (modern_ledge_grab && (variables->flags&_DRY_MANTLING_BIT)) ||
			 (modern_wall_run && player->sprinting &&
			  (variables->flags&_HORIZONTAL_COLLISION_BIT) &&
			  shallow_wall_contact)) :
			 (variables->flags&_HEAD_BELOW_MEDIA_BIT)))
	{
		if (action_flags&_absolute_position_mode)
		{
			short encoded_delta= GET_ABSOLUTE_POSITION(action_flags)-MAXIMUM_ABSOLUTE_POSITION/2;
			
			if (encoded_delta<0)
			{
				variables->velocity= (encoded_delta*constants->maximum_backward_velocity)>>(ABSOLUTE_POSITION_BITS-1);
			}
			else
			{
				variables->velocity= (encoded_delta*constants->maximum_forward_velocity)>>(ABSOLUTE_POSITION_BITS-1);
			}
		}
		else
		{
			/* handle moving forward or backward; if we’ve exceeded our maximum velocity lock out user actions
				until we return to a legal range */
			if (variables->velocity<-constants->maximum_backward_velocity||variables->velocity>constants->maximum_forward_velocity) action_flags&= ~_moving;
			switch (action_flags&_moving)
			{
				case _moving_forward:
					delta= variables->velocity<0 ? constants->deceleration+constants->acceleration : constants->acceleration;
					variables->velocity= CEILING(variables->velocity+delta, constants->maximum_forward_velocity);
					break;
				case _moving_backward:
					delta= variables->velocity>0 ? constants->deceleration+constants->acceleration : constants->acceleration;
					variables->velocity= FLOOR(variables->velocity-delta, -constants->maximum_backward_velocity);
					break;
				
				default: /* slow down */
					variables->velocity= (variables->velocity>=0) ?
						FLOOR(variables->velocity-constants->deceleration, 0) :
						CEILING(variables->velocity+constants->deceleration, 0);
					break;
			}
		}
		
		/* handle sidestepping left or right; if we’ve exceeded our maximum velocity lock out user actions
			until we return to a legal range */
		if (variables->perpendicular_velocity<-constants->maximum_perpendicular_velocity||variables->perpendicular_velocity>constants->maximum_perpendicular_velocity) action_flags&= ~_sidestepping;
		switch (action_flags&_sidestepping)
		{
			case _sidestepping_left:
				delta= variables->perpendicular_velocity>0 ? constants->acceleration+constants->deceleration : constants->acceleration;
				variables->perpendicular_velocity= FLOOR(variables->perpendicular_velocity-delta, -constants->maximum_perpendicular_velocity);
				break;
			case _sidestepping_right:
				delta= variables->perpendicular_velocity<0 ? constants->acceleration+constants->deceleration : constants->acceleration;
				variables->perpendicular_velocity= CEILING(variables->perpendicular_velocity+delta, constants->maximum_perpendicular_velocity);
				break;
			
			default: /* slow down */
				variables->perpendicular_velocity= (variables->perpendicular_velocity>=0) ?
					FLOOR(variables->perpendicular_velocity-constants->deceleration, 0) :
					CEILING(variables->perpendicular_velocity+constants->deceleration, 0);
				break;
		}
	}
	
	/*
	 * Experimental crouch movement limit.
	 *
	 * Acceleration remains responsive, but forward, backward and sideways
	 * velocity are capped at 60 percent while crouch is held.
	 */
	if (modern_crouch && (action_flags & _microphone_button) &&
		player->slide_ticks_remaining==0 &&
		!player->flying_kick_active &&
		player->flying_kick_landing_ticks==0 &&
		!player->flying_kick_recovery_pending)
	{
		const _fixed crouch_forward_limit =
			(constants->maximum_forward_velocity * 3) / 5;
		const _fixed crouch_backward_limit =
			(constants->maximum_backward_velocity * 3) / 5;
		const _fixed crouch_sideways_limit =
			(constants->maximum_perpendicular_velocity * 3) / 5;

		variables->velocity = PIN(
			variables->velocity,
			-crouch_backward_limit,
			crouch_forward_limit);

		variables->perpendicular_velocity = PIN(
			variables->perpendicular_velocity,
			-crouch_sideways_limit,
			crouch_sideways_limit);
	}

	if (modern_slide &&
		(player->slide_ticks_remaining>0 || player->flying_kick_active))
	{
		if (player->slide_ticks_remaining>0)
		{
			const int slide_duration= (TICKS_PER_SECOND*3)/2;
			const _fixed slide_speed=
				(constants->maximum_forward_velocity*player->slide_ticks_remaining*2)/
				std::max<int>(1, slide_duration);
			variables->velocity= std::max<_fixed>(slide_speed,
				constants->maximum_forward_velocity/3);
			variables->perpendicular_velocity= 0;

			// Use the built-in Sprintathon sweep directly. Projectile type
			// numbers differ between game scenarios (notably Marathon 1), so a
			// synthetic fist projectile can turn into an unrelated weapon shot.
			sprintathon_slide_attack(
				player->monster_index,
				player->facing,
				&player->camera_location,
				player->camera_polygon_index,
				(FIXED_ONE*7)/16, true);
		}
		else
		{
			// A flying kick does not alter airborne momentum. While crouch is
			// held, sweep the live facing direction and strike each newly
			// encountered target no more than once during this kick.
			if (player->flying_kick_ticks<UINT8_MAX)
				player->flying_kick_ticks++;

			object_data *player_object= get_object_data(player->object_index);
			world_point2d probe_start;
			probe_start.x= player_object->location.x;
			probe_start.y= player_object->location.y;
			world_point2d probe_end= probe_start;
			translate_point2d(&probe_end, WORLD_ONE/3, player->facing);
			const short probe_line_index= find_line_crossed_leaving_polygon(
				player_object->polygon, &probe_start, &probe_end);
			bool solid_wall_ahead= false;
			if (probe_line_index!=NONE)
			{
				const line_data *probe_line= get_line_data(probe_line_index);
				// Portal lines can still contain a lower wall beneath a ledge or
				// an upper wall below an overhang. Test the kick height against
				// the actual vertical opening as well as the line's solid flag.
				const world_distance kick_height= player_object->location.z+
					FIXED_TO_WORLD(variables->actual_height/3);
				solid_wall_ahead= LINE_IS_SOLID(probe_line) ||
					kick_height<=probe_line->highest_adjacent_floor ||
					kick_height>=probe_line->lowest_adjacent_ceiling;
			}
			if (!(action_flags&_microphone_button))
			{
				// Releasing crouch retracts the legs immediately instead of
				// holding the kick pose until the player reaches the floor.
				player->flying_kick_active= false;
				player->flying_kick_ticks= 0;
				player->flying_kick_exit_ticks= 8;
				player->flying_kick_recovery_pending= true;
				player->wall_kick_rearm_pending= false;
			}
			else if (solid_wall_ahead)
			{
				// A wall kick replaces the incoming motion. Leaving either the
				// player-controlled velocity or an older external impulse intact
				// lets held movement cancel or skew the rebound.
				variables->velocity= 0;
				variables->perpendicular_velocity= 0;
				variables->external_velocity.i= 0;
				variables->external_velocity.j= 0;

				// Launch opposite the angle of the kick. Wall geometry confirms
				// contact but no longer determines the rebound direction.
				const int32 kick_cosine= cosine_table[player->facing];
				const int32 kick_sine= sine_table[player->facing];
				const _fixed rebound=
					(constants->maximum_forward_velocity*3)/4;
				variables->external_velocity.i=
					-(kick_cosine*rebound)>>TRIG_SHIFT;
				variables->external_velocity.j=
					-(kick_sine*rebound)>>TRIG_SHIFT;

				// A strong enough lift to turn a wall kick into a useful
				// traversal move rather than merely a collision reaction.
				variables->external_velocity.k= FIXED_ONE/12;

				sprintathon_play_wall_kick_sound(player->monster_index);
				player->flying_kick_active= false;
				player->flying_kick_ticks= 0;
				player->flying_kick_exit_ticks= 8;
				player->flying_kick_recovery_pending= true;
				player->wall_kick_rearm_pending= true;
				player->wall_kick_cooldown_ticks=
					TICKS_PER_SECOND/2;
			}
			else if (action_flags&_microphone_button)
			{
				const angle movement_facing=
					FIXED_INTEGERAL_PART(variables->direction);
				const int64_t movement_cosine=
					cosine_table[movement_facing];
				const int64_t movement_sine=
					sine_table[movement_facing];
				const int64_t speed_x=
					((variables->velocity*movement_cosine-
					  variables->perpendicular_velocity*movement_sine)>>
					 TRIG_SHIFT)+variables->external_velocity.i;
				const int64_t speed_y=
					((variables->velocity*movement_sine+
					  variables->perpendicular_velocity*movement_cosine)>>
					 TRIG_SHIFT)+variables->external_velocity.j;
				const uint32 speed_squared= static_cast<uint32>(
					std::min<int64_t>(speed_x*speed_x+speed_y*speed_y,
						UINT32_MAX));
				const _fixed horizontal_speed= isqrt(speed_squared);
				const _fixed speed_ratio= static_cast<_fixed>(
					std::min<int64_t>(
						FIXED_ONE*2,
						(static_cast<int64_t>(horizontal_speed)*FIXED_ONE)/
							std::max<_fixed>(1,
								constants->maximum_forward_velocity)));
				const _fixed kick_damage_scale= PIN(
					(FIXED_ONE*7)/16+speed_ratio/2,
					(FIXED_ONE*7)/16,
					(FIXED_ONE*23)/16);

				const bool hit_monster= sprintathon_slide_attack(
					player->monster_index,
					player->facing,
					&player->camera_location,
					player->camera_polygon_index,
					kick_damage_scale);

				if (hit_monster)
				{
					// One half of the wall kick's horizontal rebound. Unlike a
					// wall kick this is additive, so it checks forward momentum
					// without turning every monster impact into a full reversal.
					const _fixed monster_rebound=
						(constants->maximum_forward_velocity*3)/8;
					variables->external_velocity.i-=
						(cosine_table[player->facing]*monster_rebound)>>
						TRIG_SHIFT;
					variables->external_velocity.j-=
						(sine_table[player->facing]*monster_rebound)>>
						TRIG_SHIFT;
					variables->external_velocity.k= std::max<_fixed>(
						variables->external_velocity.k, FIXED_ONE/24);
				}
			}
		}

		if (player->slide_ticks_remaining>0)
		{
			player->slide_time_fraction += bullet_time ? SPRINTATHON_BULLET_TIME_PERCENT : 100;
			if (player->slide_time_fraction >= 100)
			{
				player->slide_time_fraction -= 100;
				player->slide_ticks_remaining--;
			}
			if (player->slide_ticks_remaining==0)
			{
				variables->velocity= 0;
				variables->perpendicular_velocity= 0;

				// Together with the final eight slide ticks, this creates
				// a total 24-tick weapon recovery.
				player->slide_recovery_ticks= 16;
			}
		}
		else if (player->flying_kick_active && touching_ground)
		{
			player->flying_kick_active= false;
			player->flying_kick_ticks= 0;

			if (action_flags&_microphone_button)
			{
				variables->velocity= 0;
				variables->perpendicular_velocity= 0;
				player->flying_kick_landing_ticks= 12;
				player->flying_kick_exit_ticks= 0;
			}
			else player->flying_kick_exit_ticks= 8;
		}
	}

	if (player->flying_kick_recovery_pending && touching_ground)
	{
		player->flying_kick_recovery_pending= false;
		player->wall_kick_rearm_pending= false;
		player->wall_kick_cooldown_ticks= 0;
		if (action_flags&_microphone_button)
		{
			variables->velocity= 0;
			variables->perpendicular_velocity= 0;

			// The legs already retracted after release or wall impact. Start
			// only the stance/weapon recovery; replaying the landing leg phase
			// made the sprite pop back into view for a frame.
			player->flying_kick_landing_ticks= 0;
			player->flying_kick_exit_ticks= 0;
			player->slide_recovery_ticks= 24;
		}
	}

	if (player->flying_kick_exit_ticks>0)
		player->flying_kick_exit_ticks--;

	else if (player->flying_kick_landing_ticks>0)
	{
		variables->velocity= 0;
		variables->perpendicular_velocity= 0;
		player->flying_kick_landing_ticks--;
		if (player->flying_kick_landing_ticks==0)
			player->slide_recovery_ticks= 24;
	}
	else if (player->slide_recovery_ticks>0)
	{
		player->slide_recovery_ticks--;
	}

	if (player->dodge_ticks_remaining>0)
	{
		variables->velocity= 0;
		variables->perpendicular_velocity= 0;
		if (advance_dodge_animation)
			--player->dodge_ticks_remaining;
		if (player->dodge_ticks_remaining==0)
		{
			if (player->dodge_last_direction==2)
			{
				// Land decisively instead of retaining a long external glide.
				variables->external_velocity.i= 0;
				variables->external_velocity.j= 0;
				variables->external_velocity.k= 0;
				player->back_dodge_recovery_ticks= 26;
				player->slide_recovery_ticks= 26;
			}
			else
				player->slide_recovery_ticks= 16;
		}
	}
	else if (player->back_dodge_recovery_ticks>0 &&
		advance_dodge_animation)
	{
		--player->back_dodge_recovery_ticks;
	}

	if (player->dodge_auto_bullet_time &&
		player->dodge_ticks_remaining==0 &&
		player->back_dodge_recovery_ticks==0 &&
		!player->cartwheel_active &&
		!player->backflip_active &&
		player->slide_recovery_ticks==0)
	{
		if (player_is_local)
			set_sprintathon_bullet_time(false, false);
		player->dodge_auto_bullet_time= false;
	}

	const bool dry_grab_requested =
		(action_flags&_swim) &&
		((action_flags&_moving_forward) || (action_flags&_absolute_position_mode));
	if (modern_ledge_grab && dry_grab_requested && !player->sprinting && delta_z>0 &&
		variables->ledge_height!=INT16_MAX &&
		(variables->flags&_HORIZONTAL_COLLISION_BIT) &&
		!(variables->flags&_FEET_BELOW_MEDIA_BIT))
	{
		const _fixed ledge_top= WORLD_TO_FIXED(variables->ledge_height);
		const _fixed eye_height= variables->position.z+variables->actual_height-FIXED_ONE/8;
		if (eye_height+FIXED_ONE/6>=ledge_top && variables->position.z<ledge_top &&
			!(variables->flags&_DRY_MANTLING_BIT))
		{
			variables->flags|= _DRY_MANTLING_BIT;
			play_object_sound(player->object_index, _snd_human_hit, player_is_local);
		}
	}

	// Jumping during a wall run throws the player away from the wall.
	const bool wall_jump_button = (action_flags & _swim) != 0;

	if (!wall_jump_button)
		player->wall_jump_key_was_down = false;

	const bool can_wall_jump =
		modern_wall_jump && wall_jump_button &&
		!player->wall_jump_key_was_down &&
		player->wall_run_jump_cooldown_ticks==0 &&
		player->sprinting &&
		delta_z > 0 &&
		(variables->flags & _HORIZONTAL_COLLISION_BIT) &&
		shallow_wall_contact &&
		!(variables->flags & _FEET_BELOW_MEDIA_BIT);

	if (can_wall_jump)
	{
		const int64_t push_x = variables->wall_push_i;
		const int64_t push_y = variables->wall_push_j;
		const int64_t push_squared =
			push_x * push_x + push_y * push_y;
		const uint32 clamped_push_squared =
			push_squared > static_cast<int64_t>(UINT32_MAX)
				? UINT32_MAX
				: static_cast<uint32>(push_squared);
		const int32 push_magnitude =
			isqrt(clamped_push_squared);

		if (push_magnitude > 0)
		{
			const _fixed wall_jump_strength =
				(constants->maximum_forward_velocity * 5) / 4;

			// Add outward momentum while retaining movement along the wall.
			variables->external_velocity.i += static_cast<_fixed>(
				(push_x * wall_jump_strength) / push_magnitude);
			variables->external_velocity.j += static_cast<_fixed>(
				(push_y * wall_jump_strength) / push_magnitude);

			// A little less vertical force than the normal ground jump.
			variables->external_velocity.k =
				MAX(variables->external_velocity.k,
				    (_fixed)(FIXED_ONE / 16));

			play_object_sound(
				player->object_index,
				_snd_human_hit,
				player_is_local);

			// A wall jump consumes the current sprint.
			player->sprinting = false;
			player->sprint_ticks_remaining = 0;
			player->sprint_cooldown_ticks =
				2 * TICKS_PER_SECOND;
			player->wall_run_jump_cooldown_ticks=
				(TICKS_PER_SECOND*3)/4;
		}

		player->wall_jump_key_was_down = true;
	}

	/* change vertical_velocity based on difference between player height and surface height
		(if we are standing on an object, like a body, take that into account, too: this
		means a player could actually use bodies as ramps to reach ledges he couldn't
		otherwise jump to).  we should think about absorbing forward (or perpendicular)
		velocity to compensate for an increase in vertical velocity, which would slow down
		a player climbing stairs, etc. */
	if (delta_z<0)
	{
		variables->external_velocity.k= CEILING(variables->external_velocity.k+constants->climbing_acceleration, constants->terminal_velocity);
	}
	if (delta_z>0)
	{
		_fixed gravity= constants->gravitational_acceleration;
		_fixed terminal_velocity= constants->terminal_velocity;
		
		if (static_world->environment_flags&_environment_low_gravity) gravity>>= 1;
		if (variables->flags&_FEET_BELOW_MEDIA_BIT)
		{
			gravity >>= 1;
			terminal_velocity >>= 1;
		}
		else if (modern_jump)
		{
			// Experimental: faster, less floaty airborne movement.
			gravity = (gravity * 5) / 2;
			terminal_velocity = (terminal_velocity * 3) / 2;
		}
		if (modern_wall_run && player->sprinting &&
			(variables->flags&_HORIZONTAL_COLLISION_BIT) &&
			shallow_wall_contact &&
			!(variables->flags&_FEET_BELOW_MEDIA_BIT))
		{
			gravity= std::max<_fixed>(1, gravity/5);
			terminal_velocity= std::max<_fixed>(FIXED_ONE/96, terminal_velocity/3);
		}
		
		variables->external_velocity.k=
			FLOOR(variables->external_velocity.k-gravity, -terminal_velocity);
	}

	if (modern_ledge_grab && (variables->flags&_DRY_MANTLING_BIT))
	{
		const bool continuing=
			(action_flags&_swim) &&
			((action_flags&_moving_forward) || (action_flags&_absolute_position_mode)) &&
			(variables->flags&_HORIZONTAL_COLLISION_BIT) &&
			variables->ledge_height!=INT16_MAX;
		if (continuing)
		{
			const _fixed destination= WORLD_TO_FIXED(variables->ledge_height)+FIXED_ONE/32;
			if (variables->position.z<destination)
				variables->external_velocity.k= std::max<_fixed>(variables->external_velocity.k, FIXED_ONE/24);
			else
				variables->flags&= (uint16)~_DRY_MANTLING_BIT;
		}
		else variables->flags&= (uint16)~_DRY_MANTLING_BIT;
	}

	/*
	 * Modern swimming, water mantling and jumping.
	 *
	 * Holding Jump/Swim in water pulls the viewpoint toward a softly
	 * bobbing position above the surface. Pushing into a wall begins a
	 * persistent mantle which continues above the water until the wall
	 * clears, allowing traversal of high pool ledges.
	 */
	if (sprintathon &&
		((action_flags & _swim) || consume_buffered_jump))
	{
		const bool feet_in_water =
			variables->flags & _FEET_BELOW_MEDIA_BIT;
		const bool already_mantling =
			variables->flags & _WATER_MANTLING_BIT;
		const bool pushing_forward =
			(action_flags & _moving_forward) ||
			(action_flags & _absolute_position_mode);
		const bool touching_ledge =
			variables->flags & _HORIZONTAL_COLLISION_BIT;

		/*
		 * Ground contact takes priority over swimming. Previously the
		 * feet-in-media flag always entered this branch, so standing on a
		 * submerged floor made the normal jump path unreachable.
		 */
		if (modern_swimming && (feet_in_water || already_mantling) &&
			!touching_ground)
		{
			if (feet_in_water)
			{
				constexpr uint8 submerged_jump_sustain_ticks = 6;
				const bool preserve_ground_jump =
					(variables->flags & _SUBMERGED_GROUND_JUMP_BIT) &&
					variables->jump_grace_ticks <=
						jump_grace_limit + submerged_jump_sustain_ticks;

				if (preserve_ground_jump)
				{
					// Guarantee a clean takeoff through the floor/media transition.
					variables->external_velocity.k =
						std::max<_fixed>(
							variables->external_velocity.k,
							FIXED_ONE / 24);
				}
				else
				{
					variables->flags &=
						(uint16)~_SUBMERGED_GROUND_JUMP_BIT;
				}

				const _fixed eye_height =
					variables->actual_height - FIXED_ONE / 8;
				const _fixed surface_clearance = FIXED_ONE / 16;

				// Faster one-second surface bob.
				const angle bob_angle = NORMALIZE_ANGLE(
					(dynamic_world->tick_count * FULL_CIRCLE) /
						TICKS_PER_SECOND);
				const _fixed bob_height =
					((FIXED_ONE / 64) * sine_table[bob_angle]) >>
						TRIG_SHIFT;

				const _fixed target_feet_height =
					variables->media_height -
						eye_height +
						surface_clearance +
						bob_height;

				const _fixed surface_error =
					target_feet_height - variables->position.z;

				// Do not let the swimming spring swallow a submerged-floor jump.
				if (!preserve_ground_jump)
				{
					variables->external_velocity.k += surface_error / 10;
					variables->external_velocity.k -=
						variables->external_velocity.k / 6;

					variables->external_velocity.k = PIN(
						variables->external_velocity.k,
						-FIXED_ONE / 18,
						FIXED_ONE / 18);
				}

				const bool near_surface =
					std::abs(surface_error) < FIXED_ONE / 3;

				const bool head_above_water =
					!(variables->flags & _HEAD_BELOW_MEDIA_BIT);

				if ((near_surface || head_above_water) &&
					pushing_forward &&
					touching_ledge &&
					!(variables->flags & _WATER_MANTLING_BIT))
				{
					variables->flags |= _WATER_MANTLING_BIT;

					// Bob's effort sound when grabbing the ledge.
					play_object_sound(
						player->object_index,
						_snd_human_hit,
						player_is_local);
				}
			}

			if (variables->flags & _WATER_MANTLING_BIT)
			{
				/*
				 * Allow climbing somewhat above head height, while
				 * preventing indefinite travel up very tall walls.
				 */
				const _fixed maximum_mantle_height =
					variables->media_height +
					(constants->height * 2) / 3;

				if (pushing_forward &&
					touching_ledge &&
					variables->position.z < maximum_mantle_height)
				{
					// Slower sustained mantle rise.
					variables->external_velocity.k =
						std::max<_fixed>(
							variables->external_velocity.k,
							FIXED_ONE / 24);
				}
				else
				{
					variables->flags &=
						(uint16)~_WATER_MANTLING_BIT;
				}
			}

			variables->flags |= _JUMP_HELD_BIT;
		}
		else if (modern_jump && (!feet_in_water || touching_ground))
		{
			const bool jump_drains_stamina =
				input_preferences->sprintathon_stamina_jump;
			const bool can_jump =
				variables->jump_grace_ticks <= jump_grace_limit &&
				(!jump_drains_stamina ||
				 player->suit_oxygen >=
					(PLAYER_MAXIMUM_SUIT_OXYGEN*8)/100);

			if (can_jump &&
				(consume_buffered_jump ||
				 !(variables->flags & _JUMP_HELD_BIT)))
			{
				player->jump_buffer_ticks= 0;
				variables->external_velocity.k = FIXED_ONE / 13;

				// Charge once per accepted ground/coyote-time jump. Holding the
				// key, swimming, mantling and wall jumping do not repeat this cost.
				if (jump_drains_stamina)
				{
					const int16 jump_oxygen_cost=
						(PLAYER_MAXIMUM_SUIT_OXYGEN*5)/100;
					player->suit_oxygen= std::max<int16>(
						0, player->suit_oxygen-jump_oxygen_cost);
					player->flying_kick_oxygen_recharge_delay= 10;
				}

				if (feet_in_water)
					variables->flags |= _SUBMERGED_GROUND_JUMP_BIT;

                                // Half-Life-style long jump: crouch + forward + jump.
				if (modern_long_jump && (action_flags & _microphone_button) &&
                                    (action_flags & _moving_forward))
                                {
                                        const angle long_jump_direction =
                                                NORMALIZE_ANGLE(
                                                        FIXED_INTEGERAL_PART(
                                                                variables->direction));
                                        const _fixed long_jump_boost =
                                                (constants->maximum_forward_velocity * 3) / 4;

                                        variables->external_velocity.i +=
                                                (cosine_table[long_jump_direction] *
                                                 long_jump_boost) >> TRIG_SHIFT;
                                        variables->external_velocity.j +=
                                                (sine_table[long_jump_direction] *
                                                 long_jump_boost) >> TRIG_SHIFT;
                                }
				variables->jump_grace_ticks =
					jump_grace_limit + 1;

				play_object_sound(
					player->object_index,
					_snd_human_hit,
					player_is_local);
			}

			variables->flags |= _JUMP_HELD_BIT;
		}
	}
	else
	{
		variables->flags &=
			(uint16)~(_JUMP_HELD_BIT | _WATER_MANTLING_BIT |
				_SUBMERGED_GROUND_JUMP_BIT);
	}
	if (player->jump_buffer_ticks>0)
	{
		if (consume_buffered_jump)
			player->jump_buffer_ticks= 0;
		else
			--player->jump_buffer_ticks;
	}
	if ((!sprintathon || !modern_swimming) && (action_flags&_swim) &&
		(variables->flags&_HEAD_BELOW_MEDIA_BIT) &&
		variables->external_velocity.k<10*constants->climbing_acceleration)
	{
		variables->external_velocity.k+= constants->climbing_acceleration;
	}

	// Apply vertical angular velocity
	variables->elevation+= variables->vertical_angular_velocity;
	
	// Clamp virtual pitch to the effective limits of the low-precision physical pitch
	// (this won't enlarge the virtual pitch delta)
	if (player_is_local)
	{
		const fixed_angle min_pitch = FIXED_INTEGERAL_PART(-maximum_elevation) * FIXED_ONE;
		const fixed_angle max_pitch = FIXED_INTEGERAL_PART(maximum_elevation) * FIXED_ONE;
		const fixed_angle unclamped_physical_pitch = FIXED_INTEGERAL_PART(variables->elevation) * FIXED_ONE;
		const fixed_angle unclamped_virtual_pitch = unclamped_physical_pitch + vir_aim_delta.pitch;
		const fixed_angle clamped_physical_pitch = A1_PIN(unclamped_physical_pitch, min_pitch, max_pitch);
		const fixed_angle clamped_virtual_pitch = A1_PIN(unclamped_virtual_pitch, min_pitch, max_pitch);
		const fixed_angle new_delta = clamped_virtual_pitch - clamped_physical_pitch;
		assert(std::abs(new_delta) <= std::abs(vir_aim_delta.pitch));
		vir_aim_delta.pitch = new_delta;
	}
	
	// Clamp high-precision physical pitch to physics model limits
	// (note that the low-precision pitch can slightly violate a non-integral lower bound due to rounding toward -inf)
	variables->elevation= PIN(
		variables->elevation,
		-maximum_elevation,
		maximum_elevation);
	
	// If we're explicitly recentering and have reached or passed 0 pitch, stop at 0
	if ((variables->flags&_RECENTERING_BIT) && !(action_flags&_absolute_pitch_mode))
	{
		if ((variables->elevation<=0&&(action_flags&_looking_down))||(variables->elevation>=0&&(action_flags&_looking_up)))
		{
			variables->elevation= variables->vertical_angular_velocity= 0;
			variables->flags&= (uint16)~_RECENTERING_BIT;
		}
	}

	/* change the player’s heading based on his angular velocities */
	variables->last_direction= variables->direction;
	variables->direction+= variables->angular_velocity;
	if (variables->direction<0) variables->direction+= INTEGER_TO_FIXED(FULL_CIRCLE);
	if (variables->direction>=INTEGER_TO_FIXED(FULL_CIRCLE)) variables->direction-= INTEGER_TO_FIXED(FULL_CIRCLE);
	
	/* change the player’s x,y position based on his direction and velocities (parallel and perpendicular)  */
	new_position= variables->position;
	cosine= cosine_table[FIXED_INTEGERAL_PART(variables->direction)], sine= sine_table[FIXED_INTEGERAL_PART(variables->direction)];
	_fixed movement_forward = variables->velocity;
	_fixed movement_sideways = variables->perpendicular_velocity;

	if (sprintathon && player->sprinting)
	{
		// Build the 50% sprint bonus over 0.8 seconds. Repeatedly tapping
		// Sprint therefore spends oxygen without ever reaching full speed.
		const int sprint_ramp_duration =
			(TICKS_PER_SECOND * 4) / 5;
		const int sprint_ramp = std::min<int>(
			player->sprint_ramp_ticks, sprint_ramp_duration);
		movement_forward +=
			(movement_forward * sprint_ramp) /
			(2 * sprint_ramp_duration);
		movement_sideways +=
			(movement_sideways * sprint_ramp) /
			(2 * sprint_ramp_duration);
	}

	_fixed movement_delta_x=
		(movement_forward*cosine-movement_sideways*sine)>>TRIG_SHIFT;
	_fixed movement_delta_y=
		(movement_forward*sine+movement_sideways*cosine)>>TRIG_SHIFT;
	if (bullet_time)
	{
		movement_delta_x= sprintathon_scale_for_bullet_time(movement_delta_x);
		movement_delta_y= sprintathon_scale_for_bullet_time(movement_delta_y);
	}
	new_position.x+= movement_delta_x;
	new_position.y+= movement_delta_y;
	
	/* set above/below floor flags, remember old flags */
	variables->old_flags= variables->flags;
	if (new_position.z<variables->floor_height) variables->flags|= _BELOW_GROUND_BIT; else variables->flags&= (uint16)~_BELOW_GROUND_BIT;
	if (new_position.z>variables->floor_height) variables->flags|= _ABOVE_GROUND_BIT; else variables->flags&= (uint16)~_ABOVE_GROUND_BIT;

	/* if we just landed on the ground, or we just came up through the ground, absorb some of
		the player’s external_velocity.k (and in the case of hitting the ground, reflect it) */
	if (variables->external_velocity.k>0 && (variables->old_flags&_BELOW_GROUND_BIT) && !(variables->flags&_BELOW_GROUND_BIT))
	{
		variables->external_velocity.k/= 2*COEFFICIENT_OF_ABSORBTION; /* slow down */
	}
	if (variables->external_velocity.k>0 && new_position.z+variables->actual_height>=variables->ceiling_height)
	{
		variables->external_velocity.k/= -COEFFICIENT_OF_ABSORBTION, new_position.z= variables->ceiling_height-variables->actual_height; // &&variables->position.z+variables->actual_height<variables->ceiling_height
	}
	if (variables->external_velocity.k<0&&!(variables->old_flags&_BELOW_GROUND_BIT)&&!(variables->flags&_ABOVE_GROUND_BIT))
	{
		if (sprintathon)
		{
			// Add a brief visual-only downward dip on impact. Ignore tiny floor
			// corrections, scale ordinary landings by their downward speed, and
			// cap hard falls so they do not jerk the player's actual aim.
			const _fixed landing_speed= -variables->external_velocity.k;
			const _fixed landing_threshold= FIXED_ONE/96;
			const _fixed landing_full_scale= FIXED_ONE/12;
			if (landing_speed>landing_threshold)
			{
				const _fixed scaled_speed= std::min<_fixed>(
					landing_speed-landing_threshold,
					landing_full_scale-landing_threshold);
				const int16 maximum_landing_dip= (FULL_CIRCLE*3)/360;
				const int16 landing_dip= std::max<int16>(1,
					static_cast<int16>(
						(static_cast<int64_t>(maximum_landing_dip)*scaled_speed)/
						(landing_full_scale-landing_threshold)));
				player->sprintathon_camera_pitch= std::min<int16>(
					(FULL_CIRCLE*8)/360,
					player->sprintathon_camera_pitch+landing_dip);
			}
		}
		variables->external_velocity.k/= -COEFFICIENT_OF_ABSORBTION;
	}

	_fixed small_enough_velocity;
	if (get_monster_definition_external(_monster_marine)->flags & _monster_can_grenade_climb) {
		_fixed gravity= constants->gravitational_acceleration;		
		if (static_world->environment_flags&_environment_low_gravity) gravity>>= 1;
		if (variables->flags&_FEET_BELOW_MEDIA_BIT) gravity>>= 1;

		small_enough_velocity = gravity;
	} 
	else 
	{
		small_enough_velocity = SMALL_ENOUGH_VELOCITY;
	}
	if (std::abs(variables->external_velocity.k)<small_enough_velocity &&
		std::abs(variables->floor_height-new_position.z)<CLOSE_ENOUGH_TO_FLOOR)
	{
		variables->external_velocity.k= 0, new_position.z= variables->floor_height;
		variables->flags&= ~(_BELOW_GROUND_BIT|_ABOVE_GROUND_BIT);
	}

	/* change the player’s z position based on his vertical velocity (if we hit the ground coming down
		then bounce and absorb most of the blow */
	new_position.x+= bullet_time ?
		sprintathon_scale_for_bullet_time(variables->external_velocity.i) :
		variables->external_velocity.i;
	new_position.y+= bullet_time ?
		sprintathon_scale_for_bullet_time(variables->external_velocity.j) :
		variables->external_velocity.j;
	new_position.z+= bullet_time ?
		sprintathon_scale_for_bullet_time(variables->external_velocity.k) :
		variables->external_velocity.k;
	
	{
		short dx= variables->external_velocity.i, dy= variables->external_velocity.j;
		_fixed delta= (delta_z<=0) ? constants->external_deceleration : (constants->external_deceleration>>2);
		int32 magnitude= isqrt(dx*dx + dy*dy);

		if (magnitude && magnitude> std::abs(delta))
		{
			variables->external_velocity.i-= (dx*delta)/magnitude;
			variables->external_velocity.j-= (dy*delta)/magnitude;
		}
		else
		{
			variables->external_velocity.i= variables->external_velocity.j= 0;
		}
	}

	/* lower the player’s externally-induced angular velocity */
	variables->external_angular_velocity= (variables->external_angular_velocity>=0) ?
		FLOOR(variables->external_angular_velocity-constants->external_angular_deceleration, 0) :
		CEILING(variables->external_angular_velocity+constants->external_angular_deceleration, 0);

	/* instantiate new position, save old position */
	variables->last_position= variables->position;
	variables->position= new_position;

	/* if the player is moving, adjust step_phase by step_delta (if the player isn’t moving
		continue to adjust step_phase until it is zero)  if the player is in the air, don’t
		update phase until he lands. */
	variables->flags&= (uint16)~_STEP_PERIOD_BIT;
	/*
	 * Local first-person footsteps use one universal sample. Cadence follows
	 * horizontal speed, while states that should not sound like ordinary
	 * running reset the timer so movement resumes with a prompt first step.
	 */
	const _fixed footstep_speed= std::max<_fixed>(
		std::abs(movement_forward),
		std::abs(movement_sideways));
	const _fixed footstep_reference_speed= std::max<_fixed>(
		1,
		get_physics_constants_for_model(
			static_world->physics_model,
			_run_dont_walk)->maximum_forward_velocity);
	const bool wall_running_for_footsteps=
		modern_wall_run && player->sprinting &&
		(variables->flags&_HORIZONTAL_COLLISION_BIT) &&
		(variables->flags&_ABOVE_GROUND_BIT) &&
		shallow_wall_contact;
	const bool grounded_for_footsteps=
		!(variables->flags&_ABOVE_GROUND_BIT);
	const bool directional_input_for_footsteps=
		(action_flags&_sidestepping) ||
		((action_flags&_absolute_position_mode)
			? GET_ABSOLUTE_POSITION(action_flags)!=MAXIMUM_ABSOLUTE_POSITION/2
			: (action_flags&_moving));
	const bool footsteps_active=
		player_is_local && sprintathon &&
		directional_input_for_footsteps &&
		(grounded_for_footsteps || wall_running_for_footsteps) &&
        (!(variables->flags&_FEET_BELOW_MEDIA_BIT) ||
         (grounded_for_footsteps && !(variables->flags&_HEAD_BELOW_MEDIA_BIT))) &&
		!PLAYER_IS_DEAD(player) &&
		player->slide_ticks_remaining==0 &&
		!player->flying_kick_active &&
		player->flying_kick_landing_ticks==0 &&
		!(action_flags&_microphone_button) &&
		footstep_speed>footstep_reference_speed/8;
	_fixed synchronized_movement_step_phase= -1;

	if (!footsteps_active)
	{
		player->footstep_ticks_remaining= 0;
	}
	else
	{
		const int normal_footstep_interval= player->sprinting ? 7 :
			((action_flags&_run_dont_walk) ? 12 : 23);
		// This countdown also drives the synchronized run/sprint weapon bob.
		// Stretching it therefore keeps both footsteps and bob at the same 35%
		// rate as movement during bullet time.
		const int footstep_interval= bullet_time ?
			(normal_footstep_interval*100+SPRINTATHON_BULLET_TIME_PERCENT-1)/
				SPRINTATHON_BULLET_TIME_PERCENT :
			normal_footstep_interval;

		// Never retain a slower mode's long countdown after accelerating.
		const uint8 cadence_countdown= static_cast<uint8>(
			std::max(1, footstep_interval)-1);
		player->footstep_ticks_remaining= std::min(
			player->footstep_ticks_remaining, cadence_countdown);

		if (player->footstep_ticks_remaining>0)
		{
			player->footstep_ticks_remaining--;
		}
		else
		{
			if (input_preferences->sprintathon_footsteps)
				sprintathon_play_footstep_sound(
					player->monster_index, player->footstep_alternate);
			player->footstep_alternate= !player->footstep_alternate;
			player->footstep_ticks_remaining= cadence_countdown;
		}

		// Run bob completes once per step. Sprint bob advances half a cycle per
		// step for a smoother two-step stride, independent of scenario physics.
		if (((action_flags&_run_dont_walk) || player->sprinting) &&
			cadence_countdown>0)
		{
			const int16 elapsed= cadence_countdown-
				player->footstep_ticks_remaining;
			const angle run_bob_angle= player->sprinting
				? NORMALIZE_ANGLE(static_cast<angle>(
					(player->footstep_alternate ? QUARTER_CIRCLE : 3*QUARTER_CIRCLE) +
					(static_cast<int32>(elapsed)*HALF_CIRCLE)/cadence_countdown))
				: NORMALIZE_ANGLE(static_cast<angle>(
					QUARTER_CIRCLE +
					(static_cast<int32>(elapsed)*FULL_CIRCLE)/cadence_countdown));
			synchronized_movement_step_phase= static_cast<_fixed>(
				(static_cast<int64_t>(run_bob_angle)*FIXED_ONE)/FULL_CIRCLE);
		}
	}

	if (constants->maximum_forward_velocity)
		variables->step_amplitude= (MAX(std::abs(variables->velocity), std::abs(variables->perpendicular_velocity))*FIXED_ONE)/constants->maximum_forward_velocity;
	else	// CB: "Missed Island" physics would produce a division by 0
		variables->step_amplitude= MAX(std::abs(variables->velocity), std::abs(variables->perpendicular_velocity))*FIXED_ONE;
	if (delta_z>=0)
	{
		if (variables->velocity||variables->perpendicular_velocity)
		{
//			fixed old_step_phase= variables->step_phase;
			
			if ((variables->step_phase+= constants->step_delta)>=FIXED_ONE)
			{
				variables->step_phase-= FIXED_ONE;
				variables->flags|= _STEP_PERIOD_BIT;
			}
//			else
//			{
//				if (variables->step_phase>=FIXED_ONE_HALF && old_step_phase<FIXED_ONE_HALF)
//				{
//					variables->flags|= _STEP_PERIOD_BIT;
//				}
//			}
		}
		else
		{
			if (variables->step_phase)
			{
				if (variables->step_phase>FIXED_ONE_HALF)
				{
					if ((variables->step_phase+= constants->step_delta)>=FIXED_ONE) variables->step_phase= 0;
				}
				else
				{
					if ((variables->step_phase-= constants->step_delta)<0) variables->step_phase= 0;
				}
			}
		}
	}
	if (synchronized_movement_step_phase>=0)
		variables->step_phase= synchronized_movement_step_phase;

	if (delta_z >= (PLAYER_IS_DEAD(player) ? (AIRBORNE_HEIGHT+DROP_DEAD_HEIGHT) : AIRBORNE_HEIGHT))
	{
		variables->action= _player_airborne;
	}
	else
	{
		if (variables->angular_velocity||variables->velocity||variables->perpendicular_velocity)
		{
			variables->action= (action_flags&_run_dont_walk) ? _player_running : _player_walking;
		}
		else
		{
			variables->action= (variables->external_velocity.i||variables->external_velocity.j||variables->external_velocity.k) ? _player_sliding : _player_stationary;
		}
	}
}


uint8 *unpack_physics_constants(uint8 *Stream, size_t Count)
{
	return unpack_physics_constants(Stream,physics_models,Count);
}

uint8 *unpack_physics_constants(uint8 *Stream, physics_constants *Objects, size_t Count)
{
	uint8* S = Stream;
	physics_constants* ObjPtr = Objects;
	
	for (size_t k = 0; k < Count; k++, ObjPtr++)
	{
		StreamToValue(S,ObjPtr->maximum_forward_velocity);
		StreamToValue(S,ObjPtr->maximum_backward_velocity);
		StreamToValue(S,ObjPtr->maximum_perpendicular_velocity);
		StreamToValue(S,ObjPtr->acceleration);
		StreamToValue(S,ObjPtr->deceleration);
		StreamToValue(S,ObjPtr->airborne_deceleration);
		StreamToValue(S,ObjPtr->gravitational_acceleration);
		StreamToValue(S,ObjPtr->climbing_acceleration);
		StreamToValue(S,ObjPtr->terminal_velocity);
		StreamToValue(S,ObjPtr->external_deceleration);

		StreamToValue(S,ObjPtr->angular_acceleration);
		StreamToValue(S,ObjPtr->angular_deceleration);
		StreamToValue(S,ObjPtr->maximum_angular_velocity);
		StreamToValue(S,ObjPtr->angular_recentering_velocity);
		StreamToValue(S,ObjPtr->fast_angular_velocity);
		StreamToValue(S,ObjPtr->fast_angular_maximum);
		StreamToValue(S,ObjPtr->maximum_elevation);
		StreamToValue(S,ObjPtr->external_angular_deceleration);
		
		StreamToValue(S,ObjPtr->step_delta);
		StreamToValue(S,ObjPtr->step_amplitude);
		StreamToValue(S,ObjPtr->radius);
		StreamToValue(S,ObjPtr->height);
		StreamToValue(S,ObjPtr->dead_height);
		StreamToValue(S,ObjPtr->camera_height);
		StreamToValue(S,ObjPtr->splash_height);
		
		StreamToValue(S,ObjPtr->half_camera_separation);
	}
	
	assert((S - Stream) == static_cast<ptrdiff_t>(Count*SIZEOF_physics_constants));
	return S;
}

uint8* unpack_m1_physics_constants(uint8* Stream, size_t Count)
{
	static const int SIZEOF_old_physics_entry = 100;
	uint8* S = Stream + SIZEOF_old_physics_entry; // first is "editor" record
	physics_constants* ObjPtr = physics_models;
	
	for (size_t k = 0; k < Count - 1; k++, ObjPtr++)
	{
		StreamToValue(S,ObjPtr->maximum_forward_velocity);
		StreamToValue(S,ObjPtr->maximum_backward_velocity);
		StreamToValue(S,ObjPtr->maximum_perpendicular_velocity);
		StreamToValue(S,ObjPtr->acceleration);
		StreamToValue(S,ObjPtr->deceleration);
		StreamToValue(S,ObjPtr->airborne_deceleration);
		StreamToValue(S,ObjPtr->gravitational_acceleration);
		StreamToValue(S,ObjPtr->climbing_acceleration);
		StreamToValue(S,ObjPtr->terminal_velocity);
		StreamToValue(S,ObjPtr->external_deceleration);

		StreamToValue(S,ObjPtr->angular_acceleration);
		StreamToValue(S,ObjPtr->angular_deceleration);
		StreamToValue(S,ObjPtr->maximum_angular_velocity);
		StreamToValue(S,ObjPtr->angular_recentering_velocity);
		StreamToValue(S,ObjPtr->fast_angular_velocity);
		StreamToValue(S,ObjPtr->fast_angular_maximum);
		StreamToValue(S,ObjPtr->maximum_elevation);
		StreamToValue(S,ObjPtr->external_angular_deceleration);
		
		StreamToValue(S,ObjPtr->step_delta);
		StreamToValue(S,ObjPtr->step_amplitude);
		StreamToValue(S,ObjPtr->radius);
		StreamToValue(S,ObjPtr->height);
		StreamToValue(S,ObjPtr->dead_height);
		StreamToValue(S,ObjPtr->camera_height);
		ObjPtr->splash_height = 0;
		
		StreamToValue(S,ObjPtr->half_camera_separation);
	}
	
	return S;
}

uint8 *pack_physics_constants(uint8 *Stream, size_t Count)
{
	return pack_physics_constants(Stream,physics_models,Count);
}

uint8 *pack_physics_constants(uint8 *Stream, physics_constants *Objects, size_t Count)
{
	uint8* S = Stream;
	physics_constants* ObjPtr = Objects;
	
	for (size_t k = 0; k < Count; k++, ObjPtr++)
	{
		ValueToStream(S,ObjPtr->maximum_forward_velocity);
		ValueToStream(S,ObjPtr->maximum_backward_velocity);
		ValueToStream(S,ObjPtr->maximum_perpendicular_velocity);
		ValueToStream(S,ObjPtr->acceleration);
		ValueToStream(S,ObjPtr->deceleration);
		ValueToStream(S,ObjPtr->airborne_deceleration);
		ValueToStream(S,ObjPtr->gravitational_acceleration);
		ValueToStream(S,ObjPtr->climbing_acceleration);
		ValueToStream(S,ObjPtr->terminal_velocity);
		ValueToStream(S,ObjPtr->external_deceleration);

		ValueToStream(S,ObjPtr->angular_acceleration);
		ValueToStream(S,ObjPtr->angular_deceleration);
		ValueToStream(S,ObjPtr->maximum_angular_velocity);
		ValueToStream(S,ObjPtr->angular_recentering_velocity);
		ValueToStream(S,ObjPtr->fast_angular_velocity);
		ValueToStream(S,ObjPtr->fast_angular_maximum);
		ValueToStream(S,ObjPtr->maximum_elevation);
		ValueToStream(S,ObjPtr->external_angular_deceleration);
		
		ValueToStream(S,ObjPtr->step_delta);
		ValueToStream(S,ObjPtr->step_amplitude);
		ValueToStream(S,ObjPtr->radius);
		ValueToStream(S,ObjPtr->height);
		ValueToStream(S,ObjPtr->dead_height);
		ValueToStream(S,ObjPtr->camera_height);
		ValueToStream(S,ObjPtr->splash_height);
		
		ValueToStream(S,ObjPtr->half_camera_separation);
	}
	
	assert((S - Stream) == static_cast<ptrdiff_t>(Count*SIZEOF_physics_constants));
	return S;
}

void init_physics_constants()
{
	memcpy(physics_models, original_physics_models, sizeof(physics_models));
}

// LP addition: get number of physics models (restricted sense)
size_t get_number_of_physics_models() {return NUMBER_OF_PHYSICS_MODELS;}
