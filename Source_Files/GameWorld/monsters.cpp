/*
MONSTERS.C

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

Tuesday, November 10, 1992 1:10:20 PM

Friday, May 27, 1994 11:21:07 AM
	split into MONSTERS.C, PROJECTILES.C and EFFECTS.C; unified active_monster and monster array.
Friday, September 30, 1994 5:48:25 PM (Jason)
	started adding comments again.  damage_monsters_in_radius() is less forgiving in z now.
Monday, December 5, 1994 9:07:37 PM  (Jason)
	rebellion environment function (all _clients hate all _pfhor).
Wednesday, February 1, 1995 2:29:01 AM  (Jason')
	kill_sounds; invisible monsters don’t move
Wednesday, June 14, 1995 10:14:24 AM  (Jason)
	rewrite for marathon2 (halfway done).
Monday, July 10, 1995 11:49:06 AM  (Jason)
	rewrite for marathon2 done.  my bobs won’t listen to your fucking whining.

Jan 30, 2000 (Loren Petrich):
	Added some typecasts
	Removed some "static" declarations that conflict with "extern"

Feb 3, 2000 (Loren Petrich):
	Treating Jjaro goo like sewage

Feb. 4, 2000 (Loren Petrich):
	Changed halt() to assert(false) for better debugging

Feb 6, 2000 (Loren Petrich):
	Added access to size of monster-definition structure

Feb 12, 2000 (Loren Petrich):
	Suppressed an exposed "dprintf" as an unnecessary interrupt.

Feb 16, 2000 (Loren Petrich):
	Added a check on the polygon index after a line-transparency check;
	this is in case there is no polygon on the other side.

Feb 17, 2000 (Loren Petrich):
	Fixed stuff near GUESS_HYPOTENUSE() to be long-distance-friendly

Feb 19, 2000 (Loren Petrich):
	Added growable lists of indices of objects to be checked for collisions

Feb 24, 2000 (Loren Petrich):
	Suppressed some asserts about monster speeds

Apr 27, 2000 (Loren Petrich):
	Added some behavior in the case of a monster both floating and flying
	to handle the map "Aqualung" correctly.

May 29, 2000 (Loren Petirch):
	Fixed side effect of fixing keyframe-never-zero bug:
	if the keyframe is zero, then a sequence never triggers shrapnel damage.
	Thus, Hunters die a soft death more harmlessly.

Jun 11, 2000 (Loren Petrich):
	Pegging health and oxygen to maximum values when damaged;
	takes into account negative damage from healing projectiles.

Jul 1, 2000 (Loren Petrich):
	Inlined the accessors

Aug 30, 2000 (Loren Petrich):
	Added stuff for unpacking and packing
	
Oct 13, 2000 (Loren Petrich)
	Converted the intersected-objects list into a Standard Template Library vector

Oct 26, 2000 (Mark Levin)
	Revealed a few functions needed by Pfhortran

Jan 12, 2003 (Loren Petrich)
	Added controllable damage kicks
*/

#include <string.h>
#include <limits.h>
#include <map>
#include <cmath>
#include <algorithm>
#include <set>

#include "cseries.h"
#include "map.h"
#include "render.h"
#include "interface.h"
#include "FilmProfile.h"
#include "flood_map.h"
#include "effects.h"
#include "monsters.h"
#include "projectiles.h"
#include "player.h"
#include "preferences.h"
#include "vbl.h"
#include "platforms.h"
#include "scenery.h"
#include "SoundManager.h"
#include "fades.h"
#include "items.h"
#include "media.h"
#include "Packing.h"
#include "lua_script.h"
#include "Logging.h"
#include "InfoTree.h"


/*
//explosive deaths should cause damage during their key frame
*/

/* ---------- sounds */

/* ---------- constants */

#define OBSTRUCTION_DEACTIVATION_MASK 0x7

#define EVASIVE_MANOUVER_DISTANCE WORLD_ONE_HALF

#define MONSTER_EXTERNAL_DECELERATION (WORLD_ONE/200)
#define MONSTER_MINIMUM_EXTERNAL_VELOCITY (10*MONSTER_EXTERNAL_DECELERATION)
#define MONSTER_MAXIMUM_EXTERNAL_VELOCITY (TICKS_PER_SECOND*MONSTER_EXTERNAL_DECELERATION)

/* the height below which we don’t bother to float up a ledge (we just run right over it) */
#define MINIMUM_FLOATING_HEIGHT WORLD_ONE_FOURTH

#define MINIMUM_ACTIVATION_SEPARATION TICKS_PER_SECOND

/* when looking for things under or at this light intensity the monster must use his dark visual range */
#define LOW_LIGHT_INTENSITY 0

/* maximum area we will search out to find a new target */
#define MAXIMUM_TARGET_SEARCH_AREA (7*WORLD_ONE*WORLD_ONE)

#define MONSTER_PLATFORM_BUFFER_DISTANCE (WORLD_ONE/8)

#define GLUE_TRIGGER_ACTIVATION_RANGE (8*WORLD_ONE)
#define MONSTER_ALERT_ACTIVATION_RANGE (5*WORLD_ONE)

#define MONSTER_PATHFINDING_OBSTRUCTION_COST (2*WORLD_ONE*WORLD_ONE)
#define MONSTER_PATHFINDING_PLATFORM_COST (4*WORLD_ONE*WORLD_ONE)
#define MINIMUM_MONSTER_PATHFINDING_POLYGON_AREA (WORLD_ONE)

#define TERMINAL_VERTICAL_MONSTER_VELOCITY (WORLD_ONE/5)

#define MINIMUM_DYING_EXTERNAL_VELOCITY (WORLD_ONE/8)

#define CIVILIANS_KILLED_BY_PLAYER_THRESHHOLD 3
#define CIVILIANS_KILLED_DECREMENT_MASK 0x1ff

enum /* monster attitudes, extracted from enemies and friends bitfields by get_monster_attitude() */
{
	_neutral,
	_friendly,
	_hostile
};

enum /* returned by find_obstructing_terrain_feature() */
{
	_standing_on_sniper_ledge,
	_entering_platform_polygon,
	_leaving_platform_polygon,
	_flying_or_floating_transition
};

#define MINIMUM_SNIPER_ELEVATION WORLD_ONE_HALF

/* ---------- structures */

struct monster_pathfinding_data
{
	struct monster_definition *definition;
	struct monster_data *monster;
	
	bool cross_zone_boundaries;
};

// How much external velocity is imparted by some damage?
struct damage_kick_definition
{
	short base_value;
	float delta_vitality_multiplier;
	bool is_also_vertical;

	// if non-zero, will enable vertical_component if
	// delta_vitality is greater than threshold
	short vertical_threshold;

	// whether monsters die a hard death, or in flames
	short death_action;
};

/* ---------- definitions */

// LP: implements commented-out damage-kick code
struct damage_kick_definition damage_kick_definitions[NUMBER_OF_DAMAGE_TYPES] = 
{
	{0, 1, true, 0, _monster_is_dying_hard}, // _damage_explosion,
	{0, 3, true, 0, _monster_is_dying_soft}, // _damage_electrical_staff,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_projectile,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_absorbed,
	{0, 1, false, 0, _monster_is_dying_flaming}, // _damage_flame,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_hound_claws,
	{0, 1, false, 0, _monster_is_dying_flaming}, // _damage_alien_projectile,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_hulk_slap,
	{0, 3, true, 0, _monster_is_dying_soft}, // _damage_compiler_bolt,
	{0, 0, false, 100, _monster_is_dying_soft}, // _damage_fusion_bolt,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_hunter_bolt,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_fist,
	{250, 0, false, 0, _monster_is_dying_soft}, // _damage_teleporter,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_defender,
	{0, 3, true, 0, _monster_is_dying_soft}, // _damage_yeti_claws,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_yeti_projectile,
	{0, 1, false, 0, _monster_is_dying_hard}, // _damage_crushing,
	{0, 1, false, 0, _monster_is_dying_flaming}, // _damage_lava,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_suffocation,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_goo,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_energy_drain,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_oxygen_drain,
	{0, 1, false, 0, _monster_is_dying_soft}, // _damage_hummer_bolt,
	{0, 0, true, 0, _monster_is_dying_soft} // _damage_shotgun_projectile,
};

/* ---------- globals */

/* import monster definition constants, structures and globals */
#include "monster_definitions.h"

/* ---------- private prototypes */

static monster_definition *get_monster_definition(
	const short type);

static void monster_needs_path(short monster_index, bool immediately);
static void generate_new_path_for_monster(short monster_index);
void advance_monster_path(short monster_index);

static short get_monster_attitude(short monster_index, short target_index);
void change_monster_target(short monster_index, short target_index);
static bool switch_target_check(short monster_index, short attacker_index, short delta_vitality);
static bool clear_line_of_sight(short viewer_index, short target_index, bool full_circle);

static void handle_moving_or_stationary_monster(short monster_index);
static bool sprintathon_flock_move(short index, bool* moved);
static bool sprintathon_recover_ground_overlap(short index);
static bool sprintathon_spring_body(short index);
static bool sprintathon_spring_pair(short first, short second);
static world_distance sprintathon_spring_separation(world_distance separation);
static void sprintathon_update_body_springs();
static void sprintathon_queue_body_push(short source,short target,double x,double y,int hops);
static void sprintathon_begin_character_contact(short index);
static void sprintathon_end_character_contact(short index);
static double sprintathon_approach_curve(short index, double remaining, angle heading);
struct SprintathonCurveWaypoint {
    short object;
    world_point2d point;
};
static std::map<short, SprintathonCurveWaypoint> sprintathon_curve_waypoints;
static void sprintathon_reset_flock();
static bool sprintathon_ground_ai(const monster_data*, const monster_definition*);
static void execute_monster_attack(short monster_index);
static void kill_monster(short monster_index);
static bool translate_monster(short monster_index, world_distance distance);
static void sprintathon_float_coast(short monster_index);
static bool try_monster_attack(short monster_index);
			
int32 monster_pathfinding_cost_function(short source_polygon_index, short line_index,
	short destination_polygon_index, void *data);
int32 monster_m1_trigger_flood_proc(short source_polygon_index, short line_index,
                                    short destination_polygon_index, void *data);

void set_monster_action(short monster_index, short action);
void set_monster_mode(short monster_index, short new_mode, short target_index);

static short find_obstructing_terrain_feature(short monster_index, short *feature_index, short *relevant_polygon_index);

static short position_monster_projectile(short aggressor_index, short target_index, struct attack_definition *attack,
	world_point3d *origin, world_point3d *destination, world_point3d *_vector, angle theta);

static void update_monster_vertical_physics_model(short monster_index);
static void update_monster_physics_model(short monster_index);

static int32 monster_activation_flood_proc(short source_polygon_index, short line_index,
	short destination_polygon_index, void *data);

static bool attempt_evasive_manouvers(short monster_index);

static short nearest_goal_polygon_index(short polygon_index);
static int32 nearest_goal_cost_function(short source_polygon_index, short line_index,
	short destination_polygon_index, void *unused);

static void cause_shrapnel_damage(short monster_index);

// For external use
monster_definition *get_monster_definition_external(const short type);

/* ---------- code */

static bool mTYPE_IS_ENEMY(monster_definition *definition, short type)
{
    if (static_world->environment_flags & _environment_rebellion_m1)
    {
        if (definition->_class & _class_client_m1)
        {
            return (get_monster_definition(type)->_class & _class_pfhor_m1);
        }
        else if (definition->_class & _class_pfhor_m1)
        {
            if (get_monster_definition(type)->_class & _class_client_m1)
            {
                return true;
            }
        }
    }
    return TYPE_IS_ENEMY(definition, type);
}


monster_data *get_monster_data(
	short monster_index)
{
	struct monster_data *monster = GetMemberWithBounds(monsters,monster_index,MAXIMUM_MONSTERS_PER_MAP);
	
	vassert(monster, csprintf(temporary, "monster index #%d is out of range", monster_index));
	vassert(SLOT_IS_USED(monster), csprintf(temporary, "monster index #%d (%p) is unused", monster_index, (void*)monster));
	
	return monster;
}

monster_definition *get_monster_definition(
	const short type)
{
	monster_definition *definition = GetMemberWithBounds(monster_definitions,type,NUMBER_OF_MONSTER_TYPES);
	assert(definition);
	
	return definition;
}

//a non-inlined version for external use
monster_definition *get_monster_definition_external(
	const short type)
{
	return get_monster_definition(type);
}


/* returns new monster index if successful, NONE otherwise */
short new_monster(
	struct object_location *location,
	short monster_type)
{
	struct monster_definition *definition= get_monster_definition(monster_type);
	short original_monster_type= monster_type;
	struct monster_data *monster;
	short drop_mask= NONE;
	short monster_index= NONE;
	short flags= _monster_has_never_been_activated;

	switch (dynamic_world->game_information.difficulty_level)
	{
		case _wuss_level: drop_mask= 3; break; /* drop every fourth monster */
		case _easy_level: drop_mask= 7; break; /* drop every eighth monster */
		/* otherwise, drop no monsters */
	}
	
	if ((definition->flags&_monster_cannot_be_dropped) || !(definition->flags&_monster_is_alien) || drop_mask==NONE || (++dynamic_world->new_monster_vanishing_cookie&drop_mask))
	{
		/* check to see if we should promote or demote this monster based on difficulty level */
		if (definition->flags&_monster_major)
		{
			short demote_mask= NONE;
			
			switch (dynamic_world->game_information.difficulty_level)
			{
				case _wuss_level: demote_mask= 1; break; /* demote every other major */
				case _easy_level: demote_mask= 3; break; /* demote every fourth major */
				/* otherwise, demote no monsters */
			}
			
			if (demote_mask!=NONE && !(++dynamic_world->new_monster_mangler_cookie&demote_mask)) definition= get_monster_definition(monster_type-= 1), flags|= _monster_was_demoted;
		}
		else
		{
			if (definition->flags&_monster_minor)
			{
				short promote_mask= NONE;
				
				switch (dynamic_world->game_information.difficulty_level)
				{
					case _major_damage_level: promote_mask= 1; break; /* promote every other minor */
					case _total_carnage_level: promote_mask= 0; break; /* promote every minor */
					/* otherwise, promote no monsters */ 
				}
				if (promote_mask!=NONE && !(++dynamic_world->new_monster_mangler_cookie&promote_mask)) definition= get_monster_definition(monster_type+= 1), flags|= _monster_was_promoted;
			}
		}
		
		for (monster_index=0,monster=monsters;monster_index<MAXIMUM_MONSTERS_PER_MAP;++monster_index,++monster)
		{
			if (SLOT_IS_FREE(monster))
			{
				short object_index= new_map_object(location, BUILD_DESCRIPTOR(definition->collection, definition->stationary_shape));
				
				if (object_index!=NONE)
				{
					struct object_data *object= get_object_data(object_index);

					/* not doing this in !DEBUG resulted in sync errors; mmm... random data, so tasty */
					obj_set(*monster, 0x80);
	
					if (location->flags&_map_object_is_blind) flags|= _monster_is_blind;
					if (location->flags&_map_object_is_deaf) flags|= _monster_is_deaf;
					if (location->flags&_map_object_floats) flags|= _monster_teleports_out_when_deactivated;
				
					/* initialize the monster_data structure; we don’t touch most of the fields here
						because the monster is initially inactive (and they will be initialized when the
						monster is activated) */
					monster->type= monster_type;
					monster->activation_bias= DECODE_ACTIVATION_BIAS(location->flags);
					monster->vitality= NONE; /* if a monster is activated with vitality==NONE, it will be properly initialized */
					monster->object_index= object_index;
					monster->flags= flags;
					monster->goal_polygon_index= monster->activation_bias==_activate_on_goal ?
						nearest_goal_polygon_index(location->polygon_index) : NONE;
					monster->sound_polygon_index= object->polygon;
					monster->sound_location= object->location;
					monster->sound_location.z += definition->height - (definition->height >> 1);
					MARK_SLOT_AS_USED(monster);
					
					/* initialize the monster’s object */
					if (definition->flags&_monster_is_invisible) object->transfer_mode= _xfer_invisibility;
					if (definition->flags&_monster_is_subtly_invisible) object->transfer_mode= _xfer_subtle_invisibility;
					if (definition->flags&_monster_is_enlarged) object->flags|= _object_is_enlarged;
					if (definition->flags&_monster_is_tiny) object->flags|= _object_is_tiny;
					SET_OBJECT_SOLIDITY(object, true);
					SET_OBJECT_OWNER(object, _object_is_monster);
					object->permutation= monster_index;
					object->sound_pitch= definition->sound_pitch;

					/* make sure the object frequency stuff keeps track of how many monsters are
						on the map */
					object_was_just_added(_object_is_monster, original_monster_type);
				}
				else
				{
					monster_index= NONE;
				}
				
				break;
			}
		}
		if (monster_index==MAXIMUM_MONSTERS_PER_MAP) monster_index= NONE;
	}

	/* keep track of how many civilians we drop on this level */
	if ((static_world->mission_flags & _mission_rescue_m1) &&
	    monster_index!=NONE && 
	    (definition->_class&_class_human_civilian_m1)) 
	{
		dynamic_world->current_civilian_count+= 1;
	}

	return monster_index;
}

/* assumes ∂t==1 tick */
void move_monsters(
	void)
{
	sprintathon_move_corpses();
    sprintathon_update_body_springs();
	struct monster_data *monster;
	bool monster_got_time= false;
	bool monster_built_path= (dynamic_world->tick_count&3) ? true : false;
	short monster_index;

	for (monster_index= 0, monster= monsters; monster_index<MAXIMUM_MONSTERS_PER_MAP; ++monster_index, ++monster)
	{
		if (SLOT_IS_USED(monster) && !MONSTER_IS_PLAYER(monster))
		{
			struct object_data *object= get_object_data(monster->object_index);
			
            sprintathon_begin_character_contact(monster_index);
            const bool recovering_overlap = sprintathon_recover_ground_overlap(monster_index);
			if (MONSTER_IS_ACTIVE(monster))
			{
				if (!OBJECT_IS_INVISIBLE(object))
				{
					struct monster_definition *definition= get_monster_definition(monster->type);
					short animation_flags;
					
					// AlexJLS patch: effect of dangerous polygons
					cause_polygon_damage(object->polygon,monster_index);
	
					/* clear the recovering from hit flag, mark the monster as not idle */	
					SET_MONSTER_IDLE_STATUS(monster, false);
	
					update_monster_vertical_physics_model(monster_index);
	
					/* update our object’s animation unless we’re ‘suffering’ from an external velocity
						or we’re airborne (if we’re a flying or floating monster, ignore both of these */
					if ((!monster->external_velocity&&!monster->vertical_velocity) ||
						(film_profile.ketchup_fix && (monster->action==_monster_is_attacking_close||monster->action==_monster_is_attacking_far)) ||
						((monster->action!=_monster_is_being_hit||!monster->external_velocity) && (definition->flags&(_monster_floats|_monster_flys))))
					{
						animate_object(monster->object_index);
					}
					animation_flags= GET_OBJECT_ANIMATION_FLAGS(object);
		
					/* give this monster time, if we can and he needs it */
					if (!monster_got_time && monster_index>dynamic_world->last_monster_index_to_get_time && !MONSTER_IS_DYING(monster))
					{
						switch (monster->mode)
						{
							case _monster_unlocked:
								/* if this monster is unlocked and we haven’t already given a monster time,
									call find_closest_appropriate_target() */
								change_monster_target(monster_index, find_closest_appropriate_target(monster_index, false));
								monster_got_time= true;
								break;
							
							case _monster_lost_lock:
							case _monster_losing_lock:
								/* if this monster has lost or is losing lock and we haven’t already given a monster
									time, check to see if his target has become visible again */
								if (clear_line_of_sight(monster_index, monster->target_index, false))
								{
									change_monster_target(monster_index, monster->target_index);
								}
								monster_got_time= true;
								break;
						}
						
						/* if we gave this guy time, make room for the next guy */
						if (monster_got_time) dynamic_world->last_monster_index_to_get_time= monster_index;
					}
		
					/* if this monster needs a path, generate one (unless we’ve already generated a
						path this frame in which case we’ll wait until next frame, UNLESS the monster
						has no path in which case it needs one regardless) */
					if (!recovering_overlap && MONSTER_NEEDS_PATH(monster) && !MONSTER_IS_DYING(monster) && !MONSTER_IS_ATTACKING(monster) &&
						((!monster_built_path && monster_index>dynamic_world->last_monster_index_to_build_path) || monster->path==NONE))
					{
						generate_new_path_for_monster(monster_index);
						if (!monster_built_path)
						{
							monster_built_path= true;
							dynamic_world->last_monster_index_to_build_path= monster_index;
						}
					}
					
					/* it’s possible that we couldn’t get where we wanted to go, or that we arrived there
						and deactivated ourselves; if this happens we don’t want to continue processing
						the monster as if it were active */
					if (MONSTER_IS_ACTIVE(monster))
					{
						/* move the monster; check to see if we can attack; resolve modes ending; etc. */
						switch (monster->action)
						{
							case _monster_is_waiting_to_attack_again:
							case _monster_is_stationary:
							case _monster_is_moving:
								if (!recovering_overlap) handle_moving_or_stationary_monster(monster_index);
								break;
							
							case _monster_is_attacking_close:
							case _monster_is_attacking_far:
                                if (monster->action == _monster_is_attacking_far)
                                    sprintathon_flock_move(monster_index, nullptr);
								if (animation_flags&_obj_keyframe_started) execute_monster_attack(monster_index);
								if (animation_flags&_obj_last_frame_animated)
								{
									if (((monster->attack_repetitions-=1)<0) || !try_monster_attack(monster_index))
									{
										/* after an attack has been initiated successfully we need to return to
											_monster_is_moving action, kill our path and ask for a new one
											(because we’re pointed in the wrong direction now) */
										set_monster_action(monster_index,
											(monster->attack_repetitions<0 && (definition->flags&_monster_waits_with_clear_shot) && MONSTER_IS_LOCKED(monster)) ?
												_monster_is_waiting_to_attack_again : _monster_is_moving);
										monster_needs_path(monster_index, true);
										monster->ticks_since_attack= 0;
									}
								}
								break;
							
							case _monster_is_teleporting_in:
								if (animation_flags&_obj_last_frame_animated)
								{
									monster->action= _monster_is_moving;
									set_monster_action(monster_index, _monster_is_moving);
									change_monster_target(monster_index, find_closest_appropriate_target(monster_index, false));
								}
								break;
							case _monster_is_teleporting_out:
								if (animation_flags&_obj_keyframe_started)
								{
									monster->action= _monster_is_dying_soft; // to prevent aggressors from relocking
									monster_died(monster_index);
									teleport_object_out(monster->object_index);
									remove_map_object(monster->object_index);
									L_Invalidate_Monster(monster_index);
									MARK_SLOT_AS_FREE(monster);
								}
								break;
							
							case _monster_is_being_hit:
								update_monster_physics_model(monster_index);
								if (animation_flags&_obj_last_frame_animated)
								{
									monster_needs_path(monster_index, true);
									set_monster_action(monster_index, _monster_is_moving);
									monster->external_velocity= 0;
								}
								break;
							
							case _monster_is_dying_soft:
							case _monster_is_dying_hard:
							case _monster_is_dying_flaming:
								update_monster_physics_model(monster_index);
								if ((definition->flags&_monster_has_delayed_hard_death) && monster->action==_monster_is_dying_soft)
								{
									if (!monster->external_velocity && object->location.z==monster->desired_height) //&& !monster->vertical_velocity)
									{
										set_monster_action(monster_index, _monster_is_dying_hard);
									}
									else
									{
										if (definition->contrail_effect!=NONE) new_effect(&object->location, object->polygon, definition->contrail_effect, object->facing);
									}
								}
								else
								{
									// LP change: if keyframe is zero, then a monster should not produce shrapnel damage.
									// This fixes a side effect of a fix of the keyframe-never-zero bug,
									// which is that Hunters injure those nearby when they die a soft death.
									if (animation_flags&_obj_keyframe_started && (!film_profile.keyframe_fix || GET_SEQUENCE_FRAME(object->sequence) != 0))
										cause_shrapnel_damage(monster_index);
									if (animation_flags&_obj_last_frame_animated) kill_monster(monster_index);
								}
								break;
							
							default:
								assert(false);
								break;
						}
					}
				}
			}
			else
			{
				/* all inactive monsters get time to scan for targets */
				if (!monster_got_time && !MONSTER_IS_BLIND(monster) && monster_index>dynamic_world->last_monster_index_to_get_time)
				{
					change_monster_target(monster_index, find_closest_appropriate_target(monster_index, false));
					if (MONSTER_HAS_VALID_TARGET(monster)) activate_nearby_monsters(monster->target_index, monster_index, _pass_one_zone_border, MONSTER_ALERT_ACTIVATION_RANGE);
					
					monster_got_time= true;
					dynamic_world->last_monster_index_to_get_time= monster_index;
				}
			}
		}
		
		if (SLOT_IS_USED(monster) && !MONSTER_IS_PLAYER(monster) && MONSTER_IS_ACTIVE(monster))
			sprintathon_float_coast(monster_index);
        sprintathon_end_character_contact(monster_index);

		/* WARNING: a large number of unusual things could have happened here, including the monster
			being dead, his slot being free, and his object having been removed from the map; in other
			words, it’s probably not a good idea to do any postprocessing here */
	}
	
	/* either there are no unlocked monsters or ‘dynamic_world->last_monster_index_to_get_time’ is higher than
		all of them (so we reset it to zero) ... same for paths */
	if (!monster_got_time) dynamic_world->last_monster_index_to_get_time= -1;
	if (!monster_built_path) dynamic_world->last_monster_index_to_build_path= -1;

	if (dynamic_world->civilians_killed_by_players)
	{
		uint32 mask = 0;
		
		switch (dynamic_world->game_information.difficulty_level)
		{
			case _wuss_level: mask= 0x7f; break;
			case _easy_level: mask= 0xff; break;
			case _normal_level: mask= 0x1ff; break;
			case _major_damage_level: mask= 0x3ff; break;
			case _total_carnage_level: mask= 0x7ff; break;
		}
		
		if (!(dynamic_world->tick_count&mask))
		{
			dynamic_world->civilians_killed_by_players-= 1;
		}
	}
}

/* when a monster dies, all monsters locked on it need to find something better to do; this
	function should be called before the given target is expunged from the monster list but
	after it is marked as dying */
void monster_died(
	short target_index)
{
	struct monster_data *monster= get_monster_data(target_index);
	short monster_index;

//	dprintf("monster #%d is dead;g;", target_index);

	/* orphan this monster’s projectiles if they don’t belong to a player (player’s monster
		slots are always valid and we want to correctly attribute damage and kills that ocurr
		after a player dies) */
	if (!MONSTER_IS_PLAYER(monster)) orphan_projectiles(target_index);
	
	/* active monsters need extant paths deleted and should be marked as unlocked */
	if (MONSTER_IS_ACTIVE(monster))
	{
		set_monster_mode(target_index, _monster_unlocked, NONE);
		if (monster->path!=NONE) delete_path(monster->path);
		SET_MONSTER_NEEDS_PATH_STATUS(monster, false);
		monster->path= NONE;
	}

	/* anyone locked on this monster needs a clue */
	for (monster_index= 0, monster= monsters; monster_index<MAXIMUM_MONSTERS_PER_MAP; ++monster_index, ++monster)
	{
		if (SLOT_IS_USED(monster) && MONSTER_IS_ACTIVE(monster) && monster->target_index==target_index)
		{
			short closest_target_index= find_closest_appropriate_target(monster_index, true);

			monster->target_index= NONE;
			monster_needs_path(monster_index, false);
			
			play_object_sound(monster->object_index, get_monster_definition(monster->type)->kill_sound);
			if (closest_target_index!=NONE)
			{
				change_monster_target(monster_index, closest_target_index);
			}
			else
			{
				if (monster->action==_monster_is_waiting_to_attack_again) set_monster_action(monster_index, _monster_is_moving);
				set_monster_mode(monster_index, _monster_unlocked, NONE);
			}
		}
	}
}

void initialize_monsters(
	void)
{
	sprintathon_reset_flock();
	/* initialize our globals to be the same thing on all machines */
	dynamic_world->civilians_killed_by_players= 0;
	dynamic_world->last_monster_index_to_get_time= -1;
	dynamic_world->last_monster_index_to_build_path= -1;
	dynamic_world->new_monster_mangler_cookie= global_random();
	dynamic_world->new_monster_vanishing_cookie= global_random();
}

/* call this when a new level is loaded from disk so the monsters can cope with their new world */
void initialize_monsters_for_new_level(
	void)
{
    sprintathon_reset_flock();
	for (short i = 0; i < MAXIMUM_OBJECTS_PER_MAP; ++i) sprintathon_forget_corpse(i);
	struct monster_data *monster;
	short monster_index;

	/* when a level is loaded after being saved all of an active monster’s data is still intact,
		but it’s path no longer exists.  this function resets all monsters so that they recalculate
		their paths, first thing. */
	for (monster_index=0,monster=monsters;monster_index<MAXIMUM_MONSTERS_PER_MAP;++monster_index,++monster)
	{
		if (SLOT_IS_USED(monster)&&MONSTER_IS_ACTIVE(monster))
		{
			SET_MONSTER_NEEDS_PATH_STATUS(monster, true);
			monster->path= NONE;
		}
	}
}

static void load_sound(short sound_index)
{
	SoundManager::instance()->LoadSound(sound_index);
}

void load_monster_sounds(
	short monster_type)
{
	if (monster_type!=NONE)
	{
		struct monster_definition *definition= get_monster_definition(monster_type);
		
		process_collection_sounds(definition->collection, load_sound);
		
		load_projectile_sounds(definition->ranged_attack.type);
		load_projectile_sounds(definition->melee_attack.type);
		
		SoundManager::instance()->LoadSounds(&definition->activation_sound, 8);
	}
}

void mark_monster_collections(
	short monster_type,
	bool loading)
{
	if (monster_type!=NONE)
	{
		struct monster_definition *definition= get_monster_definition(monster_type);

		/* mark the monster collection */
		mark_collection(definition->collection, loading);
		
		/* mark the monster’s projectile’s collection */
		mark_projectile_collections(definition->ranged_attack.type, loading);
		mark_projectile_collections(definition->melee_attack.type, loading);
	}
}

enum
{
	MAXIMUM_NEED_TARGET_INDEXES= 32
};

void activate_nearby_monsters(
	short target_index, /* activate with lock on this target (or NONE for lock-less activation) */
	short caller_index, /* start the flood from here */
	short flags,
	int32 max_range)
{
	struct monster_data *caller= get_monster_data(caller_index);
    int32 max_cost= INT32_MAX;
    if (static_world->environment_flags&_environment_activation_ranges)
    {
		if (max_range>0)
			max_cost= max_range*max_range;
		else if (flags&_activate_glue_monsters)
			max_cost= GLUE_TRIGGER_ACTIVATION_RANGE*GLUE_TRIGGER_ACTIVATION_RANGE;
    }

	if (dynamic_world->tick_count-caller->ticks_since_last_activation>MINIMUM_ACTIVATION_SEPARATION ||
		(flags&_activation_cannot_be_avoided))
	{
		short polygon_index= get_object_data(caller->object_index)->polygon;
		short need_target_indexes[MAXIMUM_NEED_TARGET_INDEXES];
		short need_target_count= 0;
		int32 flood_flags= flags;
		
		/* flood out from the target monster’s polygon, searching through the object lists of all
			polygons we encounter */
		polygon_index= flood_map(polygon_index, max_cost, monster_activation_flood_proc, _flagged_breadth_first, &flood_flags);
		while (polygon_index!=NONE)
		{
			short object_index;
			struct object_data *object;
	
			/* loop through all objects in this polygon looking for _hostile inactive or unlocked monsters */
			for (object_index= get_polygon_data(polygon_index)->first_object; object_index!=NONE; object_index= object->next_object)
			{
				object= get_object_data(object_index);
				if (GET_OBJECT_OWNER(object)==_object_is_monster &&
					(!OBJECT_IS_INVISIBLE(object) || (flags&_activate_invisible_monsters)))
				{
					short aggressor_index= object->permutation;
					struct monster_data *aggressor= get_monster_data(aggressor_index);
//					bool target_hostile= get_monster_attitude(aggressor_index, target_index)==_hostile;
//					bool caller_hostile= get_monster_attitude(aggressor_index, caller_index)==_hostile;

// deaf monsters are only deaf to players which have always been hostile, so:
//   bobs are deaf to friendly players but not hostile ones
//   monsters are deaf to all players
// deaf monsters ignore friendly monsters activating on other friendly monsters but
//   non-deaf ones DO NOT

//					!MONSTER_IS_PLAYER(caller) || TYPE_IS_FRIEND(get_monster_definition(aggressor->type), caller->type) || caller_hostile
					
					/* don’t activate players or ourselves, and only activate monsters on glue polygons
						if they have previously been activated or we’ve been explicitly told to */
					if (!MONSTER_IS_PLAYER(aggressor) && caller_index!=aggressor_index && target_index!=aggressor_index &&
						(!(flood_flags&_passed_zone_border) || (!(aggressor->flags&_monster_has_never_been_activated))) &&
						((flood_flags&_activate_deaf_monsters) || !MONSTER_IS_DEAF(aggressor)) && // || !MONSTER_IS_PLAYER(caller) || !TYPE_IS_FRIEND(get_monster_definition(aggressor->type), caller->type) || !caller_hostile) &&
						aggressor->mode!=_monster_locked)
					{
						bool monster_was_active= true;
						
						/* activate the monster if he’s inactive */
						if (!MONSTER_IS_ACTIVE(aggressor))
						{
							activate_monster(aggressor_index);
							monster_was_active= false;
						}
						
						if (monster_was_active || !(flags&_use_activation_biases) ||
							(aggressor->activation_bias!=_activate_on_goal && aggressor->activation_bias!=_activate_randomly))
						{
							if (monster_was_active || aggressor->activation_bias!=_activate_on_nearest_hostile)
							{
								/* if we have valid target and this monster thinks that target is hostile, lock on */
								if (get_monster_attitude(aggressor_index, target_index)==_hostile)
								{
									switch_target_check(aggressor_index, target_index, 0);
								}
								else
								{
									/* but hey, if the target isn’t hostile, maybe the caller is ...
										(mostly for the automated defenses and the civilians on the ship) */
									if (get_monster_attitude(aggressor_index, caller_index)==_hostile)
									{
										switch_target_check(aggressor_index, caller_index, 0);
									}
								}
							}
							else
							{
								// must defer find_closest_appropriate_target; pathfinding is not reentrant
								if (need_target_count<MAXIMUM_NEED_TARGET_INDEXES)
								{
									need_target_indexes[need_target_count++]= aggressor_index;
								}
							}
						}
					}
				}
			}
			
			polygon_index= flood_map(NONE, max_cost, monster_activation_flood_proc, _flagged_breadth_first, &flood_flags);
		}

		// deferred find_closest_appropriate_target() calls
		while (--need_target_count>=0)
		{
			change_monster_target(need_target_indexes[need_target_count],
				find_closest_appropriate_target(need_target_indexes[need_target_count], true));
		}

		caller->ticks_since_last_activation= dynamic_world->tick_count;
	}
}

static int32 monster_activation_flood_proc(
	short source_polygon_index,
	short line_index,
	short destination_polygon_index,
	void *data)
{
	int32 *flags=(int32 *)data;
	struct polygon_data *destination_polygon= get_polygon_data(destination_polygon_index);
	struct polygon_data *source_polygon= get_polygon_data(source_polygon_index);
	struct line_data *line= get_line_data(line_index);
	bool obey_glue= (static_world->environment_flags&_environment_glue_m1);
	bool limit_activation= (static_world->environment_flags&_environment_activation_ranges);
	int32 cost= limit_activation ? source_polygon->area : 1;

//	dprintf("P#%d==>P#%d by L#%d", source_polygon_index, destination_polygon_index, line_index);

	if (destination_polygon->type==_polygon_is_zone_border)
	{
		if (((*flags)&_pass_one_zone_border) && !((*flags)&_passed_zone_border))
		{
			*flags|= _passed_zone_border;
		}
		else
		{
			// can’t pass this zone border
			cost= -1;
		}
	}
	else if ((destination_polygon->type==_polygon_is_superglue) &&
	         ((*flags)&_cannot_pass_superglue) &&
	         obey_glue)
	{
		cost= -1;
	}
	else if ((destination_polygon->type==_polygon_is_glue) &&
	         !((*flags)&_activate_glue_monsters) &&
	         obey_glue)
	{
		cost= -1;
	}
	else if ((destination_polygon->type==_polygon_is_monster_impassable) &&
	         limit_activation)
	{
		cost= -1;
	}
	else if ((destination_polygon->type==_polygon_is_platform) &&
	         (destination_polygon->floor_height==destination_polygon->ceiling_height) &&
	         !((*flags)&_pass_solid_lines) &&
	         limit_activation)
	{
		cost= -1;
	}

	if (!((*flags)&_pass_solid_lines) && LINE_IS_SOLID(line)) cost= -1;

	if (cost>0 && limit_activation)
	{
		world_distance delta_height= destination_polygon->floor_height-source_polygon->floor_height;
		cost+= delta_height*delta_height;
	}
	
	return cost;
}

#define LIVE_ALIEN_THRESHHOLD 8

static std::vector<bool> monster_must_be_exterminated(NUMBER_OF_MONSTER_TYPES, false);

bool live_aliens_on_map(
	void)
{
	bool found_alien_which_must_be_killed= false;
	struct monster_data *monster;
	short live_alien_count= 0;
	short threshhold= LIVE_ALIEN_THRESHHOLD;
	short monster_index;
	
	for (monster_index= 0, monster= monsters; monster_index<MAXIMUM_MONSTERS_PER_MAP; ++monster_index, ++monster)
	{
		if (SLOT_IS_USED(monster))
		{
			struct monster_definition *definition= get_monster_definition(monster->type);

			if (monster_must_be_exterminated[monster->type])
			{
				found_alien_which_must_be_killed = true;
				break;
			}
			
			if ((definition->flags&_monster_is_alien) ||
				((static_world->environment_flags&_environment_rebellion) && !MONSTER_IS_PLAYER(monster)))
			{
				live_alien_count+= 1;
			}
		}
	}
	
	if (static_world->environment_flags&_environment_rebellion) threshhold= 0;
	
	return live_alien_count<=threshhold ? found_alien_which_must_be_killed : true;
}

/* activate the given monster (initially unlocked) */
void activate_monster(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	struct monster_definition *definition= get_monster_definition(monster->type);

//	dprintf("monster #%d activated;g;", monster_index);

	assert(!MONSTER_IS_ACTIVE(monster));
	assert(!MONSTER_IS_PLAYER(monster));

	if (OBJECT_IS_INVISIBLE(object))
	{
		struct polygon_data *polygon= get_polygon_data(object->polygon);
		
		if (polygon->media_index!=NONE)
		{
			struct media_data *media= get_media_data(polygon->media_index);
			
			// LP change: idiot-proofed this
			if (media)
			{
				if (media->height>object->location.z+definition->height &&
					!(definition->flags&_monster_can_teleport_under_media))
				{
					return;
				}
			}
		}
	}
	
	CLEAR_MONSTER_RECOVERING_FROM_HIT(monster);
	SET_MONSTER_IDLE_STATUS(monster, false);
	SET_MONSTER_ACTIVE_STATUS(monster, true);
	SET_MONSTER_BERSERK_STATUS(monster, false);
	SET_MONSTER_HAS_BEEN_ACTIVATED(monster);
	monster->flags&= ~(_monster_is_blind|_monster_is_deaf);

	monster->path= NONE;
	/* we used to set monster->target_index here, but it is invalid when mode==_monster_unlocked */
	monster->mode= _monster_unlocked, monster->target_index= NONE;

	if (definition->attack_frequency == 0) // IP: Avoid division by zero
		definition->attack_frequency = 1;	 

	monster->ticks_since_attack= (definition->flags&_monster_attacks_immediately) ?
		definition->attack_frequency : global_random()%definition->attack_frequency;
	monster->desired_height= object->location.z; /* best guess */
	monster->random_desired_height= INT16_MAX; // to be out of range and recalculated
	monster->external_velocity= monster->vertical_velocity= 0;	
	monster->ticks_since_last_activation= 0;
	
	/* if vitality is NONE (-1) initialize it from the monster_definition, respecting
		the difficulty level if necessary */
	if (monster->vitality==NONE)
	{
		short vitality= definition->vitality;
		
		if (definition->flags&_monster_is_alien)
		{
			switch (dynamic_world->game_information.difficulty_level)
			{
				case _wuss_level: vitality-= vitality>>1; break;
				case _easy_level: vitality-= vitality>>2; break;
				case _major_damage_level: vitality+= vitality>>2; break;
				case _total_carnage_level: vitality+= vitality>>1; break;
			}
		}
		
		monster->vitality= vitality;
	}

	set_monster_action(monster_index, _monster_is_stationary);
	monster_needs_path(monster_index, true);

	if (OBJECT_IS_INVISIBLE(object))
	{
		teleport_object_in(monster->object_index);
	}
	else if (definition->flags & _monster_makes_sound_when_activated)
	{
		play_object_sound(monster->object_index, definition->activation_sound);
	}
	
	changed_polygon(object->polygon, object->polygon, NONE);
}

void deactivate_monster(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);

//	dprintf("monster #%d deactivated;g;", monster_index);

	assert(MONSTER_IS_ACTIVE(monster));

	if (MONSTER_TELEPORTS_OUT_WHEN_DEACTIVATED(monster)) monster->vertical_velocity= monster->external_velocity= 0;

	if (!monster->vertical_velocity && !monster->external_velocity)
	{
		if (MONSTER_TELEPORTS_OUT_WHEN_DEACTIVATED(monster) && monster->action!=_monster_is_teleporting_out)
		{
			set_monster_action(monster_index, _monster_is_teleporting_out);
		}
		else
		{
			/* assume stationary shape before deactivation */
			set_monster_action(monster_index, _monster_is_stationary);
			
			/* get rid of this monster’s path if he has one */
			if (monster->path!=NONE) delete_path(monster->path);
			
			SET_MONSTER_ACTIVE_STATUS(monster, false);
		}
	}
}

/* returns a list of object indexes of all monsters in or adjacent to the given polygon,
	up to maximum_object_count. */
// LP change: called with growable list
// Negative garbage permutations retain the original monster type across saves.
// Ordinary garbage keeps its existing nonnegative permutation.
struct SprintathonCorpseVelocity { int x = 0, y = 0, z = 0; };
static std::map<short, SprintathonCorpseVelocity> sprintathon_corpse_velocity;

void sprintathon_forget_corpse(short object_index)
{
    sprintathon_corpse_velocity.erase(object_index);
}

void sprintathon_register_corpse(short object_index, short monster_type)
{
    sprintathon_forget_corpse(object_index);
    if (!game_is_networked && monster_type >= 0 && monster_type < NUMBER_OF_MONSTER_TYPES)
        get_object_data(object_index)->permutation = -monster_type - 2;
}

bool sprintathon_is_physics_corpse(short object_index)
{
    if (game_is_networked || !input_preferences->sprintathon_enabled ||
        !input_preferences->sprintathon_physics_corpses) return false;
    object_data *object = get_object_data(object_index);
    const int type = -int(object->permutation) - 2;
    return SLOT_IS_USED(object) && GET_OBJECT_OWNER(object) == _object_is_garbage &&
        type >= 0 && type < NUMBER_OF_MONSTER_TYPES && !OBJECT_IS_INVISIBLE(object);
}

void sprintathon_corpse_dimensions(short object_index, world_distance *radius, world_distance *height)
{
    monster_definition *definition = get_monster_definition(-get_object_data(object_index)->permutation - 2);
    *radius = definition->radius;
    *height = std::max<int>(WORLD_ONE / 8, std::min<int>(WORLD_ONE / 3, definition->height / 4));
}

short sprintathon_corpse_impact_effect(short object_index, bool melee)
{
    monster_definition *definition = get_monster_definition(-get_object_data(object_index)->permutation - 2);
    return melee ? definition->melee_impact_effect : definition->impact_effect;
}

void sprintathon_hit_corpse(short object_index, const world_point3d& origin, world_distance impulse)
{
    if (!sprintathon_is_physics_corpse(object_index)) return;
    object_data *object = get_object_data(object_index);
    const double dx = double(object->location.x) - origin.x;
    const double dy = double(object->location.y) - origin.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    SprintathonCorpseVelocity& velocity = sprintathon_corpse_velocity[object_index];
    if (length > 0.0) {
        velocity.x += int(dx * impulse / length);
        velocity.y += int(dy * impulse / length);
    }
    velocity.z += impulse / 2;
    velocity.x = std::max(-WORLD_ONE / 3, std::min(WORLD_ONE / 3, velocity.x));
    velocity.y = std::max(-WORLD_ONE / 3, std::min(WORLD_ONE / 3, velocity.y));
    velocity.z = std::min(WORLD_ONE / 3, velocity.z);
}

void sprintathon_move_corpses()
{
    if (game_is_networked || !input_preferences->sprintathon_enabled ||
        !input_preferences->sprintathon_physics_corpses) {
        sprintathon_corpse_velocity.clear();
        return;
    }
    for (short index = 0; index < MAXIMUM_OBJECTS_PER_MAP; ++index) {
        if (SLOT_IS_FREE(objects + index) || !sprintathon_is_physics_corpse(index)) continue;
        object_data *object = get_object_data(index);
        const polygon_data *polygon = get_polygon_data(object->polygon);
        auto found = sprintathon_corpse_velocity.find(index);
        if (found == sprintathon_corpse_velocity.end() && object->location.z == polygon->floor_height) continue;
        SprintathonCorpseVelocity& velocity = sprintathon_corpse_velocity[index];
        velocity.z -= WORLD_ONE / 128;
        world_point3d next = object->location;
        next.x = std::max<int>(SHRT_MIN, std::min<int>(SHRT_MAX, int(next.x) + velocity.x));
        next.y = std::max<int>(SHRT_MIN, std::min<int>(SHRT_MAX, int(next.y) + velocity.y));
        world_distance radius, height, floor, ceiling;
        short supporting_polygon;
        sprintathon_corpse_dimensions(index, &radius, &height);
        keep_line_segment_out_of_walls(object->polygon, &object->location, &next,
            0, height, &floor, &ceiling, &supporting_polygon);
        const int top = std::max<int>(floor, int(ceiling) - height);
        const int proposed_z = int(object->location.z) + velocity.z;
        next.z = std::max<int>(floor, std::min<int>(top, proposed_z));
        if (proposed_z <= floor || proposed_z >= top) velocity.z = 0;
        translate_map_object(index, &next, NONE);
        const bool grounded = next.z <= floor;
        velocity.x = velocity.x * (grounded ? 3 : 31) / (grounded ? 4 : 32);
        velocity.y = velocity.y * (grounded ? 3 : 31) / (grounded ? 4 : 32);
        if (grounded && !velocity.x && !velocity.y && !velocity.z)
            sprintathon_corpse_velocity.erase(index);
    }
}

bool possible_intersecting_monsters(
	vector<short> *IntersectedObjectsPtr,
	unsigned maximum_object_count,
	short polygon_index,
	bool include_scenery,
	bool include_corpses)
{
	struct polygon_data *polygon= get_polygon_data(polygon_index);
	short *neighbor_indexes= get_map_indexes(polygon->first_neighbor_index, polygon->neighbor_count);
	bool found_solid_object= false;

	// Skip this step if neighbor indexes were not found
	if (!neighbor_indexes) return found_solid_object;

	for (short i=0;i<polygon->neighbor_count;++i)
	{
		struct polygon_data *neighboring_polygon= get_polygon_data(*neighbor_indexes++);
		
		if (!POLYGON_IS_DETACHED(neighboring_polygon))
		{
			short object_index= neighboring_polygon->first_object;
			
			while (object_index!=NONE)
			{
				struct object_data *object= get_object_data(object_index);
				bool solid_object= false;
				
				if (!OBJECT_IS_INVISIBLE(object))
				{
					switch (GET_OBJECT_OWNER(object))
					{
						case _object_is_monster:
						{
							struct monster_data *monster= get_monster_data(object->permutation);
						
							if (!MONSTER_IS_DYING(monster) && !MONSTER_IS_TELEPORTING(monster))
							{
								solid_object= true;
							}
							
							break;
						}
						
						case _object_is_garbage:
							if (include_corpses && sprintathon_is_physics_corpse(object_index)) solid_object = true;
							break;
						case _object_is_scenery:
							if (include_scenery && OBJECT_IS_SOLID(object)) solid_object= true;
							break;
					}
					
					if (solid_object)
					{
						found_solid_object= true;
						
						// LP change:
						if (IntersectedObjectsPtr && IntersectedObjectsPtr->size()<maximum_object_count) /* do we have enough space to add it? */
						{
							unsigned j;
							
							/* only add this object_index if it's not already in the list */
							vector<short>& IntersectedObjects = *IntersectedObjectsPtr;
							for (j=0; j<IntersectedObjects.size() && IntersectedObjects[j]!=object_index; ++j)
								;
							if (j==IntersectedObjects.size())
								IntersectedObjects.push_back(object_index);
						}
					}
				}
				
				object_index= object->next_object;
			}
		}
	}

	return found_solid_object;
}

/* when a target changes polygons, all monsters locked on it must recalculate their paths.
	target is an index into the monster list. */
void monster_moved(
	short target_index,
	short old_polygon_index)
{
	struct monster_data *monster= get_monster_data(target_index);
	struct object_data *object= get_object_data(monster->object_index);
	short monster_index;
	
	if (!MONSTER_IS_PLAYER(monster))
	{
		/* cause lights to light, platforms to trigger, etc.; the player does this differently */
		changed_polygon(old_polygon_index, object->polygon, NONE);
	}
	else if ((static_world->environment_flags & _environment_glue_m1) &&
	         (get_polygon_data(object->polygon)->type == _polygon_is_glue_trigger))
	{
		activate_nearby_monsters(target_index, target_index,
			_pass_solid_lines|_activate_deaf_monsters|_activate_invisible_monsters|_use_activation_biases|_cannot_pass_superglue|_activate_glue_monsters);
	}

	for (monster_index=0,monster=monsters;monster_index<MAXIMUM_MONSTERS_PER_MAP;++monster_index,++monster)
	{
		/* look for active monsters locked (or losing lock) on the given target_index */
		if (SLOT_IS_USED(monster) && MONSTER_HAS_VALID_TARGET(monster) && monster->target_index==target_index)
		{
			if (clear_line_of_sight(monster_index, target_index, true))
			{
				if (monster->mode==_monster_losing_lock) set_monster_mode(monster_index, _monster_locked, monster->target_index);
			}
			else
			{
				struct monster_definition *definition= get_monster_definition(monster->type);
				
				/* we can’t see our target: if this is first time, change from _monster_locked
					to _monster_losing_lock, if this isn’t the first time and our target has
					switched polygons more times out of our sight than we have intelligence points,
					go to _lost_lock (which means we won’t get any more new paths when our target
					switches polygons, but we won’t clear our last one until we reach the end). */
				if (monster->mode==_monster_locked) monster->changes_until_lock_lost= 0;
				if (monster->mode==_monster_losing_lock) monster->changes_until_lock_lost+= 1;
				set_monster_mode(monster_index, (monster->changes_until_lock_lost>=definition->intelligence) ?
					_monster_lost_lock : _monster_losing_lock, NONE);
			}
			
			/* if we’re losing lock, don’t recalculate our path (we’re headed towards the target’s
				last-known location) */
			if (monster->mode!=_monster_losing_lock) monster_needs_path(monster_index, false);
		}
	}
}

/* returns NONE or a monster_index that prevented us from moving */
short legal_player_move(
	short monster_index,
	world_point3d *new_location,
	world_distance *object_floor) /* must be set on entry */
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	world_point3d *old_location= &object->location;
	size_t monster_count;
	world_distance radius, height;
	short obstacle_index= NONE;

	get_monster_dimensions(monster_index, &radius, &height);	
	std::vector<short> IntersectedObjects;
	possible_intersecting_monsters(&IntersectedObjects, LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE, object->polygon, true);
	monster_count = IntersectedObjects.size();
	for (size_t i=0;i<monster_count;++i)
	{
		struct object_data *obstacle= get_object_data(IntersectedObjects[i]);
		world_distance obstacle_radius, obstacle_height;
		
		switch (GET_OBJECT_OWNER(obstacle))
		{
			case _object_is_monster: get_monster_dimensions(obstacle->permutation, &obstacle_radius, &obstacle_height); break;
			case _object_is_scenery: get_scenery_dimensions(obstacle->permutation, &obstacle_radius, &obstacle_height); break;
			default:
				assert(false);
				break;
		}
		
		if (IntersectedObjects[i]!=monster->object_index) /* no self-intersection */
		{
			world_point3d *obstacle_location= &obstacle->location;

			world_distance separation= radius+obstacle_radius;
            if (GET_OBJECT_OWNER(obstacle)==_object_is_monster &&
                sprintathon_spring_pair(monster_index,obstacle->permutation))
                separation=sprintathon_spring_separation(separation);
			int32 separation_squared= separation*separation;

			world_distance new_dx= obstacle_location->x-new_location->x;
			world_distance new_dy= obstacle_location->y-new_location->y;
			int32 new_distance_squared= new_dx*new_dx+new_dy*new_dy;
			
			if (new_distance_squared<separation_squared)
			{
				world_distance old_dx= obstacle_location->x-old_location->x;
				world_distance old_dy= obstacle_location->y-old_location->y;
				int32 old_distance_squared= old_dx*old_dx+old_dy*old_dy;

				if (old_distance_squared>new_distance_squared)
				{
					world_distance this_object_floor= obstacle_location->z+obstacle_height;
					
					/* it’s possible we don’t intersect in z */
					if (new_location->z+height<obstacle_location->z) continue; 
					if (new_location->z>this_object_floor)
					{
						if (this_object_floor>*object_floor) *object_floor= this_object_floor;
						continue;
					}
					
//					dprintf("#%d (%d,%d) hit #%d (%d,%d) moving to (%d,%d)", monster_index, old_location->x, old_location->y, obstacle->permutation, obstacle_location->x, obstacle_location->y, new_location->x, new_location->y);
					obstacle_index= IntersectedObjects[i];
					break;
				}
			}
		}
	}
	
	return obstacle_index;
}

/* returns NONE or a monster_index that prevented us from moving */
short legal_monster_move(
	short monster_index,
	angle facing, /* could be different than object->facing for players and ‘flying’ (heh heh) monsters */
	world_point3d *new_location)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
//	world_point2d *old_location= (world_point2d *) &object->location;
    // Enhanced ground movement uses the same full-radius cylinders as local
    // steering, regardless of facing or monster slot order. Legacy play keeps
    // its original collision rules (including network/replay compatibility).
    if (sprintathon_ground_ai(monster, get_monster_definition(monster->type))) {
        world_distance object_floor = INT16_MIN;
        return legal_player_move(monster_index, new_location, &object_floor);
    }

	size_t monster_count;
	world_distance radius, height;
	short obstacle_index= NONE;

	get_monster_dimensions(monster_index, &radius, &height);	
	
	std::vector<short> IntersectedObjects;
	possible_intersecting_monsters(&IntersectedObjects, LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE, object->polygon, true);
	monster_count= IntersectedObjects.size();
	for (size_t i=0;i<monster_count;++i)
	{
		struct object_data *obstacle= get_object_data(IntersectedObjects[i]);
		world_distance obstacle_radius, obstacle_height;
			
		switch (GET_OBJECT_OWNER(obstacle))
		{
			case _object_is_monster: get_monster_dimensions(obstacle->permutation, &obstacle_radius, &obstacle_height); break;
			case _object_is_scenery: get_scenery_dimensions(obstacle->permutation, &obstacle_radius, &obstacle_height); break;
			default:
				assert(false);
				break;
		}
			
		// LP change:
		if (IntersectedObjects[i]!=monster->object_index) /* no self-intersection */
		{
			world_point3d *obstacle_location= &obstacle->location;
			
			if (obstacle_location->z<new_location->z+height && obstacle_location->z+obstacle_height>new_location->z)
			{
				world_distance separation= radius+obstacle_radius;
				world_distance dx= obstacle_location->x-new_location->x;
				world_distance dy= obstacle_location->y-new_location->y;
				
                if (GET_OBJECT_OWNER(obstacle)==_object_is_monster &&
                    sprintathon_spring_pair(monster_index,obstacle->permutation))
                    separation=sprintathon_spring_separation(separation);
                else if (GET_OBJECT_OWNER(obstacle)!=_object_is_scenery && obstacle->permutation>monster_index && !MONSTER_IS_PLAYER(get_monster_data(obstacle->permutation))) separation= (separation>>1) + (separation>>2);
				if (dx>-separation && dx<separation && dy>-separation && dy<separation)
				{
					/* we intersect sloppily; get arctan to be sure */
					angle theta= NORMALIZE_ANGLE(arctangent(dx, dy)-facing);
					
					if (theta<EIGHTH_CIRCLE||theta>FULL_CIRCLE-EIGHTH_CIRCLE)
					{
//						dprintf("#%d (%d,%d) hit #%d (%d,%d) moving to (%d,%d)", monster_index, old_location->x, old_location->y, obstacle->permutation, obstacle_location->x, obstacle_location->y, new_location->x, new_location->y);
						obstacle_index= IntersectedObjects[i];
						break;
					}
				}
			}
		}
	}
	
	return obstacle_index;
}

void get_monster_dimensions(
	short monster_index,
	world_distance *radius,
	world_distance *height)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);

	*radius= definition->radius;
	*height= definition->height;
}

void damage_monsters_in_radius(
	short primary_target_index,
	short aggressor_index,
	short aggressor_type,
	world_point3d *epicenter,
	short epicenter_polygon_index,
	world_distance radius,
	struct damage_definition *damage,
	short projectile_index)
{
	if (input_preferences->sprintathon_physics_corpses && !game_is_networked && radius > 0) {
        for (short i = 0; i < MAXIMUM_OBJECTS_PER_MAP; ++i) {
            if (SLOT_IS_FREE(objects + i) || !sprintathon_is_physics_corpse(i)) continue;
            object_data *corpse = get_object_data(i);
            const double dx = double(corpse->location.x) - epicenter->x;
            const double dy = double(corpse->location.y) - epicenter->y;
            const double dz = double(corpse->location.z) - epicenter->z;
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance < radius && !line_is_obstructed(epicenter_polygon_index, epicenter,
                corpse->polygon, &corpse->location))
                sprintathon_hit_corpse(i, *epicenter, world_distance((WORLD_ONE / 3) * (1.0 - distance / radius)));
        }
    }
	size_t object_count;

	bool aggressor_is_live_player = false;

	(void) (primary_target_index);
	
	std::vector<short> IntersectedObjects;
	possible_intersecting_monsters(&IntersectedObjects, LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE, epicenter_polygon_index, false);
	object_count= IntersectedObjects.size();
        struct object_data *aggressor = NULL;
	if (film_profile.infinity_tag_fix && aggressor_index != NONE)
	{
		monster_data* monster = get_monster_data(aggressor_index);
		if (MONSTER_IS_PLAYER(monster))
		{
			player_data* player = get_player_data(monster_index_to_player_index(aggressor_index));
			
			if (!PLAYER_IS_DEAD(player)) aggressor_is_live_player = true;
		}
	}

	for (size_t i=0;i<object_count;++i)
	{
		struct object_data *object= get_object_data(IntersectedObjects[i]);
                if (film_profile.damage_aggressor_last_in_tag && 
		    GET_GAME_TYPE() == _game_of_tag && 
		    object->permutation == aggressor_index) {
                        // damage the aggressor last, so tag suicides are handled correctly
                        aggressor = object;
                } else {
                        world_distance distance= distance2d(epicenter, &object->location);
                        world_distance monster_radius, monster_height;
                        
                        get_monster_dimensions(object->permutation, &monster_radius, &monster_height);
        
                        /* make sure we intersect the monster’s radius in the x,y-plane and that we intersect
                                his cylinder in z */
                        if (distance<radius+monster_radius)
                        {
                                if (epicenter->z+radius-distance>object->location.z && epicenter->z-radius+distance<object->location.z+monster_height)
                                {
                                        if (!line_is_obstructed(epicenter_polygon_index, epicenter, object->polygon, &object->location))
                                        {
                                                damage_monster(object->permutation, aggressor_index, aggressor_type, epicenter, damage, projectile_index);
                                        }
                                }
                        }
                }
	}

        // damage the aggressor
        if (film_profile.damage_aggressor_last_in_tag && aggressor != NULL) 
	{
                world_distance distance= distance2d(epicenter, &aggressor->location);
                world_distance monster_radius, monster_height;
                
                get_monster_dimensions(aggressor->permutation, &monster_radius, &monster_height);
                if (distance<radius+monster_radius)
                {
                        if (epicenter->z+radius-distance>aggressor->location.z && epicenter->z-radius+distance<aggressor->location.z+monster_height)
                        {
                                if (!line_is_obstructed(epicenter_polygon_index, epicenter, aggressor->polygon, &aggressor->location))
                                {
                                        damage_monster(aggressor->permutation, aggressor_index, aggressor_type, epicenter, damage, projectile_index);
				}
			}
		}
	}

	// or, just make him it
	if (GET_GAME_TYPE() == _game_of_tag && aggressor_is_live_player)
	{
		monster_data* monster = get_monster_data(aggressor_index);
		if (MONSTER_IS_PLAYER(monster))
		{
			short player_index = monster_index_to_player_index(aggressor_index);
			player_data* player = get_player_data(player_index);
			
			// he blew himself up, so make sure he's it
			if (PLAYER_IS_DEAD(player))
			{
				dynamic_world->game_player_index = player_index;
			}
		}
	}
}

void damage_monster(
	short target_index,
	short aggressor_index,
	short aggressor_type,
	world_point3d *epicenter,
	struct damage_definition *damage,
	short projectile_index)
{
	struct monster_data *monster= get_monster_data(target_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	struct monster_data *aggressor_monster= aggressor_index!=NONE ? get_monster_data(aggressor_index) : (struct monster_data *) NULL;
//	dprintf("%d base, %d random, %d scale.\n", damage->base, damage->random, damage->scale);
	short delta_vitality= calculate_damage(damage);
//	dprintf("we are doing %d damage\n", delta_vitality);
	world_distance external_velocity= 0;
	bool vertical_component= false;

	const bool universal_sprintathon_damage =
		(damage->flags&_sprintathon_damage) != 0;
	if (universal_sprintathon_damage ||
		!(definition->immunities&FLAG(damage->type)))
	{
		// double damage for weaknesses
		if (!universal_sprintathon_damage &&
			(definition->weaknesses&FLAG(damage->type))) delta_vitality<<= 1;
		
		// if this player was shot by a friendly, make him apologise
		if (aggressor_index!=NONE && get_monster_attitude(aggressor_index, target_index)==_friendly)
		{
			play_object_sound(aggressor_monster->object_index, get_monster_definition(aggressor_monster->type)->apology_sound);
		}
		
		if (MONSTER_IS_PLAYER(monster))
		{
			damage_player(target_index, aggressor_index, aggressor_type, damage, projectile_index);
		}
		else
		{
			struct player_data *aggressor_player= (struct player_data *) NULL;
			
			/* only active monsters can take damage */
			if (!MONSTER_IS_ACTIVE(monster)) activate_monster(target_index);
			
			/* convert aggressor monster index to a player index, if possible, to record damage */
			if (aggressor_index!=NONE)
			{
				if (MONSTER_IS_PLAYER(aggressor_monster))
				{
					aggressor_player= get_player_data(monster_index_to_player_index(aggressor_index));
					aggressor_player->monster_damage_given.damage+= MAX(monster->vitality, delta_vitality);
					team_monster_damage_given[aggressor_player->team].damage += MAX(monster->vitality, delta_vitality);
				}
			}

			// LP change: pegging to maximum value
			monster->vitality = MIN(int32(monster->vitality) - int32(delta_vitality), int32(INT16_MAX));
			L_Call_Monster_Damaged(target_index, aggressor_index, damage->type,  delta_vitality, projectile_index);
			if (!SLOT_IS_USED(monster))
				return; // Lua can delete monsters
			
			if (monster->vitality > 0)
			{
				set_monster_action(target_index, _monster_is_being_hit);
				if ((definition->flags&_monster_is_berserker) && monster->vitality<(definition->vitality>>2))
					SET_MONSTER_BERSERK_STATUS(monster, true);
				if (aggressor_index!=NONE) switch_target_check(target_index, aggressor_index, delta_vitality);
				
				// if a player shoots a monster who thinks the player is friendly; ask him what the fuck is up
				if (aggressor_player && get_monster_attitude(target_index, aggressor_index)==_friendly) play_object_sound(monster->object_index, definition->friendly_fire_sound);
			}
			else
			{
				auto is_dying = MONSTER_IS_DYING(monster);
				if (!is_dying)
				{
					short action;
					
					if ((damage_kick_definitions[damage->type].death_action == _monster_is_dying_flaming) && (definition->flags&_monster_can_die_in_flames))
 					{
						action= _monster_is_dying_flaming;
					}
					else
					{
						if ((damage_kick_definitions[damage->type].death_action == _monster_is_dying_hard || ((FLAG(damage->type)&definition->weaknesses) && !(definition->flags & _monster_weaknesses_cause_soft_death)) ||
							definition->soft_dying_shape==UNONE) && definition->hard_dying_shape!=UNONE && !(definition->flags&_monster_has_delayed_hard_death))
						{
							action= _monster_is_dying_hard;
						}
						else
						{
							action= _monster_is_dying_soft;
						}
						if (definition->flags&_monster_has_delayed_hard_death) monster->vertical_velocity= -1;
					}
					
					if (action==_monster_is_dying_flaming || (damage->type == _damage_crushing && (definition->flags & _monster_screams_when_crushed))) play_object_sound(monster->object_index, definition->flaming_sound);
					set_monster_action(target_index, action);
					monster_died(target_index); /* orphan projectile, recalculate aggressor paths */
					
					if (aggressor_player)
					{
						aggressor_player->monster_damage_given.kills+= 1;
                        if (aggressor_player == local_player &&
                            (damage->flags & _sprintathon_kick_damage))
                            sprintathon_record_kick_kill();
						team_monster_damage_given[aggressor_player->team].kills += 1;
						
						if (definition->_class&_class_human_civilian) dynamic_world->civilians_killed_by_players+= 1;
					}

					if ((static_world->mission_flags & _mission_rescue_m1) && (definition->_class & _class_human_civilian_m1))
					{
						dynamic_world->current_civilian_causalties += 1;
					}
				}
				
				if (!is_dying || !film_profile.lua_monster_killed_trigger_fix)
				{
					// Lua script hook
					int aggressor_player_index = -1;
					if (aggressor_index!=NONE)
						if (MONSTER_IS_PLAYER(aggressor_monster))
							aggressor_player_index = monster_index_to_player_index(aggressor_index);
					L_Call_Monster_Killed (target_index, aggressor_player_index, projectile_index);
					if (!SLOT_IS_USED(monster)) // Lua can delete monsters
						return;
				}
			}
		}
		

		if (damage->type >= 0 && damage->type < NUMBER_OF_DAMAGE_TYPES)
		{
			damage_kick_definition& kick_def = damage_kick_definitions[damage->type];
			
			external_velocity = (world_distance)(kick_def.base_value + kick_def.delta_vitality_multiplier*delta_vitality);
			vertical_component = kick_def.is_also_vertical;
			if (film_profile.use_vertical_kick_threshold
			    && kick_def.vertical_threshold
			    && delta_vitality > kick_def.vertical_threshold)
			{
				vertical_component = true;
			}
		}

/*		
		switch (damage->type)
		{
			case _damage_teleporter:
				external_velocity= 250;
				break;

			case _damage_fusion_bolt:
				if (delta_vitality>100) vertical_component= true;
				break;
			
			case _damage_electrical_staff:
			case _damage_yeti_claws:
			case _damage_compiler_bolt:
				vertical_component= true;
				external_velocity= 3*delta_vitality;
				break;
	
			case _damage_shotgun_projectile:
				vertical_component= true;
				break;

			case _damage_explosion:
				vertical_component= true;
				external_velocity= delta_vitality;
				break;
			
			default:
				external_velocity= delta_vitality;
				break;
		}
*/
	
		if (MONSTER_IS_DYING(monster) && external_velocity<MINIMUM_DYING_EXTERNAL_VELOCITY) external_velocity= MINIMUM_DYING_EXTERNAL_VELOCITY;
		external_velocity= (external_velocity*definition->external_velocity_scale)>>FIXED_FRACTIONAL_BITS;
		if (external_velocity && epicenter)
		{
			struct object_data *object= get_object_data(monster->object_index);
			world_distance dx= object->location.x - epicenter->x;
			world_distance dy= object->location.y - epicenter->y;
			world_distance dz= object->location.z + (definition->height>>1) - epicenter->z;
			angle direction= arctangent(dx, dy);
			world_distance radius= isqrt(dx*dx+dy*dy+dz*dz);
			world_distance vertical_velocity= (vertical_component&&radius) ? ((external_velocity*dz)/radius) : 0;
	
			accelerate_monster(target_index, vertical_velocity, direction, external_velocity);
		}
	}
}

bool bump_monster(
	short aggressor_index,
	short monster_index)
{
	return switch_target_check(monster_index, aggressor_index, 0);
}


bool legal_polygon_height_change(
	short polygon_index,
	world_distance new_floor_height,
	world_distance new_ceiling_height,
	struct damage_definition *damage)
{
	world_distance new_polygon_height= new_ceiling_height-new_floor_height;
	struct polygon_data *polygon= get_polygon_data(polygon_index);
	short object_index= polygon->first_object;
	world_distance minimum_height= dead_player_minimum_polygon_height(polygon_index);
	bool legal_change= true;
	
	while (object_index!=NONE)
	{
		struct object_data *object= get_object_data(object_index);
		
		if (GET_OBJECT_OWNER(object)==_object_is_monster && OBJECT_IS_VISIBLE(object))
		{
			world_distance radius, height;
			
			get_monster_dimensions(object->permutation, &radius, &height);
			if (height>=new_polygon_height)
			{
				if (damage)
				{
					damage_monster(object->permutation, NONE, NONE, NULL, damage, NONE);
					play_object_sound(object_index, Sound_Crunched());
				}
				legal_change= false;
			}
		}
		
		object_index= object->next_object;
	}
	
	return new_polygon_height<minimum_height ? false : legal_change;
}

/* we’ve already checked and this monster is not obstructing the polygon from changing heights */
void adjust_monster_for_polygon_height_change(
	short monster_index,
	short polygon_index,
	world_distance new_floor_height,
	world_distance new_ceiling_height)
{
	struct polygon_data *polygon= get_polygon_data(polygon_index);
	struct monster_data *monster= get_monster_data(monster_index);
	world_distance radius, height;
	
	get_monster_dimensions(monster_index, &radius, &height);
	
	if (MONSTER_IS_PLAYER(monster))
	{
		adjust_player_for_polygon_height_change(monster_index, polygon_index, new_floor_height, new_ceiling_height);
	}
	else
	{
		struct object_data *object= get_object_data(monster->object_index);
		
		if (object->location.z==polygon->floor_height) object->location.z= new_floor_height;
	}
}

void accelerate_monster(
	short monster_index,
	world_distance vertical_velocity,
	angle direction,
	world_distance velocity)
{
	struct monster_data *monster= get_monster_data(monster_index);
	
	if (MONSTER_IS_PLAYER(monster))
	{
		accelerate_player(monster_index, vertical_velocity, direction, velocity);
	}
	else
	{
		struct object_data *object= get_object_data(monster->object_index);
		
		object->facing= NORMALIZE_ANGLE(direction+HALF_CIRCLE);
		monster->external_velocity+= velocity;
		monster->vertical_velocity+= PIN(monster->vertical_velocity+vertical_velocity, -TERMINAL_VERTICAL_MONSTER_VELOCITY, TERMINAL_VERTICAL_MONSTER_VELOCITY);
	}
}

short get_monster_impact_effect(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	short impact_effect_index= definition->impact_effect;
	
	if (MONSTER_IS_PLAYER(monster))
	{
		struct object_data *object= get_object_data(monster->object_index);
		
		switch (object->transfer_mode)
		{
			case _xfer_static:
				impact_effect_index= NONE;
				break;
		}
	}
	
	return impact_effect_index;
}

short get_monster_melee_impact_effect(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	
	return definition->melee_impact_effect;
}

/* ---------- private code */

static void cause_shrapnel_damage(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	struct monster_definition *definition= get_monster_definition(monster->type);

	if (definition->shrapnel_radius!=NONE)
	{
		damage_monsters_in_radius(NONE, NONE, NONE, &object->location, object->polygon,
			definition->shrapnel_radius, &definition->shrapnel_damage, NONE);
	}
}

// Experimental flight: no new saved state, random calls, or multiplayer changes.
static bool sprintathon_organic_flight(const monster_data* monster, const monster_definition* definition)
{
    return input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
        !game_is_networked && !game_is_being_replayed() &&
        (definition->flags & (_monster_flys | _monster_floats)) == _monster_flys &&
        !MONSTER_IS_DYING(monster) && !MONSTER_IS_ATTACKING(monster) &&
        monster->action != _monster_is_being_hit && !monster->external_velocity;
}
static double sprintathon_flight_wave(short index, int period, int offset = 0)
{
    const int phase = int((dynamic_world->tick_count % period + int(index)*37 + offset) % period);
    return std::sin(6.283185307179586 * phase / period);
}
static const int sprintathon_dodge_ticks = std::max(1, 9*TICKS_PER_SECOND/10);
static const int sprintathon_dodge_settle_ticks = std::max(1, 2*TICKS_PER_SECOND/5);
struct SprintathonClearShot {
    short object = NONE;
    int requested = -1000, start = -1, cooldown = -1, direction = 1;
    int motion_tick = -1000;
    float motion[3] = {0, 0, 0};
    float bank_axis_x = 0, bank_axis_y = 0;
};
static std::map<short, SprintathonClearShot> sprintathon_clear_shots;
static bool sprintathon_small_flyer(const monster_data* monster, const monster_definition* definition)
{
    return input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
        !game_is_networked && !game_is_being_replayed() &&
        (definition->flags & (_monster_flys | _monster_floats)) == _monster_flys &&
        definition->radius < WORLD_ONE/2 && definition->height < 3*WORLD_ONE/2 && !MONSTER_IS_DYING(monster);
}
// Local steering follows connected polygons and modest traversable steps.
// Doors and lifts remain the responsibility of normal path following.
struct SprintathonGroundSteering {
    short object = NONE;
    int tick = -1000, side = 1, until = -1, cooldown = -1, fan_until = -1, fan_check = -1;
    int body_repath = -1;
};
static std::map<short, SprintathonGroundSteering> sprintathon_ground_steering;
static bool sprintathon_ground_ai(const monster_data* monster, const monster_definition* definition)
{
    return input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
        !game_is_networked && !game_is_being_replayed() && !MONSTER_IS_PLAYER(monster) &&
        !MONSTER_IS_DYING(monster) && definition->speed > 0 &&
        !(definition->flags & (_monster_flys | _monster_floats | _monster_uses_sniper_ledges));
}
static SprintathonGroundSteering& sprintathon_ground_state(short index)
{
    const auto* monster = get_monster_data(index);
    auto& state = sprintathon_ground_steering[index];
    const int now = dynamic_world->tick_count;
    if (state.object != monster->object_index || now < state.tick || now-state.tick > 3*TICKS_PER_SECOND) {
        state = SprintathonGroundSteering{};
        state.object = monster->object_index;
        state.side = 1;
    }
    state.tick = now;
    return state;
}
static void sprintathon_request_ground_shot(short index)
{
    auto* monster = get_monster_data(index);
    if (!sprintathon_ground_ai(monster, get_monster_definition(monster->type))) return;
    monster->attack_repetitions = 0;
    auto& state = sprintathon_ground_state(index);
    const int now = dynamic_world->tick_count;
    if (now < state.cooldown) return;
    state.until = now + TICKS_PER_SECOND;
    state.cooldown = now + 2*TICKS_PER_SECOND;
}
static bool sprintathon_ground_corridor(object_data* object, world_point3d* next,
    const monster_definition* definition)
{
    short polygon = object->polygon;
    const auto* start = get_polygon_data(polygon);
    if (start->type == _polygon_is_platform) return false;
    // Follow actual adjacency, never a global XY lookup (maps can overlap).
    for (int crossings = 0; crossings < 32; ++crossings) {
        const short line_index = find_line_crossed_leaving_polygon(polygon, &object->location, next);
        if (line_index == NONE) {
            const auto* room = get_polygon_data(polygon);
            next->z = room->floor_height;
            return int(next->z)+definition->height <= room->ceiling_height;
        }
        const auto* line = get_line_data(line_index);
        if (LINE_IS_SOLID(line)) return false;
        const short adjacent = find_adjacent_polygon(polygon, line_index);
        if (adjacent == NONE || adjacent == polygon) return false;
        const auto* room = get_polygon_data(adjacent);
        const auto* previous = get_polygon_data(polygon);
        const int rise = int(room->floor_height)-previous->floor_height;
        const int total_rise = int(room->floor_height)-start->floor_height;
        const int max_step = std::min<int>(definition->maximum_ledge_delta, WORLD_ONE/4);
        const int min_step = std::max<int>(definition->minimum_ledge_delta, -WORLD_ONE/4);
        if (rise < min_step || rise > max_step || total_rise < min_step || total_rise > max_step ||
            int(std::min(previous->ceiling_height, room->ceiling_height))-
                std::max(previous->floor_height, room->floor_height) < definition->height ||
            room->type == _polygon_is_platform || room->type == _polygon_is_zone_border ||
            room->type == _polygon_is_monster_impassable || room->type == _polygon_is_teleporter ||
            room->type == _polygon_is_minor_ouch || room->type == _polygon_is_major_ouch) return false;
        if (room->media_index != NONE && room->media_index != start->media_index) {
            const auto* media = get_media_data(room->media_index);
            if (media && media->height > room->floor_height) return false;
        }
        polygon = adjacent;
    }
    return false;
}
// Soft outer shells are only for ground-enemy pairs. World/scenery clearance
// and player movement retain their existing dimensions.
struct SprintathonBodySpring {
    short object = NONE;
    int push_hops = -1;
    double vx = 0, vy = 0, rx = 0, ry = 0, fx = 0, fy = 0;
};
static std::map<short, SprintathonBodySpring> sprintathon_body_springs;

static bool sprintathon_spring_body(short index)
{
    const auto* monster = get_monster_data(index);
    const auto* definition = get_monster_definition(monster->type);
    if (!sprintathon_ground_ai(monster, definition) ||
        monster->external_velocity || monster->vertical_velocity ||
        monster->action == _monster_is_being_hit ||
        monster->action == _monster_is_teleporting_in ||
        monster->action == _monster_is_teleporting_out) return false;
    const auto* object = get_object_data(monster->object_index);
    const auto* polygon = get_polygon_data(object->polygon);
    return !OBJECT_IS_INVISIBLE(object) && polygon->type != _polygon_is_platform &&
        std::abs(int(object->location.z)-polygon->floor_height) <= WORLD_ONE/16;
}

static bool sprintathon_spring_pair(short first, short second)
{
    if (first == second || !sprintathon_spring_body(first) || !sprintathon_spring_body(second))
        return false;
    // Do not separate melee opponents while they are trying to land a hit.
    for (int i = 0; i < 2; ++i) {
        const short a = i ? second : first, b = i ? first : second;
        const auto* monster = get_monster_data(a);
        const auto* definition = get_monster_definition(monster->type);
        if (monster->target_index != b || definition->melee_attack.type == NONE) continue;
        const auto* object = get_object_data(monster->object_index);
        const auto* peer = get_object_data(get_monster_data(b)->object_index);
        const double dx = double(object->location.x)-peer->location.x;
        const double dy = double(object->location.y)-peer->location.y;
        world_distance r,h,pr,ph;
        get_monster_dimensions(a,&r,&h);get_monster_dimensions(b,&pr,&ph);
        const double reach = std::max(int(r)+pr, int(definition->melee_attack.range))+WORLD_ONE/16;
        if (dx*dx+dy*dy <= reach*reach) return false;
    }
    return true;
}

static world_distance sprintathon_spring_separation(world_distance separation)
{
    // Round up: never permit more than twenty percent compression.
    return world_distance((int(separation)*4+4)/5);
}

struct SprintathonBodyPush {
    short source, target, source_object, target_object;
    int tick, hops;
    double x,y;
};
static std::vector<SprintathonBodyPush> sprintathon_body_pushes;

static void sprintathon_queue_body_push(short source,short target,double x,double y,int hops)
{
    if (hops>3 || !sprintathon_spring_pair(source,target)) return;
    const auto* a=get_object_data(get_monster_data(source)->object_index);
    const auto* b=get_object_data(get_monster_data(target)->object_index);
    world_distance ar,ah,br,bh;
    get_monster_dimensions(source,&ar,&ah);get_monster_dimensions(target,&br,&bh);
    if (a->location.z+ah<=b->location.z || a->location.z>=int(b->location.z)+bh) return;
    world_point2d from={a->location.x,a->location.y},to={b->location.x,b->location.y};
    if (line_is_obstructed(a->polygon,&from,b->polygon,&to)) return;
    const double length=std::hypot(x,y);
    if (length<0.01) return;
    const double dx=double(b->location.x)-a->location.x;
    const double dy=double(b->location.y)-a->location.y;
    // Forward pressure only. A side scrape or an escape must not pull a peer.
    if (x*dx+y*dy<=0) return;
    const double amount=std::min(double(WORLD_ONE)/256,length)*std::pow(0.85,hops);
    x=x/length*amount;y=y/length*amount;
    const int now=dynamic_world->tick_count;
    for (auto& push:sprintathon_body_pushes) {
        if (push.tick==now && push.source==source && push.target==target) {
            // Collision probes must not multiply force during the same tick.
            if (std::hypot(x,y)>std::hypot(push.x,push.y)) {
                push.x=x;push.y=y;push.hops=hops;
            }
            return;
        }
    }
    if (sprintathon_body_pushes.size()>=size_t(MAXIMUM_MONSTERS_PER_MAP)*2) return;
    sprintathon_body_pushes.push_back({source,target,get_monster_data(source)->object_index,
        get_monster_data(target)->object_index,now,hops,x,y});
}

static void sprintathon_update_body_springs()
{
    // Snapshot forces before any character moves; no recursive push chains.
    std::map<short, SprintathonBodySpring> next;
    for (short i=0;i<MAXIMUM_MONSTERS_PER_MAP;++i) {
        const auto* monster=monsters+i;
        if (!SLOT_IS_USED(monster) || !sprintathon_spring_body(i)) continue;
        auto old=sprintathon_body_springs.find(i);
        SprintathonBodySpring state;
        if (old!=sprintathon_body_springs.end() && old->second.object==monster->object_index)
            state=old->second;
        state.object=monster->object_index;state.fx=state.fy=0;state.push_hops=-1;
        next[i]=state;
    }
    sprintathon_body_springs.swap(next);
    for (auto& entry:sprintathon_body_springs) {
        const short i=entry.first;
        const auto* object=get_object_data(entry.second.object);
        world_distance r,h;
        get_monster_dimensions(i,&r,&h);
        std::vector<short> nearby;
        possible_intersecting_monsters(&nearby,LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE,object->polygon,true);
        for (short id:nearby) {
            const auto* body=get_object_data(id);
            if (GET_OBJECT_OWNER(body)!=_object_is_monster || body->permutation<=i) continue;
            auto peer=sprintathon_body_springs.find(body->permutation);
            if (peer==sprintathon_body_springs.end() || !sprintathon_spring_pair(i,body->permutation)) continue;
            world_distance pr,ph;get_monster_dimensions(body->permutation,&pr,&ph);
            if (object->location.z+h<=body->location.z || object->location.z>=int(body->location.z)+ph) continue;
            double dx=double(object->location.x)-body->location.x;
            double dy=double(object->location.y)-body->location.y;
            const double distance=std::sqrt(dx*dx+dy*dy);
            const double compression=double(r)+pr-distance;
            if (compression<=0) continue;
            world_point2d from={object->location.x,object->location.y};
            world_point2d to={body->location.x,body->location.y};
            if (line_is_obstructed(object->polygon,&from,body->polygon,&to)) continue;
            if (distance<1) { dx=1;dy=0; }
            else { dx/=distance;dy/=distance; }
            const double force=std::min(double(WORLD_ONE)/256,compression*0.20);
            entry.second.fx+=dx*force;entry.second.fy+=dy*force;
            peer->second.fx-=dx*force;peer->second.fy-=dy*force;
        }
    }
    // Consume last tick's intentions together, independent of update order.
    for (const auto& push:sprintathon_body_pushes) {
        if (push.tick!=dynamic_world->tick_count-1) continue;
        auto source=sprintathon_body_springs.find(push.source);
        auto target=sprintathon_body_springs.find(push.target);
        if (source==sprintathon_body_springs.end() || target==sprintathon_body_springs.end() ||
            source->second.object!=push.source_object || target->second.object!=push.target_object ||
            !sprintathon_spring_pair(push.source,push.target)) continue;
        auto& state=target->second;
        state.fx+=push.x;state.fy+=push.y;
        state.push_hops=std::max(state.push_hops,push.hops);
    }
    sprintathon_body_pushes.clear();
    for (auto& entry:sprintathon_body_springs) {
        auto& state=entry.second;
        state.vx=state.vx*0.55+state.fx*0.45;
        state.vy=state.vy*0.55+state.fy*0.45;
        const double speed=std::hypot(state.vx,state.vy);
        const double cap=double(WORLD_ONE)/256;
        if (speed>cap) { state.vx*=cap/speed;state.vy*=cap/speed; }
    }
}

static void sprintathon_apply_body_spring(short index)
{
    auto it=sprintathon_body_springs.find(index);
    if (it==sprintathon_body_springs.end() || !sprintathon_spring_body(index)) return;
    auto& state=it->second;
    auto* monster=get_monster_data(index);
    auto* object=get_object_data(monster->object_index);
    if (state.object!=monster->object_index) return;
    const auto* definition=get_monster_definition(monster->type);
    state.rx+=state.vx;state.ry+=state.vy;
    const int dx=int(std::trunc(state.rx)),dy=int(std::trunc(state.ry));
    if (!dx && !dy) return;
    const int x=int(object->location.x)+dx,y=int(object->location.y)+dy;
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) {
        state.vx=state.vy=state.rx=state.ry=0;return;
    }
    world_point3d next={world_distance(x),world_distance(y),object->location.z};
    world_distance floor=get_polygon_data(object->polygon)->floor_height;
    world_distance ceiling=get_polygon_data(object->polygon)->ceiling_height;
    short supporting=object->polygon;
    bool valid=sprintathon_ground_corridor(object,&next,definition);
    if (valid) {
        keep_line_segment_out_of_walls(object->polygon,&object->location,&next,
            std::min<world_distance>(definition->maximum_ledge_delta,WORLD_ONE/4),
            definition->height,&floor,&ceiling,&supporting);
        valid=sprintathon_ground_corridor(object,&next,definition) &&
            int(next.z)+definition->height<=ceiling;
    }
    world_distance object_floor=INT16_MIN;
    const short blocker=valid ? legal_player_move(index,&next,&object_floor) : NONE;
    if (!valid || blocker!=NONE) {
        if (valid && blocker!=NONE && state.push_hops>=0 &&
            GET_OBJECT_OWNER(get_object_data(blocker))==_object_is_monster)
            sprintathon_queue_body_push(index,get_object_data(blocker)->permutation,
                state.vx,state.vy,state.push_hops+1);
        // No stored impulse against walls: remove blocked velocity immediately.
        state.vx=state.vy=state.rx=state.ry=0;return;
    }
    state.rx-=dx;state.ry-=dy;
    const short old_polygon=object->polygon;
    if (translate_map_object(monster->object_index,&next,NONE)) monster_moved(index,old_polygon);
}

struct SprintathonOverlapRecovery {
    short object = NONE, peer = NONE;
    int tick = -1, hold_until = -1;
    double nx = 0, ny = 0;
};
static std::map<short, SprintathonOverlapRecovery> sprintathon_overlap_recovery;

// Navigation headings are temporary movement intent during the monster update.
// Body contact must not turn that intent into a spinning displayed character.
struct SprintathonContactFacing {
    short object = NONE;
    angle heading = 0, shown = 0;
    int tick = -1;
    bool touching = false;
};
static std::map<short, SprintathonContactFacing> sprintathon_contact_facing;

static bool sprintathon_contact_enabled(const monster_data* monster)
{
    return input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
        !game_is_networked && !game_is_being_replayed() &&
        !MONSTER_IS_PLAYER(monster) && !MONSTER_IS_DYING(monster) &&
        !MONSTER_IS_ATTACKING(monster) && !monster->external_velocity &&
        monster->action != _monster_is_being_hit &&
        monster->action != _monster_is_teleporting_in &&
        monster->action != _monster_is_teleporting_out;
}

static bool sprintathon_near_character(short index)
{
    const auto* monster = get_monster_data(index);
    const auto* object = get_object_data(monster->object_index);
    world_distance radius, height;
    get_monster_dimensions(index, &radius, &height);
    const int margin = std::max(int(WORLD_ONE/16),
        std::abs(int(get_monster_definition(monster->type)->speed))*2);
    std::vector<short> nearby;
    possible_intersecting_monsters(&nearby, LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE, object->polygon, true);
    for (short other : nearby) {
        if (other == monster->object_index) continue;
        const auto* body = get_object_data(other);
        if (GET_OBJECT_OWNER(body) != _object_is_monster || OBJECT_IS_INVISIBLE(body)) continue;
        const auto* peer = get_monster_data(body->permutation);
        if (MONSTER_IS_DYING(peer)) continue;
        world_distance r, h;
        get_monster_dimensions(body->permutation, &r, &h);
        if (object->location.z+height <= body->location.z ||
            object->location.z >= int(body->location.z)+h) continue;
        const double dx = double(object->location.x)-body->location.x;
        const double dy = double(object->location.y)-body->location.y;
        const double reach = int(radius)+r+margin;
        if (dx*dx+dy*dy > reach*reach) continue;
        world_point2d from = {object->location.x, object->location.y};
        world_point2d to = {body->location.x, body->location.y};
        if (!line_is_obstructed(object->polygon, &from, body->polygon, &to)) return true;
    }
    return false;
}

static void sprintathon_begin_character_contact(short index)
{
    auto* monster = get_monster_data(index);
    auto* object = get_object_data(monster->object_index);
    auto old = sprintathon_contact_facing.find(index);
    // Restore movement intent before pathfinding/terrain probes use facing.
    if (old != sprintathon_contact_facing.end() &&
        old->second.object == monster->object_index &&
        old->second.tick == dynamic_world->tick_count-1 &&
        object->facing == old->second.shown)
        object->facing = old->second.heading;
    if (!sprintathon_contact_enabled(monster) || OBJECT_IS_INVISIBLE(object)) {
        sprintathon_contact_facing.erase(index);
        return;
    }
    const bool touching = sprintathon_near_character(index);
    if (!touching && old == sprintathon_contact_facing.end()) return;
    const angle shown = old != sprintathon_contact_facing.end() &&
        old->second.object == monster->object_index &&
        old->second.tick == dynamic_world->tick_count-1 ?
        old->second.shown : object->facing;
    auto& state = sprintathon_contact_facing[index];
    state.object = monster->object_index;
    state.heading = object->facing;
    state.shown = shown;
    state.tick = dynamic_world->tick_count;
    state.touching = touching;
}

static void sprintathon_end_character_contact(short index)
{
    auto it = sprintathon_contact_facing.find(index);
    if (it == sprintathon_contact_facing.end()) return;
    auto* monster = monsters+index;
    if (!SLOT_IS_USED(monster) || monster->object_index != it->second.object ||
        !sprintathon_contact_enabled(monster)) {
        sprintathon_contact_facing.erase(it);
        return;
    }
    auto* object = get_object_data(monster->object_index);
    auto& state = it->second;
    state.heading = object->facing;
    // Keep looking at the target rather than at alternating avoidance headings.
    // Without a target, retain the pre-contact facing until there is room again.
    angle desired = state.heading;
    if (state.touching) {
        desired = state.shown;
        if (MONSTER_HAS_VALID_TARGET(monster)) {
            const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
            const int dx = int(target->location.x)-object->location.x;
            const int dy = int(target->location.y)-object->location.y;
            if (dx || dy) desired = arctangent(dx, dy);
        }
    }
    const int turn = NORMALIZE_ANGLE(int(desired)-state.shown+HALF_CIRCLE)-HALF_CIRCLE;
    const int limit = std::max(1, FULL_CIRCLE/90);
    state.shown = NORMALIZE_ANGLE(int(state.shown)+std::max(-limit, std::min(limit, turn)));
    object->facing = state.shown;
    // Smooth the return to the route heading after leaving contact.
    if (!state.touching && state.shown == state.heading)
        sprintathon_contact_facing.erase(it);
}

// Resolve existing ground-body penetration independently of walking intent.
// This displacement must never become a new facing or an evasive manoeuvre.
static bool sprintathon_recover_ground_overlap(short index)
{
    if (sprintathon_spring_body(index)) {
        sprintathon_overlap_recovery.erase(index);
        sprintathon_apply_body_spring(index);
        // Pressure displacement never cancels walking, aiming or firing.
        return false;
    }

    auto* monster = get_monster_data(index);
    const auto* definition = get_monster_definition(monster->type);
    if (!sprintathon_ground_ai(monster, definition) || MONSTER_IS_ATTACKING(monster) ||
        monster->external_velocity || monster->vertical_velocity ||
        monster->action == _monster_is_being_hit ||
        monster->action == _monster_is_teleporting_in || monster->action == _monster_is_teleporting_out)
        { sprintathon_overlap_recovery.erase(index); return false; }
    auto* object = get_object_data(monster->object_index);
    if (OBJECT_IS_INVISIBLE(object) ||
        std::abs(int(object->location.z)-get_polygon_data(object->polygon)->floor_height) > WORLD_ONE/16)
        { sprintathon_overlap_recovery.erase(index); return false; }
    world_distance radius, height;
    get_monster_dimensions(index, &radius, &height);
    std::vector<short> nearby;
    possible_intersecting_monsters(&nearby, LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE, object->polygon, true);
    const int now = dynamic_world->tick_count;
    auto& state = sprintathon_overlap_recovery[index];
    if (state.object != monster->object_index || now < state.tick || now-state.tick > 1) {
        state = SprintathonOverlapRecovery{};
        state.object = monster->object_index;
    }
    double deepest = 2.0, nx = 0, ny = 0;
    short selected = NONE;
    for (short other_object : nearby) {
        if (other_object == monster->object_index) continue;
        const auto* body = get_object_data(other_object);
        if (GET_OBJECT_OWNER(body) != _object_is_monster || OBJECT_IS_INVISIBLE(body)) continue;
        const auto* peer = get_monster_data(body->permutation);
        // Ordinary melee contact and player pushing retain their existing rules.
        if (MONSTER_IS_PLAYER(peer) || MONSTER_IS_DYING(peer)) continue;
        world_distance peer_radius, peer_height;
        get_monster_dimensions(body->permutation, &peer_radius, &peer_height);
        if (object->location.z+height <= body->location.z ||
            object->location.z >= int(body->location.z)+peer_height) continue;
        double dx = double(object->location.x)-body->location.x;
        double dy = double(object->location.y)-body->location.y;
        const double length = std::sqrt(dx*dx+dy*dy);
        const double penetration = double(radius)+peer_radius-length;
        if (penetration <= 2.0) continue;
        const bool preferred = other_object == state.peer;
        if (!preferred && ((state.peer != NONE && selected == state.peer) || penetration < deepest ||
            (penetration == deepest && selected != NONE && other_object >= selected))) continue;
        world_point2d from = {object->location.x, object->location.y};
        world_point2d to = {body->location.x, body->location.y};
        if (line_is_obstructed(object->polygon, &from, body->polygon, &to)) continue;
        deepest = penetration; selected = other_object;
        if (length < 1.0) {
            // Coincident centres need a stable, opposite direction for each body.
            nx = monster->object_index < other_object ? 1.0 : -1.0; ny = 0;
        } else { nx = dx/length; ny = dy/length; }
    }
    if (selected == NONE) { sprintathon_overlap_recovery.erase(index); return false; }
    if (state.peer != selected) {
        state.peer = selected;
        state.nx = nx; state.ny = ny;
        state.hold_until = now+TICKS_PER_SECOND/2;
    }
    state.tick = now;
    nx = state.nx; ny = state.ny;
    const bool waiting = now < state.hold_until;
    const double step = std::min(double(WORLD_ONE)/128.0, std::max(1.0, deepest*0.125));
    const double x = object->location.x+nx*step, y = object->location.y+ny*step;
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) return waiting;
    world_point3d next = {static_cast<world_distance>(std::round(x)),
        static_cast<world_distance>(std::round(y)), object->location.z};
    world_distance floor = get_polygon_data(object->polygon)->floor_height;
    world_distance ceiling = get_polygon_data(object->polygon)->ceiling_height;
    short supporting = object->polygon;
    if (!sprintathon_ground_corridor(object, &next, definition)) return waiting;
    keep_line_segment_out_of_walls(object->polygon, &object->location, &next,
        std::min<world_distance>(definition->maximum_ledge_delta, WORLD_ONE/4),
        height, &floor, &ceiling, &supporting);
    if (!sprintathon_ground_corridor(object, &next, definition) || int(next.z)+height > ceiling) return waiting;
    world_distance object_floor = INT16_MIN;
    if (legal_player_move(index, &next, &object_floor) != NONE) return waiting;
    // Wall clipping must still leave a displacement that separates the pair.
    const auto* body = get_object_data(selected);
    const double dx = double(next.x)-body->location.x, dy = double(next.y)-body->location.y;
    const double old_dx = double(object->location.x)-body->location.x;
    const double old_dy = double(object->location.y)-body->location.y;
    if (dx*dx+dy*dy <= old_dx*old_dx+old_dy*old_dy) return waiting;
    const short old_polygon = object->polygon;
    if (translate_map_object(monster->object_index, &next, NONE)) monster_moved(index, old_polygon);
    state.hold_until = now+TICKS_PER_SECOND/2;
    monster_needs_path(index, false);
    return true;
}

// Contact constraints for an upright cylinder. Each normal points out of a
// neighbour; clearance is the minimum permitted outward displacement.
struct SprintathonBodyContact { double nx, ny, clearance; };
static void sprintathon_project_contacts(double& vx, double& vy,
    const std::vector<SprintathonBodyContact>& contacts, double limit, int side)
{
    // A perfectly head-on approach has no tangent to retain. Give it a small
    // consistent passing bias before resolving ALL contacts, not just the first.
    for (const auto& c : contacts) {
        const double inward = vx*c.nx+vy*c.ny;
        const double tangent = -vx*c.ny+vy*c.nx;
        if (inward < c.clearance && std::abs(tangent) < limit*0.15) {
            vx += -c.ny*side*limit*0.65;
            vy += c.nx*side*limit*0.65;
            break;
        }
    }
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (const auto& c : contacts) {
            const double into = c.clearance-(vx*c.nx+vy*c.ny);
            if (into > 0.01) {
                vx += c.nx*into; vy += c.ny*into;
                changed = true;
            }
        }
        const double length = std::sqrt(vx*vx+vy*vy);
        if (length > limit) { vx *= limit/length; vy *= limit/length; }
        if (!changed) break;
    }
}
static void sprintathon_slide_bodies(short index, world_point3d& next, int side)
{
    const auto* monster = get_monster_data(index);
    const auto* object = get_object_data(monster->object_index);
    world_distance radius, height;
    get_monster_dimensions(index, &radius, &height);
    double vx = double(next.x)-object->location.x, vy = double(next.y)-object->location.y;
    const double limit = std::sqrt(vx*vx+vy*vy);
    if (limit < 1) return;
    std::vector<short> nearby;
    possible_intersecting_monsters(&nearby, LOCAL_INTERSECTING_MONSTER_BUFFER_SIZE, object->polygon, true);
    std::vector<SprintathonBodyContact> contacts;
    for (short object_index : nearby) {
        if (object_index == monster->object_index) continue;
        const auto* body = get_object_data(object_index);
        world_distance body_radius, body_height;
        if (GET_OBJECT_OWNER(body) == _object_is_monster)
            get_monster_dimensions(body->permutation, &body_radius, &body_height);
        else if (GET_OBJECT_OWNER(body) == _object_is_scenery)
            get_scenery_dimensions(body->permutation, &body_radius, &body_height);
        else continue;
        if (next.z+height <= body->location.z || next.z >= int(body->location.z)+body_height) continue;
        double dx = double(object->location.x)-body->location.x;
        double dy = double(object->location.y)-body->location.y;
        double length = std::sqrt(dx*dx+dy*dy);
        const double separation = GET_OBJECT_OWNER(body)==_object_is_monster &&
            sprintathon_spring_pair(index,body->permutation) ?
            sprintathon_spring_separation(world_distance(int(radius)+body_radius)) :
            int(radius)+body_radius;
        if (length > separation+limit+2) continue;
        if (length < 1) { dx = monster->object_index < object_index ? 1 : -1; dy = 0; length = 1; }
        // Leave one map unit for integer rounding. Recover overlaps gradually;
        // final collision checks still reject making any overlap worse.
        const double clearance = length < separation ?
            std::min(limit*0.25, (separation-length)*0.25+1) : separation-length+1;
        contacts.push_back({dx/length, dy/length, clearance});
    }
    sprintathon_project_contacts(vx, vy, contacts, limit, side);
    const double x = object->location.x+vx, y = object->location.y+vy;
    next.x = static_cast<world_distance>(std::max(-32768.0, std::min(32767.0, std::round(x))));
    next.y = static_cast<world_distance>(std::max(-32768.0, std::min(32767.0, std::round(y))));
}

static bool sprintathon_ground_step(short index, world_distance distance, bool firing,
    const world_point3d* destination = nullptr, bool allow_attack = false)
{
    auto* monster = get_monster_data(index);
    const auto* definition = get_monster_definition(monster->type);
    if (!sprintathon_ground_ai(monster, definition) || (!allow_attack && MONSTER_IS_ATTACKING(monster)) ||
        monster->action == _monster_is_being_hit || monster->external_velocity || distance <= 0) return false;
    auto* object = get_object_data(monster->object_index);
    // Do not snap a falling or airborne ground enemy onto the floor.
    if (std::abs(int(object->location.z)-get_polygon_data(object->polygon)->floor_height) > WORLD_ONE/16)
        return false;
    auto& state = sprintathon_ground_state(index);
    angle heading = object->facing;
    if (firing && MONSTER_HAS_VALID_TARGET(monster)) {
        const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
        heading = arctangent(int(target->location.x)-object->location.x, int(target->location.y)-object->location.y);
    }
    if (destination) heading = arctangent(int(destination->x)-object->location.x,
        int(destination->y)-object->location.y);
    std::vector<angle> directions;
    if (destination) {
        directions.push_back(heading);
    } else {
        for (int attempt = 0; attempt < (firing ? 2 : 4); ++attempt) {
            const int turn = firing || (attempt & 1) ? QUARTER_CIRCLE : EIGHTH_CIRCLE;
            const int side = (firing ? attempt != 0 : attempt >= 2) ? -state.side : state.side;
            directions.push_back(NORMALIZE_ANGLE(heading+side*turn));
        }
    }
    for (size_t attempt = 0; attempt < directions.size(); ++attempt) {
        const angle direction = directions[attempt];
        world_point3d next = object->location;
        translate_point2d(&next, std::min<world_distance>(distance, WORLD_ONE/32), direction);
        if (destination && attempt==0) {
            world_distance pressure_floor=INT16_MIN;
            const short pressure_body=legal_player_move(index,&next,&pressure_floor);
            if (pressure_body!=NONE && GET_OBJECT_OWNER(get_object_data(pressure_body))==_object_is_monster)
                sprintathon_queue_body_push(index,get_object_data(pressure_body)->permutation,
                    double(next.x)-object->location.x,double(next.y)-object->location.y,0);
        }
        sprintathon_slide_bodies(index, next, state.side);
        world_distance floor = get_polygon_data(object->polygon)->floor_height;
        world_distance ceiling = get_polygon_data(object->polygon)->ceiling_height;
        short supporting = object->polygon;
        if (!sprintathon_ground_corridor(object, &next, definition)) continue;
        keep_line_segment_out_of_walls(object->polygon, &object->location, &next,
            std::min<world_distance>(definition->maximum_ledge_delta, WORLD_ONE/4),
            definition->height, &floor, &ceiling, &supporting);
        if (!sprintathon_ground_corridor(object, &next, definition) ||
            int(next.z)+definition->height > get_polygon_data(object->polygon)->ceiling_height ||
            int(next.z)+definition->height > ceiling) continue;
        world_distance object_floor = INT16_MIN;
        const short blocker = legal_player_move(index, &next, &object_floor);
        if (blocker != NONE) {
            if (destination && GET_OBJECT_OWNER(get_object_data(blocker))==_object_is_monster)
                sprintathon_queue_body_push(index,get_object_data(blocker)->permutation,
                    double(destination->x)-object->location.x,
                    double(destination->y)-object->location.y,0);
            continue;
        }
        const int dx = int(next.x)-object->location.x, dy = int(next.y)-object->location.y;
        if (dx*dx+dy*dy < 4) continue;
        const short old_polygon = object->polygon;
        if (translate_map_object(monster->object_index, &next, NONE))
            monster_moved(index, old_polygon);

        SET_MONSTER_IDLE_STATUS(monster, false);
        // Rebuild from the displaced position rather than consuming a straight
        // segment's distance for sideways movement.
        monster_needs_path(index, false);
        return true;
    }
    return false;
}

// Local combat steering: no reservations, movement quota, or waiting orders.
// Velocity is only an intent; ground_step still validates every displacement.
// Keep the release threshold inside actual weapon reach: a held attacker
// must never wait just outside the range in which try_monster_attack can fire.
struct SprintathonMeleeHold {
    short object = NONE, target = NONE, target_object = NONE;
    int tick = -1;
    bool holding = false;
};
static std::map<short, SprintathonMeleeHold> sprintathon_melee_holds;
static bool sprintathon_melee_contact(short attacker, short defender)
{
    const auto* monster = get_monster_data(attacker);
    const auto* definition = get_monster_definition(monster->type);
    if (!SLOT_IS_USED(monster) || MONSTER_IS_DYING(monster) ||
        monster->target_index != defender || definition->melee_attack.type == NONE) return false;
    auto* object = get_object_data(monster->object_index);
    auto* target = get_object_data(get_monster_data(defender)->object_index);
    return distance2d(&object->location, &target->location) < definition->melee_attack.range &&
        object->location.z < int(target->location.z)+get_monster_definition(get_monster_data(defender)->type)->height &&
        target->location.z < int(object->location.z)+definition->height &&
        clear_line_of_sight(attacker, defender, true);
}
static bool sprintathon_hold_melee(short index)
{
    auto* monster = get_monster_data(index);
    auto* definition = get_monster_definition(monster->type);
    auto& state = sprintathon_melee_holds[index];
    if (!sprintathon_ground_ai(monster, definition) || !MONSTER_HAS_VALID_TARGET(monster) ||
        definition->melee_attack.type == NONE || monster->external_velocity ||
        monster->mode != _monster_locked) {
        state = SprintathonMeleeHold{};
        return false;
    }
    auto* object = get_object_data(monster->object_index);
    auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
    const int now = dynamic_world->tick_count;
    const short target_object = get_monster_data(monster->target_index)->object_index;
    if (state.object != monster->object_index || state.target != monster->target_index ||
        state.target_object != target_object || now < state.tick || now-state.tick > 5*TICKS_PER_SECOND)
        state = SprintathonMeleeHold{};
    state.object = monster->object_index; state.target = monster->target_index;
    state.target_object = target_object; state.tick = now;
    const int range = definition->melee_attack.range;
    const int buffer = std::min<int>(WORLD_ONE/8, range/8);
    const int distance = distance2d(&object->location, &target->location);
    if (distance >= (state.holding ? range : range-buffer) ||
        !clear_line_of_sight(index, monster->target_index, true)) {
        state.holding = false;
        return false;
    }
    world_point3d origin = object->location, destination = target->location, vector;
    const angle aim = arctangent(int(destination.x)-origin.x, int(destination.y)-origin.y);
    const short polygon = position_monster_projectile(index, monster->target_index,
        &definition->melee_attack, &origin, &destination, &vector, aim);
    short obstruction = NONE;
    if (polygon == NONE || !preflight_projectile(&origin, polygon, &destination,
        definition->melee_attack.error, definition->melee_attack.type, index, monster->type, &obstruction) ||
        (obstruction != NONE && obstruction != monster->target_index)) {
        state.holding = false;
        return false;
    }
    state.holding = true;
    object->facing = aim;
    return true;
}

// A failed walking route does not imply a lost combat target. Keep this
// transient state separate from the saved monster/path structures.
struct SprintathonFailedRoute {
    short object = NONE, target = NONE, target_object = NONE;
    int tick = -1, retry = -1;
};
static std::map<short, SprintathonFailedRoute> sprintathon_failed_routes;
static bool sprintathon_visible_ranged_target(short index)
{
    auto* monster = get_monster_data(index);
    const auto* definition = get_monster_definition(monster->type);
    if (!input_preferences->sprintathon_enabled || !input_preferences->sprintathon_enhanced_enemy_ai ||
        game_is_networked || game_is_being_replayed() || MONSTER_IS_PLAYER(monster) ||
        MONSTER_IS_DYING(monster) || monster->mode != _monster_locked || !MONSTER_HAS_VALID_TARGET(monster) ||
        definition->ranged_attack.type == NONE ||
        (definition->flags & (_monster_flys | _monster_floats | _monster_cannot_attack))) return false;
    auto* target = get_monster_data(monster->target_index);
    if (!SLOT_IS_USED(target) || MONSTER_IS_DYING(target)) return false;
    auto* object = get_object_data(monster->object_index);
    auto* target_object = get_object_data(target->object_index);
    return distance2d(&object->location, &target_object->location) < definition->ranged_attack.range &&
        clear_line_of_sight(index, monster->target_index, true);
}
static bool sprintathon_wait_for_route(short index)
{
    const auto found = sprintathon_failed_routes.find(index);
    if (found == sprintathon_failed_routes.end()) return false;
    const auto* monster = get_monster_data(index);
    const auto& state = found->second;
    const int now = dynamic_world->tick_count;
    if (state.object != monster->object_index || state.target != monster->target_index ||
        now < state.tick || !sprintathon_visible_ranged_target(index) ||
        state.target_object != get_monster_data(monster->target_index)->object_index) {
        sprintathon_failed_routes.erase(found);
        return false;
    }
    return now < state.retry;
}

struct SprintathonFlockState {
    short object = NONE, target = NONE;
    int tick = -1, stuck = 0, recover_until = -1, lane_check = -1;
    double vx = 0, vy = 0;
    int high_check = -1, high_until = -1;
    world_point3d high_goal = {0, 0, 0};
    bool clear = false;
};
static std::map<short, SprintathonFlockState> sprintathon_flock;
// Probe a straight route through actual adjacent polygons in short steps.
// Each step uses the same terrain restrictions as local movement; several
// small stairs may cumulatively gain more height than a single ledge allows.
static bool sprintathon_raised_route(object_data* object, world_point3d& goal,
    const monster_definition* definition, short& polygon)
{
    object_data probe = *object;
    const double dx = double(goal.x)-object->location.x, dy = double(goal.y)-object->location.y;
    const int steps = std::max(1, int(std::ceil(std::sqrt(dx*dx+dy*dy)/(WORLD_ONE/8.0))));
    if (steps > 32) return false;
    for (int i = 1; i <= steps; ++i) {
        world_point3d next = probe.location;
        next.x = static_cast<world_distance>(object->location.x+dx*i/steps);
        next.y = static_cast<world_distance>(object->location.y+dy*i/steps);
        if (!sprintathon_ground_corridor(&probe, &next, definition)) return false;
        const short adjacent = find_new_object_polygon(&probe.location, &next, probe.polygon);
        if (adjacent == NONE) return false;
        probe.location = next; probe.polygon = adjacent;
    }
    goal = probe.location; polygon = probe.polygon;
    return true;
}
static bool sprintathon_raised_shot(short index, world_point3d goal, short polygon)
{
    const auto* monster = get_monster_data(index);
    const auto* definition = get_monster_definition(monster->type);
    const auto* target_monster = get_monster_data(monster->target_index);
    world_point3d aim = get_object_data(target_monster->object_index)->location;
    aim.z += get_monster_definition(target_monster->type)->height/2;
    goal.z += definition->height/2;
    const double dx = double(aim.x)-goal.x, dy = double(aim.y)-goal.y;
    if (dx*dx+dy*dy >= double(definition->ranged_attack.range)*definition->ranged_attack.range) return false;
    short obstruction = NONE;
    return preflight_projectile(&goal, polygon, &aim, 0, definition->ranged_attack.type,
        index, monster->type, &obstruction) && (obstruction == NONE || obstruction == monster->target_index);
}
static void sprintathon_seek_raised_position(short index, SprintathonFlockState& state,
    double preferred, double& fx, double& fy)
{
    auto* monster = get_monster_data(index);
    auto* object = get_object_data(monster->object_index);
    const auto* definition = get_monster_definition(monster->type);
    const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
    const int now = dynamic_world->tick_count;
    // Stagger the first scan as well as subsequent scans across the group.
    if (state.high_check < 0) state.high_check = now+index%(2*TICKS_PER_SECOND);
    if (now >= state.high_check) {
        state.high_check = now+3*TICKS_PER_SECOND+index%17;
        if (state.high_until <= now) {
            double best = 0.10;
            for (int sample = 0; sample < 24; ++sample) {
                const double angle = 6.283185307179586*((sample+index)%12)/12.0;
                const double travel = (sample < 12 ? 1.5 : 3.0)*WORLD_ONE;
                const double x = object->location.x+std::cos(angle)*travel;
                const double y = object->location.y+std::sin(angle)*travel;
                if (x < -32768 || x > 32767 || y < -32768 || y > 32767) continue;
                world_point3d goal = {static_cast<world_distance>(x), static_cast<world_distance>(y), object->location.z};
                short polygon = NONE;
                if (!sprintathon_raised_route(object, goal, definition, polygon)) continue;
                const int rise = int(goal.z)-object->location.z;
                if (rise < WORLD_ONE/8 || rise > 2*WORLD_ONE) continue;
                const double dx = double(target->location.x)-goal.x, dy = double(target->location.y)-goal.y;
                const double range = std::sqrt(dx*dx+dy*dy);
                if (range < preferred*0.65 || !sprintathon_raised_shot(index, goal, polygon)) continue;
                double score = std::min(1.0, double(rise)/WORLD_ONE)-0.08*travel/WORLD_ONE-
                    0.15*std::abs(range-preferred)/preferred;
                // Occupied spots and neighbours' current destinations cost
                // more, without granting reservations or making anyone wait.
                for (const auto& entry : sprintathon_flock) {
                    if (entry.first == index || now-entry.second.tick > TICKS_PER_SECOND || entry.second.tick > now) continue;
                    const auto* peer = get_monster_data(entry.first);
                    if (!SLOT_IS_USED(peer) || MONSTER_IS_DYING(peer) || peer->object_index != entry.second.object) continue;
                    const auto* body = get_object_data(peer->object_index);
                    const auto& position = entry.second.high_until > now ? entry.second.high_goal : body->location;
                    if (std::abs(int(position.z)-goal.z) > WORLD_ONE/2) continue;
                    const double px = double(position.x)-goal.x, py = double(position.y)-goal.y;
                    const double spacing = definition->radius+get_monster_definition(peer->type)->radius+WORLD_ONE;
                    if (px*px+py*py < spacing*spacing) score -= 0.75;
                }
                if (score > best) { best = score; state.high_goal = goal; state.high_until = now+5*TICKS_PER_SECOND; }
            }
        }
    }
    if (state.high_until <= now) return;
    world_point3d goal = state.high_goal;
    short polygon = NONE;
    const double dx = double(goal.x)-object->location.x, dy = double(goal.y)-object->location.y;
    const double distance = std::sqrt(dx*dx+dy*dy);
    if (distance < WORLD_ONE/4 || !sprintathon_raised_route(object, goal, definition, polygon) ||
        ((now+index)%15 == 0 && !sprintathon_raised_shot(index, goal, polygon))) {
        state.high_until = now;
        return;
    }
    // Mild travel bias, applied before separation; do not replace local safety
    // or aiming, and do not interrupt a committed firing animation.
    if (!MONSTER_IS_ATTACKING(monster)) {
        fx = fx*0.35+0.65*dx/distance;
        fy = fy*0.35+0.65*dy/distance;
    }
}
static void sprintathon_reset_flock()
{
    sprintathon_flock.clear();
    sprintathon_melee_holds.clear();
    sprintathon_failed_routes.clear();
    sprintathon_ground_steering.clear();
    sprintathon_overlap_recovery.clear();
    sprintathon_body_springs.clear();
    sprintathon_body_pushes.clear();
    sprintathon_contact_facing.clear();
}
static bool sprintathon_flock_move(short index, bool* moved)
{
    if (moved) *moved = false;
    auto* monster = get_monster_data(index);
    auto* definition = get_monster_definition(monster->type);
    if (!sprintathon_ground_ai(monster, definition) || !MONSTER_HAS_VALID_TARGET(monster) ||
        definition->ranged_attack.type == NONE || MONSTER_IS_BERSERK(monster) ||
        monster->external_velocity || monster->action == _monster_is_being_hit ||
        monster->action == _monster_is_attacking_close) return false;
    auto* object = get_object_data(monster->object_index);
    auto& state = sprintathon_flock[index];
    const int now = dynamic_world->tick_count;
    if (state.object != monster->object_index || state.target != monster->target_index ||
        now < state.tick || now-state.tick > TICKS_PER_SECOND) {
        state = SprintathonFlockState{};
        state.object = monster->object_index; state.target = monster->target_index;
    }
    state.tick = now;
    // Stock paths handle unseen targets, doors/lifts and failed local routes.
    if (now < state.recover_until || !clear_line_of_sight(index, monster->target_index, true)) {
        state.vx = state.vy = 0;
        state.high_until = now;
        return false;
    }
    const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
    const double tx = double(target->location.x)-object->location.x;
    const double ty = double(target->location.y)-object->location.y;
    const double distance = std::sqrt(tx*tx+ty*ty);
    if (distance < 1 || (definition->melee_attack.type != NONE && distance < definition->melee_attack.range))
        return false;
    const double ux = tx/distance, uy = ty/distance;
    world_distance radius, height;
    get_monster_dimensions(index, &radius, &height);
    auto& steering = sprintathon_ground_state(index);
    // Aim separately from travel. Do not rotate an already committed volley.
    if (!MONSTER_IS_ATTACKING(monster)) object->facing = arctangent(int(tx), int(ty));
    // Use the real muzzle/preflight, rather than a center-to-center planning ray.
    if (now >= state.lane_check) {
        world_point3d origin = object->location, destination = target->location, vector;
        const short polygon = position_monster_projectile(index, monster->target_index,
            &definition->ranged_attack, &origin, &destination, &vector, arctangent(int(tx), int(ty)));
        short obstruction = NONE;
        state.clear = polygon != NONE && distance < definition->ranged_attack.range &&
            preflight_projectile(&origin, polygon, &destination, definition->ranged_attack.error,
                definition->ranged_attack.type, index, monster->type, &obstruction) &&
            (obstruction == NONE || obstruction == monster->target_index);
        state.lane_check = now+5+index%3;
    }
    // Actual friendly-fire rejections override cached planning immediately.
    const bool clear = state.clear && now >= steering.until;
    const double preferred = std::max(double(WORLD_ONE),
        std::min(6.0*WORLD_ONE, 0.55*definition->ranged_attack.range));
    const bool engaged = sprintathon_melee_contact(monster->target_index, index);
    const double approach = distance > preferred*1.25 ? 1.0 :
        (distance < preferred*0.65 && !engaged) ? -0.65 : 0.0;
    double fx = ux*approach, fy = uy*approach;
    if (approach > 0 && clear) {
        // Bend only the approach component; firing-lane and separation forces
        // below retain priority. Stop curving before the desired firing range.
        const double turn = sprintathon_approach_curve(index,
            std::max(0.0, distance-preferred*1.25), arctangent(int(tx), int(ty)));
        const double tangent = std::tan(turn);
        fx += -uy*tangent;
        fy += ux*tangent;
    }
    if (!clear) {
        fx += -uy*steering.side*0.75;
        fy += ux*steering.side*0.75;
    }
    if (!engaged) sprintathon_seek_raised_position(index, state, preferred, fx, fy);
    else state.high_until = now;
    double crowd_pressure = 0, nearest_gap = 1.0e30, nearest_nx = 1, nearest_ny = 0;
    // Soft personal space; body radius supplies relative weight. A common
    // handedness makes head-on pairs pass rather than both choosing one gap.
    for (short other = 0; other < MAXIMUM_MONSTERS_PER_MAP; ++other) {
        const auto* peer = monsters+other;
        if (other == index || other == monster->target_index || !SLOT_IS_USED(peer) ||
            MONSTER_IS_DYING(peer) || MONSTER_IS_PLAYER(peer)) continue;
        // A melee attacker is not an ally asking us to yield personal space.
        if (sprintathon_melee_contact(other, index)) continue;
        const auto* body = get_object_data(peer->object_index);
        const auto* pd = get_monster_definition(peer->type);
        if (OBJECT_IS_INVISIBLE(body) || (get_monster_attitude(index, other) == _hostile &&
            !(definition->_class & pd->_class))) continue;
        world_distance peer_radius, peer_height;
        get_monster_dimensions(other, &peer_radius, &peer_height);
        if (object->location.z >= int(body->location.z)+peer_height ||
            body->location.z >= int(object->location.z)+height) continue;
        double dx = double(object->location.x)-body->location.x;
        double dy = double(object->location.y)-body->location.y;
        double gap = std::sqrt(dx*dx+dy*dy);
        const double contact = radius+peer_radius;
        const double space = contact+WORLD_ONE*0.6;
        if (gap >= space || !clear_line_of_sight(index, other, true)) continue;
        if (gap < 1) { dx = index < other ? 1 : -1; dy = 0; gap = 1; }
        const double nx = dx/gap, ny = dy/gap;
        const double weight = double(peer_radius)*peer_radius /
            std::max(1.0, double(peer_radius)*peer_radius+double(radius)*radius);
        const double pressure = std::min(2.0, (space-gap)/(space-contact));
        crowd_pressure += pressure*weight;
        if (gap < nearest_gap) { nearest_gap = gap; nearest_nx = nx; nearest_ny = ny; }
        fx += nx*pressure*weight*2.5;
        fy += ny*pressure*weight*2.5;
        // Tangential motion breaks a compressed row's symmetry, even at rest.
        fx += -ny*steering.side*pressure*weight*0.9;
        fy += nx*steering.side*pressure*weight*0.9;
    }
    // Opposing neighbours can cancel their forces in the middle of a crowd.
    // Treat that as congestion, not a useful stationary firing position.
    if (crowd_pressure > 0.2 && fx*fx+fy*fy < 0.04) {
        fx = nearest_nx*0.35-nearest_ny*steering.side*0.8;
        fy = nearest_ny*0.35+nearest_nx*steering.side*0.8;
    }
    const double force = std::sqrt(fx*fx+fy*fy);
    const double speed = std::min<int>(definition->speed, WORLD_ONE/32);
    if (force > 1) { fx /= force; fy /= force; }
    // Damped acceleration; heavier bodies change their movement more slowly.
    const double response = std::max(0.12, std::min(0.35,
        0.22*WORLD_ONE/std::max<int>(WORLD_ONE/2, definition->radius)));
    state.vx += (fx*speed-state.vx)*response;
    state.vy += (fy*speed-state.vy)*response;
    const double travel = std::sqrt(state.vx*state.vx+state.vy*state.vy);
    if (travel < 2) {
        // Fractional steering must not become an endless "successful hold".
        if (force < 0.05 && clear) { state.stuck = 0; return true; }
        if (++state.stuck >= TICKS_PER_SECOND/2) {
            steering.side = -steering.side;
            state.stuck = 0; state.recover_until = now+TICKS_PER_SECOND;
            monster_needs_path(index, false);
            return false;
        }
        return true;
    }
    const double x = object->location.x+state.vx, y = object->location.y+state.vy;
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) return false;
    world_point3d next = {static_cast<world_distance>(x), static_cast<world_distance>(y), object->location.z};
    const world_point3d before = object->location;
    if (sprintathon_ground_step(index, static_cast<world_distance>(travel), true, &next, true)) {
        state.stuck = 0;
        // Remember the successful slide rather than pushing into the blocker.
        state.vx = double(object->location.x)-before.x;
        state.vy = double(object->location.y)-before.y;
        if (moved) *moved = true;
        return true;
    }
    state.vx *= 0.5; state.vy *= 0.5;
    if (++state.stuck >= TICKS_PER_SECOND/2) {
        steering.side = -steering.side;
        state.stuck = 0; state.recover_until = now+TICKS_PER_SECOND;
        monster_needs_path(index, false);
        return false;
    }
    return true;
}

static void sprintathon_request_clear_shot(short index)
{
    const auto* monster = get_monster_data(index);
    if (!sprintathon_small_flyer(monster, get_monster_definition(monster->type))) return;
    get_monster_data(index)->attack_repetitions = 0; // finish this volley before repositioning
    auto& dodge = sprintathon_clear_shots[index];
    const int now = dynamic_world->tick_count;
    if (dodge.object != monster->object_index || now < dodge.requested) dodge = SprintathonClearShot{};
    if (now < dodge.cooldown) return;
    dodge.object = monster->object_index; dodge.requested = now; dodge.start = -1;
    dodge.bank_axis_x = dodge.bank_axis_y = 0;
    dodge.cooldown = now + 2*TICKS_PER_SECOND;
    dodge.direction = ((index + now/TICKS_PER_SECOND) & 1) ? 1 : -1;
}

bool sprintathon_flight_dodge_motion(short index, float* motion)
{
    const auto it = sprintathon_clear_shots.find(index);
    if (it == sprintathon_clear_shots.end()) return false;
    const auto* monster = get_monster_data(index);
    const auto& dodge = it->second;
    const int age = dynamic_world->tick_count-dodge.motion_tick;
    if (age < 0 || age > 1 || dodge.object != monster->object_index ||
        !sprintathon_small_flyer(monster, get_monster_definition(monster->type))) return false;
    const float length = std::sqrt(dodge.motion[0]*dodge.motion[0]+dodge.motion[1]*dodge.motion[1]);
    if (length < 1) return false;
    const float exposure = std::min(3.0f, float(WORLD_ONE)/3/length);
    for (int i=0; i<3; ++i) motion[i] = dodge.motion[i]*exposure;
    return true;
}

struct SprintathonFlightBank {
    int tick = -1;
    short object = NONE;
    float x = 0, y = 0, target_x = 0, target_y = 0;
};
static std::map<short, SprintathonFlightBank> sprintathon_flight_banks;
float sprintathon_flight_bank(short index, float camera_yaw)
{
    if (!input_preferences->sprintathon_enhanced_enemy_ai) return 0;
    // Large flyers keep their movement but remain visually level.
    const auto* definition = get_monster_definition(get_monster_data(index)->type);
    if (definition->radius >= WORLD_ONE/2 || definition->height >= 3*WORLD_ONE/2)
        return 0;

    // Play the dodge bank directly from its timeline. Attack transitions can
    // clear the ordinary flight target while the dodge is still visible.
    const auto* monster = get_monster_data(index);
    const auto dodge_it = sprintathon_clear_shots.find(index);
    if (sprintathon_small_flyer(monster, definition) &&
        monster->action != _monster_is_being_hit && !monster->external_velocity &&
        dodge_it != sprintathon_clear_shots.end()) {
        const auto& dodge = dodge_it->second;
        const int elapsed = dynamic_world->tick_count-dodge.start;
        const int total = sprintathon_dodge_ticks+sprintathon_dodge_settle_ticks;
        if (dodge.object == monster->object_index && dodge.start >= 0 &&
            elapsed >= 0 && elapsed <= total &&
            (dodge.bank_axis_x != 0 || dodge.bank_axis_y != 0)) {
            const bool settling = elapsed >= sprintathon_dodge_ticks;
            const float phase = settling ?
                float(elapsed-sprintathon_dodge_ticks)/sprintathon_dodge_settle_ticks :
                float(elapsed)/sprintathon_dodge_ticks;
            const float wave = float(std::sin(3.141592653589793*phase));
            const float tilt = (settling ? 10.0f : -32.0f)*wave*wave;
            return tilt*(-std::sin(camera_yaw)*dodge.bank_axis_x +
                std::cos(camera_yaw)*dodge.bank_axis_y);
        }
    }

    const auto it = sprintathon_flight_banks.find(index);
    if (it == sprintathon_flight_banks.end() || !input_preferences->sprintathon_enabled || game_is_networked)
        return 0;
    const auto& bank = it->second;
    if (dynamic_world->tick_count-bank.tick < 0 || dynamic_world->tick_count-bank.tick > 1 || get_monster_data(index)->object_index != bank.object) return 0;
    return -std::sin(camera_yaw)*bank.x + std::cos(camera_yaw)*bank.y;
}

// Transient single-player motion only; no map/save format or random-stream changes.
struct SprintathonFloatMotion {
    short object = NONE;
    int tick = -1, request_tick = -1, ledge_until = -1;
    double speed = 0, heading = 0, goal = 0, vz = 0;
    int floor = 0;
    int climb_floor = 0, climb_goal = 0, climb_until = -1;
};
static std::map<short, SprintathonFloatMotion> sprintathon_float_motion;
static bool sprintathon_soft_float(const monster_data* monster, const monster_definition* definition)
{
    return input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
        !game_is_networked && !game_is_being_replayed() &&
        (definition->flags & (_monster_flys | _monster_floats)) == _monster_floats &&
        !MONSTER_IS_DYING(monster) && monster->action != _monster_is_being_hit &&
        !monster->external_velocity;
}

static void sprintathon_float_coast(short index)
{
    auto it = sprintathon_float_motion.find(index);
    if (it == sprintathon_float_motion.end()) return;
    auto* monster = get_monster_data(index);
    auto* definition = get_monster_definition(monster->type);
    if (!sprintathon_soft_float(monster, definition)) {
        sprintathon_float_motion.erase(it);
        return;
    }
    auto& motion = it->second;
    if (motion.object != monster->object_index || motion.tick != dynamic_world->tick_count ||
        motion.request_tick == dynamic_world->tick_count) return;
    motion.speed *= 0.80;
    if (motion.speed < 1) { motion.speed = 0; return; }
    auto* object = get_object_data(monster->object_index);
    world_point3d next = object->location;
    const angle heading = NORMALIZE_ANGLE(int(motion.heading));
    translate_point2d(&next, world_distance(motion.speed), heading);
    world_distance floor, ceiling;
    short supporting;
    keep_line_segment_out_of_walls(object->polygon, &object->location, &next,
        0, definition->height, &floor, &ceiling, &supporting);
    if (next.z < floor || int(next.z)+definition->height > ceiling ||
        legal_monster_move(index, heading, &next) != NONE) { motion.speed = 0; return; }
    const short previous = object->polygon;
    if (translate_map_object(monster->object_index, &next, NONE)) monster_moved(index, previous);
    // Coasting never consumes the path: it may happen during a firing animation.
}

static int sprintathon_flight_velocity(int error, int velocity, int limit)
{
    limit = std::max(1, limit);
    const int target = std::max(-limit, std::min(limit, error / 10));
    const int delta = target - velocity;
    return velocity + (delta / 6 != 0 ? delta / 6 : (delta > 0 ? 1 : delta < 0 ? -1 : 0));
}

static void update_monster_vertical_physics_model(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	struct object_data *object= get_object_data(monster->object_index);
	struct polygon_data *polygon= get_polygon_data(object->polygon);
	struct media_data *media= polygon->media_index==NONE ? (struct media_data *) NULL : get_media_data(polygon->media_index);
	uint32 moving_flags= MONSTER_IS_DYING(monster) ? 0 : (definition->flags&(_monster_flys|_monster_floats));
	world_distance gravity= (static_world->environment_flags&_environment_low_gravity) ? (definition->gravity>>1) : definition->gravity;
	world_distance floor_height= polygon->floor_height;
	world_distance desired_height;
	world_distance old_height= object->location.z;
	bool above_ground, below_ground;
    const bool organic_flight = sprintathon_organic_flight(monster, definition);
    const bool soft_float = sprintathon_soft_float(monster, definition);
    if (!soft_float) sprintathon_float_motion.erase(monster_index);
    if (organic_flight || sprintathon_flight_banks.count(monster_index)) {
        auto& bank = sprintathon_flight_banks[monster_index];
        if (bank.object != monster->object_index || dynamic_world->tick_count-bank.tick != 1)
            bank = SprintathonFlightBank{};
        bank.object = monster->object_index; bank.tick = dynamic_world->tick_count;
        float target_x = organic_flight ? bank.target_x : 0;
        float target_y = organic_flight ? bank.target_y : 0;
        float response = 0.18f;
        bank.x += (target_x-bank.x)*response;
        bank.y += (target_y-bank.y)*response;
        bank.target_x = bank.target_y = 0;
    }

	if (media)
	{
		// flying and floating monsters treat media as the floor
		if (moving_flags && media->height>floor_height) floor_height= media->height + WORLD_ONE/16;
		
		// take damage if necessary
		if (media->height>object->location.z)
		{
			struct damage_definition *damage= get_media_damage(polygon->media_index, FIXED_ONE);
			
			if (damage) damage_monster(monster_index, NONE, NONE, NULL, damage, NONE);
		}
	}
	desired_height= (monster->desired_height==NONE||MONSTER_IS_DYING(monster)) ? polygon->floor_height : monster->desired_height;
    if (soft_float) {
        auto& motion = sprintathon_float_motion[monster_index];
        const int now = dynamic_world->tick_count;
        if (motion.object != monster->object_index || motion.tick != now-1) {
            motion = SprintathonFloatMotion{};
            motion.object = monster->object_index;
            motion.heading = object->facing;
            motion.goal = object->location.z;
            motion.floor = floor_height;
            motion.vz = monster->vertical_velocity;
        }
        motion.tick = now;
        if (int(floor_height) < motion.floor - WORLD_ONE/16)
            motion.ledge_until = now + TICKS_PER_SECOND/3;
        motion.floor = floor_height;
        const int top = std::max(int(polygon->floor_height), int(polygon->ceiling_height)-definition->height);
        int target = std::max(int(floor_height), int(desired_height));
        // Keep an explicit terrain climb through pauses to fire. The normal
        // desired_height is reset to the current floor each tick.
        if (floor_height >= motion.climb_floor) motion.climb_until = -1;
        const bool climbing = now < motion.climb_until;
        if (climbing) target = std::max(target, motion.climb_goal);
        // Rise more promptly; retain the slow delayed descent over drops.
        if (now >= motion.ledge_until || target > motion.goal)
            motion.goal += (target-motion.goal)*(climbing ? 0.18 : 0.09);
        motion.goal = std::max(double(polygon->floor_height), std::min(double(top), motion.goal));
        desired_height = world_distance(motion.goal);
    }
	above_ground= object->location.z>desired_height;
	below_ground= object->location.z<desired_height;

	switch (moving_flags)
	{
		case 0:
			/* if we’re above the floor, adjust vertical velocity */
			if (above_ground) monster->vertical_velocity= FLOOR(monster->vertical_velocity-gravity, -definition->terminal_velocity);
			if (below_ground) monster->vertical_velocity= 0, object->location.z= desired_height;
			break;
		
		case _monster_flys:
            if (organic_flight) {
                monster->vertical_velocity = sprintathon_flight_velocity(
                    int(desired_height)-object->location.z, monster->vertical_velocity, definition->terminal_velocity);
                break;
            }
			if (above_ground && !MONSTER_IS_ATTACKING(monster)) monster->vertical_velocity= FLOOR(monster->vertical_velocity-gravity, -definition->terminal_velocity);
			if (below_ground) monster->vertical_velocity= CEILING(monster->vertical_velocity+gravity, definition->terminal_velocity);
			break;

		case _monster_floats:
            if (soft_float) {
                auto& motion = sprintathon_float_motion[monster_index];
                const double limit = std::max(1, int(definition->terminal_velocity));
                const double target = std::max(-limit, std::min(limit, (motion.goal-object->location.z)*0.10));
                motion.vz += (target-motion.vz)*0.14;
                monster->vertical_velocity = world_distance(std::round(motion.vz));
                break;
            }
			if (above_ground && !MONSTER_IS_ATTACKING(monster)) monster->vertical_velocity= FLOOR(monster->vertical_velocity-gravity, -definition->terminal_velocity);
			if (below_ground) monster->vertical_velocity= CEILING(monster->vertical_velocity+gravity, definition->terminal_velocity);
			break;
		
		default:
			/* can’t fly and float, beavis */
			// LP change: this stuff put in to handle the map "Aqualung" correctly
			if (above_ground && !MONSTER_IS_ATTACKING(monster)) monster->vertical_velocity= FLOOR(monster->vertical_velocity-gravity, -definition->terminal_velocity);
			if (below_ground) monster->vertical_velocity= CEILING(monster->vertical_velocity+gravity, definition->terminal_velocity);
			break;
	}
	
	/* add our vertical velocity to z */
	object->location.z= PIN(object->location.z+monster->vertical_velocity, polygon->floor_height, polygon->ceiling_height-definition->height);

	/* if we’re under the floor moving down, put us on the floor and clear our velocity;
		if we’re above the floor moving up, put us on the floor and clear our velocity if we were previously below ground */
	switch (moving_flags)
	{
		case 0:
		case _monster_floats:
            if (soft_float) break;
			if (object->location.z<=desired_height && monster->vertical_velocity<0) monster->vertical_velocity= 0, object->location.z= desired_height;
			if (object->location.z>=desired_height && monster->vertical_velocity>0 && below_ground) monster->vertical_velocity= 0, object->location.z= desired_height;
			break;
		
		case _monster_flys:
		default: // LP: added this case to handle "Aqualung" correctly
            if (organic_flight) break; // damped steering settles without snapping to the moving goal
			if (object->location.z<=desired_height && above_ground) monster->vertical_velocity>>= 1, object->location.z= desired_height;
			if (object->location.z>=desired_height && below_ground) monster->vertical_velocity>>= 1, object->location.z= desired_height;
			break;
	}

	/* reset desired height (flying and floating monsters often change this later) */
	if (moving_flags&_monster_flys)
	{
		/* we’re flying!: if we have no target, take the middle ground; if we have a target aim
			for his midsection */
		if (MONSTER_HAS_VALID_TARGET(monster))
		{
			struct monster_data *target= get_monster_data(monster->target_index);
			struct monster_definition *target_definition= get_monster_definition(target->type);
			
			monster->desired_height= get_object_data(target->object_index)->location.z + ((target_definition->height-definition->height)>>1) + definition->preferred_hover_height;
			monster->desired_height= PIN(monster->desired_height, floor_height+(definition->height>>2), polygon->ceiling_height-definition->height);
		}
		else
		{
			if (monster->random_desired_height<floor_height || monster->random_desired_height>polygon->ceiling_height)
			{
				world_distance delta= polygon->ceiling_height-floor_height-definition->height;
				
				monster->random_desired_height= floor_height + ((delta>0) ? (global_random()%delta) : 0);
			}
			
			monster->desired_height= MONSTER_IS_DYING(monster) ? polygon->floor_height : monster->random_desired_height;
		}
	}
	else
	{
		monster->desired_height= floor_height;
	}

    if (organic_flight) {
        const int bottom = floor_height;
        const int top = int(polygon->ceiling_height) - definition->height;
        const int space = std::max(0, top-bottom);
        bool close_target = false;
        if (MONSTER_HAS_VALID_TARGET(monster)) {
            const auto* target_object = get_object_data(get_monster_data(monster->target_index)->object_index);
            const double dx = double(target_object->location.x)-object->location.x;
            const double dy = double(target_object->location.y)-object->location.y;
            close_target = dx*dx+dy*dy < double(2*WORLD_ONE)*(2*WORLD_ONE);
        }
        if (space > 0 && !close_target) {
            const int clearance = std::min(space/2, WORLD_ONE/8);
            const int cruise = bottom + std::min(space*2/5, 3*WORLD_ONE/2);
            const int amplitude = std::min(space/10, WORLD_ONE/4);
            const int base_height = MONSTER_HAS_VALID_TARGET(monster) ?
                std::max(int(monster->desired_height), cruise) : cruise;
            const int goal = base_height + int(amplitude*sprintathon_flight_wave(monster_index, 5*TICKS_PER_SECOND));
            monster->desired_height = std::max(bottom+clearance, std::min(top-clearance, goal));
        }
        if ((object->location.z <= polygon->floor_height && monster->vertical_velocity < 0) ||
            (object->location.z >= top && monster->vertical_velocity > 0)) monster->vertical_velocity = 0;
    }

    if (soft_float) {
        auto& motion = sprintathon_float_motion[monster_index];
        if ((object->location.z <= floor_height && motion.vz < 0) ||
            (int(object->location.z)+definition->height >= polygon->ceiling_height && motion.vz > 0)) {
            motion.vz = 0;
            monster->vertical_velocity = 0;
        }
    }
	monster->sound_location= object->location;
	monster->sound_polygon_index= object->polygon;
	monster->sound_location.z+= definition->height - (definition->height>>1);

	if (media)
	{
		world_point3d location= object->location;
		short media_effect_type= NONE;
		
		location.z= media->height;
		if (old_height>=media->height && object->location.z<media->height)
		{
			media_effect_type= _large_media_detonation_effect;
		}
		if (old_height<media->height && object->location.z>=media->height)
		{
			media_effect_type= _large_media_emergence_effect;
		}
		
		if (media_effect_type!=NONE)
		{
			short effect_type= NONE;
			
			get_media_detonation_effect(polygon->media_index, media_effect_type, &effect_type);
			new_effect(&location, object->polygon, effect_type, object->facing);
		}
	}
}

static void update_monster_physics_model(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	struct object_data *object= get_object_data(monster->object_index);
	
	if (monster->external_velocity)
	{
		world_point3d new_location= object->location;
		world_distance adjusted_floor_height, adjusted_ceiling_height;
		angle negative_facing= NORMALIZE_ANGLE(HALF_CIRCLE+object->facing);
		struct polygon_data *polygon;
		short supporting_polygon_index;

		/* move the monster */		
		translate_point2d(&new_location, monster->external_velocity, negative_facing);
		keep_line_segment_out_of_walls(object->polygon, &object->location, &new_location,
			0, definition->height, &adjusted_floor_height, &adjusted_ceiling_height, &supporting_polygon_index);
		if (legal_monster_move(monster_index, negative_facing, &new_location)==NONE)
		{
			short old_polygon_index= object->polygon;
			
			if (translate_map_object(monster->object_index, &new_location, NONE)) monster_moved(monster_index, old_polygon_index);
		}
		
		/* slow him down if he’s touching the ground or flying */
		polygon= get_polygon_data(object->polygon);
		if (object->location.z<=polygon->floor_height || (definition->flags&(_monster_flys|_monster_floats)))
		{
			if ((monster->external_velocity-= MONSTER_EXTERNAL_DECELERATION)<MONSTER_MINIMUM_EXTERNAL_VELOCITY)
			{
				monster->external_velocity= 0;
			}
		}
	}
}

static void monster_needs_path(
	short monster_index,
	bool immediately)
{
	struct monster_data *monster= get_monster_data(monster_index);
	
	if (monster->path!=NONE && immediately) delete_path(monster->path), monster->path= NONE;
	if (monster->action==_monster_is_moving && immediately) set_monster_action(monster_index, _monster_is_stationary);
	SET_MONSTER_NEEDS_PATH_STATUS(monster, true);
}

void set_monster_mode(
	short monster_index,
	short new_mode,
	short target_index)
{
	struct monster_data *monster= get_monster_data(monster_index);

	/* if we were locked on a monster in our own polygon and we lost him then we don’t have a path
		and going anywhere would be dangerous so we need to ask for a new path */
	if (monster->mode==_monster_locked&&new_mode!=_monster_locked&&monster->path==NONE) monster_needs_path(monster_index, false);

	switch (new_mode)
	{
		case _monster_locked:
			(void)get_monster_data(target_index); /* for bounds checking only */
			monster->target_index= target_index;
			CLEAR_TARGET_DAMAGE_FLAG(monster);
//			if (target_index==local_player->monster_index)
//			dprintf("monster #%d is locked on new target #%d;g;", monster_index, target_index);
//			switch (monster->type)
//			{
//				case _civilian_crew: case _civilian_engineering: case _civilian_science: case _civilian_security:
//				dprintf("monster #%d is locked on new target #%d;g;", monster_index, target_index);
//			}
			break;
		
		case _monster_losing_lock: /* target_index ignored, but still valid */
		case _monster_lost_lock:
			(void)get_monster_data(monster->target_index); /* for bounds checking only */
			break;
		
		case _monster_unlocked:
			monster->target_index= NONE;
			break;
		
		default:
			assert(false);
			break;
	}
	
	monster->mode= new_mode;
}

/* this function decides what the given monster actually wants to do, and then generates a path
	to get him there; if a monster who has lost lock calls this function, he will be forced to
	wander randomly or follow a guard path. */
static void generate_new_path_for_monster(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	struct monster_pathfinding_data data;
	short destination_polygon_index;
	world_point2d *destination;
	world_point2d bias;

    // Suppress repeated path attempts, not attacks or genuine activations.
    if (sprintathon_wait_for_route(monster_index)) {
        SET_MONSTER_NEEDS_PATH_STATUS(monster, false);
        return;
    }

	/* delete this monster’s old path, if one exists, and clear the need path flag */
	if (monster->path!=NONE) delete_path(monster->path), monster->path= NONE;
	SET_MONSTER_NEEDS_PATH_STATUS(monster, false);

	switch (monster->mode)
	{
		case _monster_losing_lock:
			/* our target is out of sight, but we’re still zen-ing his position until we run out
				of intelligence points */
		case _monster_locked:
		{
			struct monster_data *target= get_monster_data(monster->target_index);
			struct object_data *target_object= get_object_data(target->object_index);

			if (definition->random_sound_mask && !(global_random()&definition->random_sound_mask)) play_object_sound(monster->object_index, definition->random_sound);

			/* if we can’t attack, run away, otherwise go for the target */
			if (definition->flags&_monster_cannot_attack)
			{
				// LP changed: unnecessary to interrupt for this
				// dprintf("%p", monster);
				destination= &bias;
				bias.x= object->location.x - target_object->location.x;
				bias.y= object->location.y - target_object->location.y;
				destination_polygon_index= NONE;
			}
			else
			{
				/* if we still have lock, just build a new path and keep charging */
				destination= &target_object->location;
				destination_polygon_index= MONSTER_IS_PLAYER(target) ?
					get_polygon_index_supporting_player(monster->target_index) :
					target_object->polygon;
			}
			break;
		}
		
		case _monster_lost_lock:
			/* if we lost lock during this path and we went as far as we could go, unlock */
			set_monster_mode(monster_index, _monster_unlocked, NONE);
//			dprintf("monster #%d lost lock and reached end of path;g;", monster_index);
		case _monster_unlocked:
			/* if we’re unlocked and need a new path, follow our guard path if we have one and
				run around randomly if we don’t */
			if ((destination_polygon_index= monster->goal_polygon_index)!=NONE)
			{
				destination= &get_polygon_data(destination_polygon_index)->center;
			}
			else
			{	
				destination= nullptr;
			}
			break;
		
		default:
			assert(false);
			break;
	}

//	dprintf("#%d: generating new %spath for monster #%d;g;", dynamic_world->tick_count, destination?"":"random ", monster_index);

	data.definition= definition;
	data.monster= monster;
	data.cross_zone_boundaries= destination_polygon_index==NONE ? false : true;

	monster->path= new_path(&object->location, object->polygon, destination,
		destination_polygon_index, 3*definition->radius, monster_pathfinding_cost_function, &data);
	if (monster->path==NONE)
	{
		if (monster->action!=_monster_is_being_hit || MONSTER_IS_DYING(monster)) set_monster_action(monster_index, _monster_is_stationary);
        if (sprintathon_visible_ranged_target(monster_index)) {
            auto& state = sprintathon_failed_routes[monster_index];
            state.object = monster->object_index; state.target = monster->target_index;
            state.target_object = get_monster_data(monster->target_index)->object_index;
            state.tick = dynamic_world->tick_count;
            state.retry = state.tick+2*TICKS_PER_SECOND+monster_index%13;
            // Preserve the lock so the normal attack timer can run.
        } else {
            sprintathon_failed_routes.erase(monster_index);
            set_monster_mode(monster_index, _monster_unlocked, NONE);
        }
	}
	else
	{
        sprintathon_failed_routes.erase(monster_index);
		advance_monster_path(monster_index);
	}
}


/* somebody just did damage to us; see if we should start attacking them or not.  berserk
	monsters always switch targets.  this is where we check to see if we go berserk, right?
	monster->vitality has already been changed (a monster who just bumped into another monster
	also calls this, with a delta_vitality of zero).  returns true if an attack was started. */
static bool switch_target_check(
	short monster_index,
	short attacker_index,
	short delta_vitality)
{
	struct monster_data *monster= get_monster_data(monster_index);
	bool switched_target= false;

	if (!MONSTER_IS_PLAYER(monster) && !MONSTER_IS_DYING(monster)) /* don’t mess with players or dying monsters */
	{
		if (MONSTER_HAS_VALID_TARGET(monster) && monster->target_index==attacker_index)
		{
			/* if we didn’t know where our target was and he just shot us, we sort of like, know
				where he is now */
			if (monster->mode==_monster_losing_lock)
			{
				set_monster_mode(monster_index, _monster_locked, attacker_index);
				monster_needs_path(monster_index, false);
			}

			/* if we’re already after this guy and he just did damage to us, remember that */
			if (delta_vitality)
				SET_TARGET_DAMAGE_FLAG(monster);
			
			switched_target= true;
		}
		else
		{
			struct monster_definition *definition= get_monster_definition(monster->type);
			struct monster_data *attacker= get_monster_data(attacker_index);
			
			CLEAR_TARGET_DAMAGE_FLAG(monster);

			if (!MONSTER_IS_DYING(attacker) && !(definition->flags&_monster_cannot_attack))
			{
				/* if our attacker is an enemy (or a neutral doing non-zero damage or we are berserk) and
						a) we’re inactive, or,
						b) idle, or,
						c) unlocked, or,
						d) our current target has not done any damage
					then go kick his ass. */
				if (mTYPE_IS_ENEMY(definition, attacker->type) ||
					(TYPE_IS_NEUTRAL(definition, attacker->type)&&delta_vitality) ||
					MONSTER_IS_BERSERK(monster))
				{
					if (!MONSTER_IS_ACTIVE(monster) ||
						MONSTER_IS_IDLE(monster) ||
						monster->mode!=_monster_locked ||
						!TARGET_HAS_DONE_DAMAGE(monster))
					{
						change_monster_target(monster_index, attacker_index);
						if (delta_vitality) SET_TARGET_DAMAGE_FLAG(monster);
						switched_target= true;
					}
				}
			}
		}
	}
	
	return switched_target;
}

static short get_monster_attitude(
	short monster_index,
	short target_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	struct monster_data *target= get_monster_data(target_index);
	short target_type= target->type;
	short attitude;

	/* berserk monsters are hostile toward everything */
	if (mTYPE_IS_ENEMY(definition, target_type) || MONSTER_IS_BERSERK(monster) ||
		(MONSTER_HAS_VALID_TARGET(monster) && monster->target_index==target_index) ||
		((definition->_class&_class_human_civilian) && MONSTER_IS_PLAYER(target) && dynamic_world->civilians_killed_by_players>=CIVILIANS_KILLED_BY_PLAYER_THRESHHOLD))
	{
		attitude= _hostile;
	}
	else
	{
		attitude= (TYPE_IS_FRIEND(definition, target_type)) ? _friendly : _neutral;
	}

//	if ((definition->_class&_class_human_civilian) && MONSTER_IS_PLAYER(target))
//	{
//		dprintf("#%d vs. #%d ==> #%d", monster_index, target_index, attitude);
//	}
	
	return attitude;
}

static std::map<short, std::set<short> > sprintathon_sweep_hits;

static void sprintathon_play_external_hit_sound(
	short aggressor_index,
	const char *source_path,
	const char *installed_path,
	float gain = 1.f)
{
	if (aggressor_index == NONE ||
		!SLOT_IS_USED(get_monster_data(aggressor_index)))
		return;

	monster_data *aggressor = get_monster_data(aggressor_index);
	if (!MONSTER_IS_PLAYER(aggressor) ||
		monster_index_to_player_index(aggressor_index) != current_player_index)
		return;

	FileSpecifier hit_sound(source_path);
	if (!hit_sound.Exists() &&
		!hit_sound.SetNameWithPath(installed_path))
	{
		hit_sound = FileSpecifier(
			get_data_path(kPathDefaultData) + "/" + installed_path);
	}

	if (hit_sound.Exists())
	{
		SoundParameters parameters;
		parameters.stereo_parameters.is_panning = true;
		parameters.stereo_parameters.gain_global = gain;
		parameters.stereo_parameters.gain_left = gain;
		parameters.stereo_parameters.gain_right = gain;
		SoundManager::instance()->PlayExternalSound(hit_sound, parameters);
	}
}

void sprintathon_play_kick_hit_sound(short aggressor_index)
{
	sprintathon_play_external_hit_sound(
		aggressor_index,
		"snd/kickhit.ogg",
		"Sprintathon/kickhit.ogg");
}

void sprintathon_play_wall_kick_sound(short aggressor_index)
{
	sprintathon_play_external_hit_sound(
		aggressor_index,
		"snd/wallkick.ogg",
		"Sprintathon/wallkick.ogg");
}

void sprintathon_play_footstep_sound(short aggressor_index, bool alternate)
{
    if (current_player && aggressor_index == current_player->monster_index) {
        const auto flags = current_player->variables.flags;
        const bool wading = (flags & _FEET_BELOW_MEDIA_BIT) &&
            !(flags & (_HEAD_BELOW_MEDIA_BIT | _ABOVE_GROUND_BIT));
        if (wading) {
            sprintathon_play_external_hit_sound(aggressor_index,
                alternate ? "snd/wetstep2.ogg" : "snd/wetstep.ogg",
                alternate ? "Sprintathon/wetstep2.ogg" : "Sprintathon/wetstep.ogg",
                input_preferences->sprintathon_footstep_volume_percent / 100.f);
            return;
        }
    }

	sprintathon_play_external_hit_sound(
		aggressor_index,
		alternate ? "snd/footstep2.ogg" : "snd/footstep.ogg",
		alternate ? "Sprintathon/footstep2.ogg" :
			"Sprintathon/footstep.ogg",
		input_preferences->sprintathon_footstep_volume_percent / 100.f);
}

void sprintathon_begin_sweep_attack(short aggressor_index)
{
	sprintathon_sweep_hits[aggressor_index].clear();
}

bool sprintathon_slide_attack(
	short aggressor_index,
	angle facing,
	const world_point3d *origin,
	short origin_polygon_index,
	_fixed damage_scale, bool slide_hit)
{
	const int32 forward_x = cosine_table[facing];
	const int32 forward_y = sine_table[facing];
	bool hit_new_target = false;

	// Standard fist damage at half the former 1.5x slide modifier.
	damage_definition damage = {
		_damage_fist,
		static_cast<int16>(_sprintathon_damage | (slide_hit ? 0 : _sprintathon_kick_damage)),
		56,
		0,
		damage_scale
	};

	for (short target_index = 0;
		target_index < MAXIMUM_MONSTERS_PER_MAP;
		++target_index)
	{
		monster_data *target = monsters + target_index;

		if (!SLOT_IS_USED(target) ||
			target_index == aggressor_index ||
			MONSTER_IS_DYING(target) ||
			sprintathon_sweep_hits[aggressor_index].count(target_index) != 0 ||
			get_monster_attitude(aggressor_index, target_index) != _hostile)
		{
			continue;
		}

		object_data *target_object =
			get_object_data(target->object_index);
		monster_definition *target_definition =
			get_monster_definition(target->type);

		const int32 dx = target_object->location.x - origin->x;
		const int32 dy = target_object->location.y - origin->y;

		const int32 forward_distance =
			(dx * forward_x + dy * forward_y) >> TRIG_SHIFT;
		const int32 sideways_distance =
			(-dx * forward_y + dy * forward_x) >> TRIG_SHIFT;

		// Reach approximately one world unit in front of the player.
		if (forward_distance <= 0 ||
			forward_distance >
				WORLD_ONE + target_definition->radius)
		{
			continue;
		}

		/*
		 * A restrained widening wedge around the crosshair. Target radius is
		 * retained so large monsters do not become artificially hard to hit.
		 */
		const int32 half_width =
			WORLD_ONE / 8 +
			forward_distance / 4 +
			target_definition->radius;

		if (std::abs(sideways_distance) > half_width)
			continue;

		// Require a reasonable vertical overlap.
		const int32 target_middle =
			target_object->location.z +
			target_definition->height / 2;

		if (std::abs(target_middle - origin->z) >
			WORLD_ONE)
		{
			continue;
		}

		// Monsters behind walls are not affected.
		if (line_is_obstructed(
			origin_polygon_index,
			const_cast<world_point3d *>(origin),
			target_object->polygon,
			&target_object->location))
		{
			continue;
		}

		/*
		 * Pass no epicentre to suppress the fist damage type's normal
		 * knockback. We apply a controlled custom impulse below.
		 */
		// Give every newly connected kick its own universal impact sound.
		sprintathon_play_kick_hit_sound(aggressor_index);

		damage_monster(
			target_index,
			aggressor_index,
			get_monster_data(aggressor_index)->type,
			nullptr,
			&damage,
			NONE);

		sprintathon_sweep_hits[aggressor_index].insert(target_index);
		hit_new_target = true;

		// Push directly away from the player: backward in the centre,
		// and increasingly sideways for targets near either edge.
		if (SLOT_IS_USED(target))
		{
			const angle push_direction = arctangent(dx, dy);

			accelerate_monster(
				target_index,
				slide_hit ? 115 : 85,
				push_direction,
				30);
		}
	}

	return hit_new_target;
}

/* find_closest_appropriate_target() tries to do just that.  it is a little broken in that it
	treats all monsters in a given polygon as if they were the same distance away, which could
	result in strange behavior.  the assumption is that if there is a more accessable hostile monster
	nearby, that monster will attack and thus end a possible wild goose chase.  if there is a
	closer hostile target which the aggressor subsequently attempts to move through, he will
	change lock and attack the obstruction instead, which will help minimize weirdness.
	full_circle is passed directly to clear_line_of_sight(). */
short find_closest_appropriate_target(
	short aggressor_index,
	bool full_circle)
{
	struct monster_data *aggressor= get_monster_data(aggressor_index);
	struct monster_definition *definition= get_monster_definition(aggressor->type);
	short closest_hostile_target_index= NONE;
	
	if (MONSTER_IS_ACTIVE(aggressor))
	{
		int32 flood_flags= _pass_one_zone_border;
		short polygon_index= get_object_data(get_monster_data(aggressor_index)->object_index)->polygon;
		
		/* flood out from the aggressor monster’s polygon, searching through the object lists of all
			polygons we encounter */
		polygon_index= flood_map(polygon_index, INT32_MAX, monster_activation_flood_proc, _flagged_breadth_first, &flood_flags);
		while (polygon_index!=NONE && closest_hostile_target_index==NONE)
		{
			short object_index;
			struct object_data *object;
	
			/* loop through all objects in this polygon looking for hostile monsters we can see */
			for (object_index= get_polygon_data(polygon_index)->first_object; object_index!=NONE; object_index= object->next_object)
			{
				object= get_object_data(object_index);
				if (GET_OBJECT_OWNER(object)==_object_is_monster && OBJECT_IS_VISIBLE(object))
				{
					short target_monster_index= object->permutation;
					struct monster_data *target_monster= get_monster_data(target_monster_index);
	
					if (!MONSTER_IS_DYING(target_monster) && target_monster_index!=aggressor_index)
					{
						if (get_monster_attitude(aggressor_index, target_monster_index)==_hostile)
						{
							if (((definition->flags&_monster_is_omniscent) || clear_line_of_sight(aggressor_index, target_monster_index, full_circle)) &&
								(MONSTER_IS_ACTIVE(target_monster) || MONSTER_IS_PLAYER(target_monster) || (static_world->environment_flags&_environment_rebellion)))
							{
								/* found hostile, live, visible monster */
								closest_hostile_target_index= target_monster_index;
								break;
							}
						}
					}
				}
			}
			
			polygon_index= flood_map(NONE, INT32_MAX, monster_activation_flood_proc, _flagged_breadth_first, &flood_flags);
		}
	}
	else
	{
		short player_index;
		
		/* if this monster is deactivated, only seeing a player will activate him */
		
		for (player_index= 0; player_index<dynamic_world->player_count; ++player_index)
		{
			struct player_data *player= get_player_data(player_index);
			
			if (get_monster_attitude(aggressor_index, player->monster_index)==_hostile &&
				clear_line_of_sight(aggressor_index, player->monster_index, full_circle))
			{
				closest_hostile_target_index= player->monster_index;
				
				break;
			}
		}
	}

	return closest_hostile_target_index;
}

/* if ‘full_circle’ is true, the monster can see in all directions.  if ‘full_circle’ is false
	the monster respects his visual_arc and current facing.  clear_line_of_sight() is implemented
	wholly in 2D and only attempts to connect the centers of the two monsters by a line. */
static bool clear_line_of_sight(
	short viewer_index,
	short target_index,
	bool full_circle)
{
	struct monster_data *viewer= get_monster_data(viewer_index);
	struct object_data *viewer_object= get_object_data(viewer->object_index);
	struct monster_definition *viewer_definition= get_monster_definition(viewer->type);
	struct monster_data *target= get_monster_data(target_index);
	struct object_data *target_object= get_object_data(target->object_index);
	bool target_visible= true;
	
	{
		world_point3d *origin= &viewer_object->location;
		world_point3d *destination= &target_object->location;
		// LP change: made this long-distance friendly
		int32 dx= int32(destination->x)-int32(origin->x);
		int32 dy= int32(destination->y)-int32(origin->y);
		world_distance dz= destination->z-origin->z;
		int32 distance2d= GUESS_HYPOTENUSE(std::abs(dx), std::abs(dy));

		/* if we can’t see full circle, make sure the target is in our visual arc */
		if (!full_circle)
		{
			angle theta= arctangent(dx, dy)-viewer_object->facing;
			angle phi= arctangent(distance2d, std::abs(dz));
			
			if (std::abs(theta)>viewer_definition->half_visual_arc) target_visible= false;
			if (phi>=viewer_definition->half_vertical_visual_arc&&phi<FULL_CIRCLE-viewer_definition->half_vertical_visual_arc) target_visible= false;
		}

		/* we can’t see some transfer modes */
		switch (target_object->transfer_mode)
		{
			case _xfer_invisibility:
			case _xfer_subtle_invisibility:
				if (distance2d>viewer_definition->dark_visual_range) target_visible= false;
				break;
		}
		
		/* make sure the target is within our visual_range (taking any of his active
			effects, i.e. invisibility, into account) and that he isn’t standing in a
			dark polygon beyond our dark_visual_range. */
		if (target_visible)
		{
			if (distance2d>viewer_definition->visual_range) // || (distance2d>viewer_definition->dark_visual_range&&get_object_light_intensity(target->object_index)<=LOW_LIGHT_INTENSITY))
			{
				target_visible= false;
			}
		}

		/* make sure there are no non-transparent lines between the viewer and the target */
		if (target_visible)
		{
			short polygon_index= viewer_object->polygon;
			short line_index;
			
			do
			{
				line_index= find_line_crossed_leaving_polygon(polygon_index, origin, destination);
				if (line_index!=NONE)
				{
					if (LINE_IS_TRANSPARENT(get_line_data(line_index)))
					{
						/* transparent line, find adjacent polygon */
						polygon_index= find_adjacent_polygon(polygon_index, line_index);
						// LP change: make no polygon act like a non-transparent line
						if (polygon_index == NONE) target_visible= false;
					}
					else
					{
						/* non-transparent line, target not visible */
						target_visible= false;
					}
				}
				else
				{
					/* we got to the target’s (x,y) location, but we’re in a different polygon;
						he’s invisible */
					if (polygon_index!=target_object->polygon) target_visible= false;
				}
			}
			while (target_visible&&line_index!=NONE);
		}
	}
	
	return target_visible;
}

/* lock the given monster onto the given target, playing a locking sound if the monster
	previously didn’t have a lock */
void change_monster_target(
	short monster_index,
	short target_index)
{
	/* locking on ourselves would be cool, but ... */
	if (monster_index!=target_index)
	{
		struct monster_data *monster= get_monster_data(monster_index);
		struct monster_definition *definition= get_monster_definition(monster->type);
	
		if (target_index!=NONE)
		{
			/* only active monsters can have lock, so activate inactive monsters */
			if (!MONSTER_IS_ACTIVE(monster)) activate_monster(monster_index);
		
			/* play activation sounds (including activating on a friendly) */
			if (monster->target_index!=target_index && TYPE_IS_FRIEND(definition, get_monster_data(target_index)->type))
			{
				play_object_sound(monster->object_index, definition->friendly_activation_sound);
			}
			else if (!(definition->flags & _monster_makes_sound_when_activated))
			{
				if (monster->mode==_monster_unlocked) play_object_sound(monster->object_index, definition->activation_sound);
			}
			
			/* instantiate the new target and ask for a new path */
			if (MONSTER_HAS_VALID_TARGET(monster) && target_index!=monster->target_index) CLEAR_TARGET_DAMAGE_FLAG(monster);
			monster_needs_path(monster_index, false);
			set_monster_mode(monster_index, _monster_locked, target_index);
		}
		else
		{
			if (MONSTER_IS_ACTIVE(monster))
			{
				/* no target, if we’re not unlocked mark us as unlocked and ask for a new path */
				if (monster->mode!=_monster_unlocked)
				{
//					dprintf("monster #%d was locked on NONE;g;", monster_index);
		
					set_monster_mode(monster_index, _monster_unlocked, NONE);
					monster_needs_path(monster_index, false);
				}
			}
		}
	}
}

static void handle_moving_or_stationary_monster(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	struct monster_definition *definition= get_monster_definition(monster->type);

    bool flock_moved = false;
    const bool melee_hold = sprintathon_hold_melee(monster_index);
    const bool route_hold = sprintathon_wait_for_route(monster_index);
    if (route_hold) {
        const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
        object->facing = arctangent(int(target->location.x)-object->location.x,
            int(target->location.y)-object->location.y);
    }
    const bool flock_controlled = melee_hold || route_hold || sprintathon_flock_move(monster_index, &flock_moved);
    // Reconsider a failed route after the cooldown, even if no shot was fired.
    if (!route_hold && sprintathon_failed_routes.find(monster_index) != sprintathon_failed_routes.end())
        monster_needs_path(monster_index, false);

    const auto dodge_it = sprintathon_clear_shots.find(monster_index);
    if (sprintathon_small_flyer(monster, definition) && dodge_it != sprintathon_clear_shots.end()) {
        const auto& dodge = dodge_it->second;
        const int now = dynamic_world->tick_count;
        if (dodge.object == monster->object_index && now >= dodge.requested &&
            now-dodge.requested < 2*TICKS_PER_SECOND &&
            (dodge.start < 0 || now-dodge.start < sprintathon_dodge_ticks)) {
            // Waiting-to-attack normally skips movement. A queued dodge must
            // leave that state and complete before attempting another volley.
            set_monster_action(monster_index, _monster_is_moving);
            translate_monster(monster_index, definition->speed);
            return;
        }
    }

	if (monster->path==NONE && monster->mode!=_monster_locked && monster->action==_monster_is_stationary)
	{
		/* stationary, unlocked monsters without paths cannot move */
		monster_needs_path(monster_index, false);
	}
	else
	{
		world_distance distance_moved= definition->speed;
		
		/* base speed on difficulty level (for aliens) and berserk status */
		if (definition->flags&_monster_is_alien)
		{
			switch (dynamic_world->game_information.difficulty_level)
			{
				case _wuss_level: distance_moved-= distance_moved>>3; break;
				case _easy_level: distance_moved-= distance_moved>>4; break;
				case _major_damage_level: distance_moved+= distance_moved>>3; break;
				case _total_carnage_level: distance_moved+= distance_moved>>2; break;
			}
		}
		if (MONSTER_IS_BERSERK(monster)) distance_moved+= (distance_moved>>1);
        if (sprintathon_organic_flight(monster, definition))
            distance_moved = std::max(1, int(distance_moved *
                (0.90 + 0.10*sprintathon_flight_wave(monster_index, 4*TICKS_PER_SECOND, 29))));
		
        if (flock_controlled)
        {
            // Movement never returns before the normal attack cadence below.
            if (monster->action != _monster_is_waiting_to_attack_again) {
                const short action = flock_moved ? _monster_is_moving : _monster_is_stationary;
                if (monster->action != action) set_monster_action(monster_index, action);
            }
            if (!flock_moved) monster->ticks_since_attack+= 1;
        }
        else if (monster->action!=_monster_is_waiting_to_attack_again)
		{
			if (translate_monster(monster_index, distance_moved))
			{
				/* we moved: _monster_is_stationary becomes _monster_is_moving */
				if (monster->action==_monster_is_stationary) set_monster_action(monster_index, _monster_is_moving);
			}
			else
			{
				/* we couldn’t move: _monster_is_moving becomes _monster_is_stationary */
				if (monster->action==_monster_is_moving) set_monster_action(monster_index, _monster_is_stationary);
				monster->ticks_since_attack+= 1; /* attacks occur twice as frequently if we can’t move (damnit!) */
			}
		}
		else
		{
			monster->ticks_since_attack+= 1;
		}
	
		/* whether we moved or not, see if we can attack if we have lock */
		monster->ticks_since_attack+= MONSTER_IS_BERSERK(monster) ? 3 : 1;
		if (OBJECT_WAS_ANIMATED(object) && monster->mode==_monster_locked)
		{
			short attack_frequency= definition->attack_frequency;
			
			if (definition->flags&_monster_is_alien)
			{
				switch (dynamic_world->game_information.difficulty_level)
				{
					case _wuss_level: attack_frequency= 3*attack_frequency; break;
					case _easy_level: attack_frequency= 2*attack_frequency; break;
					case _major_damage_level: attack_frequency= attack_frequency>>1; break;
					case _total_carnage_level: attack_frequency= attack_frequency>>2; break;
				}
			}

			if (monster->ticks_since_attack>attack_frequency)
			{
				if (try_monster_attack(monster_index))
				{
					/* activate with lock nearby monsters on our target */
					activate_nearby_monsters(monster->target_index, monster_index, _pass_one_zone_border, MONSTER_ALERT_ACTIVATION_RANGE);
				}
				else
				{
					if (monster->action==_monster_is_waiting_to_attack_again)
					{
						set_monster_action(monster_index, _monster_is_stationary);
						monster_needs_path(monster_index, true);
					}
				}
			}
		}
	}
}

void set_monster_action(
	short monster_index,
	short action)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	shape_descriptor shape;

	/* what shape should we use? */
	if (action==_monster_is_dying_flaming)
	{
		shape= FLAMING_DYING_SHAPE;
	}
	else
	{
		switch (action)
		{
			case _monster_is_waiting_to_attack_again:
			case _monster_is_stationary: shape= definition->stationary_shape; break;
			case _monster_is_moving: shape= definition->moving_shape; break;
			case _monster_is_attacking_close: shape= definition->melee_attack.attack_shape; break;
			case _monster_is_attacking_far: shape= definition->ranged_attack.attack_shape; break;
			case _monster_is_being_hit: shape= definition->hit_shapes; break;
			case _monster_is_dying_hard: shape= definition->hard_dying_shape; break;
			case _monster_is_dying_soft: shape= definition->soft_dying_shape; break;
			case _monster_is_teleporting_in: shape= definition->teleport_in_shape; break;
			case _monster_is_teleporting_out: shape= definition->teleport_out_shape; break;
			default: dprintf("what is monster action #%d?", action); assert(false); break;
		}
		
		shape= shape==UNONE ? UNONE : BUILD_DESCRIPTOR(definition->collection, shape);
	}

	if (shape!=UNONE)
	{
		/* only set the action of the shape is not UNONE */
		monster->action= action;
		set_object_shape_and_transfer_mode(monster->object_index, shape, NONE);

		/* if this monster does shrapnel damage, do it */
		if (action == _monster_is_dying_hard)
		{
			if (definition->flags & _monster_has_delayed_hard_death)
			{
				cause_shrapnel_damage(monster_index);
			}
			else if (film_profile.key_frame_zero_shrapnel_fix)
			{
				object_data* object = get_object_data(monster->object_index);
				shape_animation_data* animation = get_shape_animation_data(object->shape);
				if (animation && animation->key_frame == 0)
				{
					cause_shrapnel_damage(monster_index);
				}
			}
		}
		
		if ((definition->flags&_monster_has_nuclear_hard_death) && action==_monster_is_dying_hard)
		{
			start_fade(_fade_long_bright);
			SoundManager::instance()->PlaySound(Sound_Exploding(), nullptr, NONE);
		}
	}
}

/* do whatever needs to be done when this monster dies and remove it from the monster list */
static void kill_monster(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	struct object_data *object= get_object_data(monster->object_index);
	shape_descriptor shape;
	
	switch (monster->action)
	{
		case _monster_is_dying_soft:
			shape= definition->soft_dead_shapes==UNONE ? UNONE : BUILD_DESCRIPTOR(definition->collection, definition->soft_dead_shapes);
			break;
		case _monster_is_dying_hard:
			shape= definition->hard_dead_shapes==UNONE ? UNONE : BUILD_DESCRIPTOR(definition->collection, definition->hard_dead_shapes);
			break;
		case _monster_is_dying_flaming:
			shape= FLAMING_DEAD_SHAPE;
			break;
		
		default:
			assert(false);
			break;
	}

	/* add an item if we’re supposed to be carrying something */
	if (definition->carrying_item_type!=NONE && monster->action==_monster_is_dying_soft)
	{
		world_distance radius, height;
		world_point3d random_point;
		short random_polygon_index;
		
		get_monster_dimensions(monster_index, &radius, &height);
		random_point_on_circle(&object->location, object->polygon, radius, &random_point, &random_polygon_index);
		if (random_polygon_index!=NONE)
		{
			struct polygon_data *random_polygon= get_polygon_data(random_polygon_index);
			struct object_location location;
			
			switch (random_polygon->type)
			{
				case _polygon_is_platform:
				case _polygon_is_item_impassable:
				case _polygon_is_monster_impassable:
				case _polygon_is_teleporter:
					break;
				
				default:
					location.polygon_index= random_polygon_index;
					location.p.x= random_point.x, location.p.y= random_point.y, location.p.z= 0;
					location.yaw= 0;
					location.flags= 0;
					new_item(&location, definition->carrying_item_type);
					break;
			}
		}
	}
	
	/* stuff in an appropriate dead shape (or remove our object if we don’t have a dead shape) */
    bool remove_object = (shape == UNONE);
    if (!remove_object && (static_world->environment_flags & _environment_ouch_m1))
    {
        struct polygon_data *polygon = get_polygon_data(object->polygon);
        switch (polygon->type)
        {
            case _polygon_is_major_ouch:
            case _polygon_is_minor_ouch:
                remove_object = true;
                break;
            case _polygon_is_platform:
                if (PLATFORM_IS_FLOODED(get_platform_data(polygon->permutation)) &&
                    find_flooding_polygon(object->polygon) != NONE)
                    remove_object = true;
                break;
        }
    }
    
	if (remove_object)
	{
		remove_map_object(monster->object_index);
	}
	else
	{
		turn_object_to_shit(monster->object_index);
		sprintathon_register_corpse(monster->object_index, monster->type);
		randomize_object_sequence(monster->object_index, shape);
	}

	/* recover original type and notify the object stuff a monster died */
	if (monster->flags&_monster_was_promoted) monster->type-= 1;
	if (monster->flags&_monster_was_demoted) monster->type+= 1;
	object_was_just_destroyed(_object_is_monster, monster->type);

	L_Invalidate_Monster(monster_index);
	MARK_SLOT_AS_FREE(monster);
}
		
/* move the monster along his current heading; if he reaches the center of his destination square,
	then point him at the next square and send him off.  this used to chuck if the monster moved
	too far during a certain turn (which was completely possible when the player was wearing the
	red cloak in Pathways), but that was fixed.  i just recoded this for marathon and it looks
	a hell of a lot better now. */
static bool translate_monster(
	short monster_index,
	world_distance distance)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	world_point3d new_location;
	short obstacle_index;
	bool legal_move= false;

    angle route_facing = object->facing;
    double approach_turn = 0;
    const auto route = sprintathon_curve_waypoints.find(monster_index);
    if (input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
        !game_is_networked && !game_is_being_replayed() &&
        !MONSTER_IS_DYING(monster) && !MONSTER_IS_ATTACKING(monster) &&
        monster->action != _monster_is_being_hit && !monster->external_velocity &&
        route != sprintathon_curve_waypoints.end() && route->second.object == monster->object_index) {
        world_point2d goal = route->second.point;
        // Refresh a direct pursuit goal as the target moves within this room.
        if (monster->path == NONE && MONSTER_HAS_VALID_TARGET(monster)) {
            const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
            if (target->polygon == object->polygon) {
                goal.x = target->location.x; goal.y = target->location.y;
            }
        }
        route_facing = arctangent(int(goal.x)-object->location.x, int(goal.y)-object->location.y);
        const int remaining = distance2d(&goal, &object->location);
        // Account for real progress toward the waypoint, not distance travelled
        // sideways along the arc. This prevents early waypoint completion.
        monster->path_segment_length = remaining;
        approach_turn = sprintathon_approach_curve(monster_index, remaining, route_facing);
    }
    const angle approach_facing = NORMALIZE_ANGLE(route_facing+
        int(approach_turn*FULL_CIRCLE/6.283185307179586));
    angle travel_facing = approach_facing;
    float flight_sway = 0;
    if (sprintathon_organic_flight(monster, definition)) {
        const auto* room = get_polygon_data(object->polygon);
        const int space = std::max(0, int(room->ceiling_height)-room->floor_height-definition->height);
        const double openness = std::min(1.0, double(space)/WORLD_ONE);
        flight_sway = float(0.32*openness*sprintathon_flight_wave(monster_index, 5*TICKS_PER_SECOND, 71));
        auto dodge_it = sprintathon_clear_shots.find(monster_index);
        if (sprintathon_small_flyer(monster, definition) && dodge_it != sprintathon_clear_shots.end()) {
            auto& dodge = dodge_it->second;
            const int now = dynamic_world->tick_count;
            if (dodge.object == monster->object_index && now >= dodge.requested && now-dodge.requested < 2*TICKS_PER_SECOND) {
                if (dodge.start < 0) dodge.start = now;
                const int duration = sprintathon_dodge_ticks;
                const int elapsed = now-dodge.start;
                if (elapsed >= 0 && elapsed < duration) {
                    const float pulse = float(std::sin(3.141592653589793*(elapsed+1)/(duration+1)));
                    const angle sideways = NORMALIZE_ANGLE(object->facing+dodge.direction*QUARTER_CIRCLE);
                    const world_distance step = std::max(1, int(std::min(float(WORLD_ONE)/8,
                        std::max(float(WORLD_ONE)/24, float(distance)*2.5f))*pulse));
                    const world_point3d before = object->location;
                    world_point3d next = before;
                    translate_point2d(&next, step, sideways);
                    world_distance floor, ceiling;
                    short supporting;
                    keep_line_segment_out_of_walls(object->polygon, &object->location, &next,
                        definition->radius, definition->height, &floor, &ceiling, &supporting);
                    if (next.z < floor || int(next.z)+definition->height > ceiling ||
                        legal_monster_move(monster_index, sideways, &next) != NONE) return false;
                    const short old_polygon = object->polygon;
                    if (translate_map_object(monster->object_index, &next, NONE)) monster_moved(monster_index, old_polygon);
                    dodge.motion[0] = float(object->location.x)-before.x;
                    dodge.motion[1] = float(object->location.y)-before.y;
                    dodge.motion[2] = float(object->location.z)-before.z;
                    dodge.motion_tick = now;
                    const float moved = std::sqrt(dodge.motion[0]*dodge.motion[0]+dodge.motion[1]*dodge.motion[1]);
                    if (moved < 1) return false;
                    dodge.bank_axis_x = dodge.motion[0]/moved;
                    dodge.bank_axis_y = dodge.motion[1]/moved;
                    // Lateral dodging does not consume forward path distance or turn the sprite.
                    return true;
                }
            }
        }
        travel_facing = NORMALIZE_ANGLE(approach_facing + int(std::atan(flight_sway)*FULL_CIRCLE/6.283185307179586));
    }
    if (sprintathon_soft_float(monster, definition)) {
        auto it = sprintathon_float_motion.find(monster_index);
        if (it != sprintathon_float_motion.end()) {
            auto& motion = it->second;
            motion.request_tick = dynamic_world->tick_count;
            // Tiny broad sway, and shortest-arc heading response across north.
            const bool climbing = dynamic_world->tick_count < motion.climb_until;
            const double target = approach_facing + (climbing ? 0.0 : 0.025*FULL_CIRCLE*
                sprintathon_flight_wave(monster_index, 7*TICKS_PER_SECOND, 113));
            double turn = std::fmod(target-motion.heading+1.5*FULL_CIRCLE, double(FULL_CIRCLE))-HALF_CIRCLE;
            motion.heading = std::fmod(motion.heading+turn*(climbing ? 0.20 : 0.10)+FULL_CIRCLE, double(FULL_CIRCLE));
            motion.speed += (distance-motion.speed)*0.12;
            distance = world_distance(std::max(1.0, motion.speed));
            travel_facing = NORMALIZE_ANGLE(int(motion.heading));
        }
    }
    new_location = object->location;
    translate_point2d(&new_location, distance, travel_facing);
    if (sprintathon_soft_float(monster, definition)) {
        world_distance floor, ceiling;
        short supporting;
        keep_line_segment_out_of_walls(object->polygon, &object->location, &new_location,
            0, definition->height, &floor, &ceiling, &supporting);
        if (new_location.z < floor || int(new_location.z)+definition->height > ceiling) {
            sprintathon_float_motion[monster_index].speed *= 0.5;
            // Run the original direction below so the terrain probe can request
            // the height needed to climb a ledge.
            new_location = object->location;
            travel_facing = route_facing;
            translate_point2d(&new_location, distance, travel_facing);
        }
    }
    obstacle_index = legal_monster_move(monster_index, travel_facing, &new_location);
    // If the weave hits an obstacle, retry the original AI direction.
    if (obstacle_index != NONE && travel_facing != object->facing) {
        flight_sway = 0;
        new_location = object->location;
        translate_point2d(&new_location, distance, route_facing);
        obstacle_index = legal_monster_move(monster_index, route_facing, &new_location);
    }
    if (sprintathon_ground_ai(monster, definition)) {
        world_distance body_floor = INT16_MIN;
        const short body = legal_player_move(monster_index, &new_location, &body_floor);
        if (body != NONE && GET_OBJECT_OWNER(get_object_data(body)) == _object_is_monster)
            obstacle_index = body;
    }
    /* Retain the normal terrain, platform and object movement checks. */
    if (obstacle_index == NONE)
	{
		/* legal move: see if there is a platform that we have to open or wait for,
			if not move, if so, wait */
		
		short feature_index;
		short relevant_polygon_index;

		legal_move= true;
		switch (find_obstructing_terrain_feature(monster_index, &feature_index, &relevant_polygon_index))
		{
			case _entering_platform_polygon:
				switch (monster_can_enter_platform(feature_index, relevant_polygon_index, definition->height, definition->minimum_ledge_delta, definition->maximum_ledge_delta))
				{
					case _platform_will_never_be_accessable:
						monster_needs_path(monster_index, true);
						break;
						
					case _platform_will_be_accessable:
						/* we avoid vidding the door by only trying to open it every door_retry_mask+1 ticks */
						if (!(dynamic_world->tick_count&definition->door_retry_mask)) try_and_change_platform_state(feature_index, true);
						SET_MONSTER_IDLE_STATUS(monster, true);
						legal_move= false;
						break;
					
					/* _platform_is_accessable */
				}
				break;
				
			case _leaving_platform_polygon:
				switch (monster_can_leave_platform(feature_index, relevant_polygon_index, definition->height, definition->minimum_ledge_delta, definition->maximum_ledge_delta))
				{
					case _exit_will_never_be_accessable:
						monster_needs_path(monster_index, true);
						break;
					
					case _exit_will_be_accessable:
						SET_MONSTER_IDLE_STATUS(monster, true);
						legal_move= false;
						break;
					
					/* _exit_is_accessable, ignored */
				}
				break;

			case _flying_or_floating_transition:
				/* there is a wall in our way which we have to rise (or fall) along, so don’t
					go anywhere unless we’re over it (or under it) */
				if (std::abs(object->location.z-monster->desired_height)>MINIMUM_FLOATING_HEIGHT) legal_move= false;
				break;
			
			case _standing_on_sniper_ledge:
				/* we’ve been told to freeze on a sniper ledge (no saving throw) */
				legal_move= false;
				break;
		}
		
		if (legal_move)
		{
			if ((monster->path_segment_length-= distance)<=0)
			{
				advance_monster_path(monster_index);
			}
			else
			{
				short old_polygon_index= object->polygon;
				
				/* update the monster’s object to reflect his new position */
				if (translate_map_object(monster->object_index, &new_location, NONE)) monster_moved(monster_index, old_polygon_index);
			}
	
			legal_move= true;
		}
	}
	else
	{
		struct object_data *obstacle_object= get_object_data(obstacle_index);
		
		if (GET_OBJECT_OWNER(obstacle_object)==_object_is_monster)
		{
			struct monster_data *obstacle_monster= get_monster_data(obstacle_object->permutation);
            sprintathon_queue_body_push(monster_index,obstacle_object->permutation,
                double(new_location.x)-object->location.x,
                double(new_location.y)-object->location.y,0);

	
			/* we collided with another monster: see if we want to attack him; if not, see if we
				can attack his current target (if he is locked or losing_lock); if not, drop lock
				and ask for a new path. */
			
			if (!mTYPE_IS_ENEMY(definition, obstacle_monster->type) && !(MONSTER_HAS_VALID_TARGET(monster)&&monster->target_index==obstacle_object->permutation) &&
				!MONSTER_IS_BERSERK(monster))
			{
                if (sprintathon_ground_step(monster_index, distance, false, &new_location)) return true;
                // A friendly body is a cylinder to pass, not a reason to pick
                // a random facing. The destination attempt above only tries
                // forward motion; explicitly try the stable side preference.
                if (!MONSTER_IS_PLAYER(obstacle_monster) &&
                    sprintathon_ground_ai(monster, definition) &&
                    !MONSTER_IS_ATTACKING(monster) && !monster->external_velocity &&
                    !monster->vertical_velocity && monster->action != _monster_is_being_hit &&
                    std::abs(int(object->location.z)-
                        get_polygon_data(object->polygon)->floor_height) <= WORLD_ONE/16) {
                    if (sprintathon_ground_step(monster_index, distance, false)) return true;
                    auto& steering = sprintathon_ground_state(monster_index);
                    if (dynamic_world->tick_count >= steering.body_repath) {
                        monster_needs_path(monster_index, false);
                        steering.body_repath = dynamic_world->tick_count+TICKS_PER_SECOND/2;
                    }
                    SET_MONSTER_IDLE_STATUS(monster, true);
                    return false;
                }
				if (!MONSTER_IS_PLAYER(obstacle_monster))
				{
					if (monster->mode!=_monster_locked)
					{
						if (!MONSTER_HAS_VALID_TARGET(obstacle_monster) || !switch_target_check(monster_index, obstacle_monster->target_index, 0))
						{
							if (monster->mode==_monster_unlocked && !(global_random()&OBSTRUCTION_DEACTIVATION_MASK) &&
								(monster->goal_polygon_index==NONE || monster->goal_polygon_index==object->polygon))
							{
								deactivate_monster(monster_index);
							}
							else
							{
								monster_needs_path(monster_index, false);
								if (monster->mode!=_monster_locked)
								{
									/* if we’re not locked, we might want to think about deactivating here, but
										for now we just build a new random path by forcing our state to _unlocked. */
									set_monster_mode(monster_index, _monster_unlocked, NONE);
	//								dprintf("monster #%d going unlocked by obstruction;g;", monster_index);
								}
							}
						}
					}
					else
					{
						attempt_evasive_manouvers(monster_index);
					}
				}
	
				SET_MONSTER_IDLE_STATUS(monster, true);
			}
			else
			{
				struct monster_definition *obstacle_definition= get_monster_definition(obstacle_monster->type);
				world_distance key_height= obstacle_object->location.z+(obstacle_definition->height>>1);
				
				change_monster_target(monster_index, obstacle_object->permutation);
				
				/* if we’re a kamakazi and we’re within range, pop */
				if ((definition->flags&_monster_is_kamakazi) &&
					object->location.z<key_height)
				{
					bool in_range = object->location.z+definition->height>key_height;
					
					/* if we're short and can't float, take out their knees! */
					if (!in_range && film_profile.allow_short_kamikaze && !(definition->flags&_monster_floats))
						in_range = object->location.z>=obstacle_object->location.z;
					
					if (in_range)
					{
						set_monster_action(monster_index, _monster_is_dying_hard);
						monster_died(monster_index);
						L_Call_Monster_Kamikazed(monster_index);
					}
				}
				
				/* if we float and this is our target, go up */
				if (definition->flags&_monster_floats)
				{
					monster->desired_height= obstacle_object->location.z;
				}
			}
		}
		else
		{
			attempt_evasive_manouvers(monster_index); // to avoid the scenery
		}
	}
	
    if (!legal_move && sprintathon_soft_float(monster, definition))
        sprintathon_float_motion[monster_index].speed *= 0.5;
    if (legal_move && flight_sway != 0) {
        auto& bank = sprintathon_flight_banks[monster_index];
        const float yaw = object->facing * (6.283185307179586f/FULL_CIRCLE);
        // Six degrees at full sway, expressed in world axes for each viewer.
        const float tilt = std::max(-6.0f, std::min(6.0f, -6.0f*flight_sway/0.32f));
        bank.target_x = -std::sin(yaw)*tilt;
        bank.target_y = std::cos(yaw)*tilt;
    }
	return legal_move;
}

static bool attempt_evasive_manouvers(
	short monster_index)
{
    const auto contact = sprintathon_contact_facing.find(monster_index);
    if (contact != sprintathon_contact_facing.end() &&
        contact->second.tick == dynamic_world->tick_count && contact->second.touching)
        return false;

	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	world_point2d destination= { object->location.x, object->location.y };
	angle new_facing= NORMALIZE_ANGLE(object->facing + ((global_random()&1) ? QUARTER_CIRCLE : -QUARTER_CIRCLE));
	world_distance original_floor_height= get_polygon_data(object->polygon)->floor_height;
	short polygon_index= object->polygon;
	bool successful= true;
	
	translate_point2d(&destination, EVASIVE_MANOUVER_DISTANCE, new_facing);
	do
	{
		short line_index= find_line_crossed_leaving_polygon(polygon_index, &object->location, &destination);
		
		if (line_index==NONE)
		{
			polygon_index= NONE;
		}
		else
		{
			/* if we ran off the map, we failed */
			if (LINE_IS_SOLID(get_line_data(line_index)) || (polygon_index= find_adjacent_polygon(polygon_index, line_index))==NONE)
			{
				polygon_index= NONE;
				successful= false;
			}
			else
			{
				struct polygon_data *polygon= get_polygon_data(polygon_index);
				if (polygon->floor_height!=original_floor_height || polygon->type==_polygon_is_monster_impassable)
				{
					polygon_index= NONE;
					successful= false;
				}
			}
		}
	}
	while (polygon_index!=NONE);
	
	if (successful)
	{
		object->facing= new_facing;
		if (monster->path!=NONE) delete_path(monster->path), monster->path= NONE;
		monster->path_segment_length= EVASIVE_MANOUVER_DISTANCE;
	}
	
	return successful;
}

// A shortcut is a swept cylinder through connected polygons, not a sight ray.
static double sprintathon_point_segment_distance2(double px, double py,
    double ax, double ay, double bx, double by)
{
    const double dx = bx-ax, dy = by-ay;
    const double length2 = dx*dx+dy*dy;
    const double t = length2 > 0 ? std::max(0.0, std::min(1.0,
        ((px-ax)*dx+(py-ay)*dy)/length2)) : 0;
    const double x = px-ax-t*dx, y = py-ay-t*dy;
    return x*x+y*y;
}
static double sprintathon_segment_clearance2(const world_point2d& a, const world_point2d& b,
    const world_point2d& c, const world_point2d& d)
{
    const double rx = double(b.x)-a.x, ry = double(b.y)-a.y;
    const double sx = double(d.x)-c.x, sy = double(d.y)-c.y;
    const double qx = double(c.x)-a.x, qy = double(c.y)-a.y;
    const double cross = rx*sy-ry*sx;
    if (std::abs(cross) > 0.0001) {
        const double t = (qx*sy-qy*sx)/cross, u = (qx*ry-qy*rx)/cross;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0;
    }
    return std::min(std::min(
        sprintathon_point_segment_distance2(a.x,a.y,c.x,c.y,d.x,d.y),
        sprintathon_point_segment_distance2(b.x,b.y,c.x,c.y,d.x,d.y)), std::min(
        sprintathon_point_segment_distance2(c.x,c.y,a.x,a.y,b.x,b.y),
        sprintathon_point_segment_distance2(d.x,d.y,a.x,a.y,b.x,b.y)));
}
static bool sprintathon_shortcut_room(const polygon_data* room)
{
    switch (room->type) {
        case _polygon_is_platform: case _polygon_is_zone_border:
        case _polygon_is_monster_impassable: case _polygon_is_teleporter:
        case _polygon_is_minor_ouch: case _polygon_is_major_ouch: return false;
        default: return true;
    }
}
struct SprintathonShortcutContext {
    object_data* object;
    monster_definition* definition;
    world_distance radius, height;
};
static bool sprintathon_shortcut_portal(short from, short edge,
    const SprintathonShortcutContext& context)
{
    const auto* line = get_line_data(edge);
    if (LINE_IS_SOLID(line)) return false;
    const short adjacent = find_adjacent_polygon(from, edge);
    if (adjacent == NONE || adjacent == from) return false;
    const auto* a = get_polygon_data(from);
    const auto* b = get_polygon_data(adjacent);
    if (!sprintathon_shortcut_room(b) || b->media_index != a->media_index) return false;
    const int bottom = std::max(a->floor_height,b->floor_height);
    const int top = std::min(a->ceiling_height,b->ceiling_height);
    if (top-bottom < context.height || line->length < 2*context.radius) return false;
    if (context.definition->flags & (_monster_flys | _monster_floats)) {
        // Vertical manoeuvres retain the original waypoints and terrain probes.
        return context.object->location.z >= bottom &&
            int(context.object->location.z)+context.height <= top;
    }
    const int rise = int(b->floor_height)-a->floor_height;
    return rise >= std::max<int>(-WORLD_ONE/4, context.definition->minimum_ledge_delta) &&
        rise <= std::min<int>(WORLD_ONE/4, context.definition->maximum_ledge_delta);
}
static bool sprintathon_clear_shortcut(const world_point2d* goal,
    short expected_before, short expected_after, void* data)
{
    const auto& context = *static_cast<SprintathonShortcutContext*>(data);
    world_point2d start = {context.object->location.x,context.object->location.y};
    world_point2d destination = *goal;
    const double dx = double(goal->x)-start.x, dy = double(goal->y)-start.y;
    if (dx*dx+dy*dy > double(12*WORLD_ONE)*(12*WORLD_ONE)) return false;
    short polygon = context.object->polygon;
    short visited[64];
    int used = 0;
    bool arrived = false;
    const double clearance = context.radius+WORLD_ONE/64;
    for (int count = 0; count < 32; ++count) {
        for (int i = 0; i < count; ++i) if (visited[i] == polygon) return false;
        visited[count] = polygon;
        used = count+1;
        const auto* room = get_polygon_data(polygon);
        if (!sprintathon_shortcut_room(room) ||
            int(room->ceiling_height)-room->floor_height < context.height) return false;
        // Check the entire body's clearance from every blocking edge, including
        // corners the center ray would miss. Conservative rejection is safe.
        for (int i = 0; i < room->vertex_count; ++i) {
            const short edge = room->line_indexes[i];
            if (sprintathon_shortcut_portal(polygon, edge, context)) continue;
            const auto* line = get_line_data(edge);
            const auto& a = get_endpoint_data(line->endpoint_indexes[0])->vertex;
            const auto& b = get_endpoint_data(line->endpoint_indexes[1])->vertex;
            if (sprintathon_segment_clearance2(start,*goal,a,b) < clearance*clearance) return false;
        }
        const short edge = find_line_crossed_leaving_polygon(polygon, &start, &destination);
        if (edge == NONE) {
            // XY coincidence in a different overlapping room is not arrival.
            arrived = polygon == expected_before || polygon == expected_after;
            break;
        }
        if (!sprintathon_shortcut_portal(polygon, edge, context)) return false;
        polygon = find_adjacent_polygon(polygon, edge);
    }
    if (!arrived) return false;
    // The body can graze a side room without its center entering it. Explore
    // those adjacent slivers too, so an inset wall cannot be cut through.
    for (int i = 0; i < used; ++i) {
        const short current = visited[i];
        const auto* room = get_polygon_data(current);
        for (int n = 0; n < room->vertex_count; ++n) {
            const short edge = room->line_indexes[n];
            const auto* line = get_line_data(edge);
            const auto& a = get_endpoint_data(line->endpoint_indexes[0])->vertex;
            const auto& b = get_endpoint_data(line->endpoint_indexes[1])->vertex;
            if (sprintathon_segment_clearance2(start,*goal,a,b) >= clearance*clearance) continue;
            if (!sprintathon_shortcut_portal(current, edge, context)) return false;
            const short adjacent = find_adjacent_polygon(current, edge);
            bool found = false;
            for (int j = 0; j < used; ++j) if (visited[j] == adjacent) found = true;
            if (!found) {
                if (used == 64) return false;
                visited[used++] = adjacent;
            }
        }
    }
    return true;
}

struct SprintathonApproachArc {
    short object = NONE, target = NONE;
    int tick = -1, level = -1, straight_until = -1;
    double bias = 0, turn = 0;
};
static std::map<short, SprintathonApproachArc> sprintathon_approach_arcs;
static double sprintathon_approach_curve(short index, double remaining, angle heading)
{
    auto* monster = get_monster_data(index);
    auto* definition = get_monster_definition(monster->type);
    if (!input_preferences->sprintathon_enabled || !input_preferences->sprintathon_enhanced_enemy_ai ||
        game_is_networked || game_is_being_replayed() || MONSTER_IS_DYING(monster) ||
        monster->external_velocity || monster->action == _monster_is_being_hit ||
        (definition->flags & _monster_uses_sniper_ledges)) return 0;
    auto* object = get_object_data(monster->object_index);
    auto& arc = sprintathon_approach_arcs[index];
    const int now = dynamic_world->tick_count;
    if (arc.object != monster->object_index || arc.target != monster->target_index ||
        arc.level != dynamic_world->current_level_number || now < arc.tick || now-arc.tick > 2*TICKS_PER_SECOND) {
        arc = SprintathonApproachArc{};
        arc.object = monster->object_index; arc.target = monster->target_index;
        arc.level = dynamic_world->current_level_number;
        // Stable variation without consuming the simulation's random stream.
        uint32 seed = uint32(index+1)*0x9e3779b9u ^ uint32(arc.object+1)*0x85ebca6bu ^
            uint32(arc.target+2)*0xc2b2ae35u ^ uint32(arc.level+1);
        seed ^= seed >> 16;
        arc.bias = ((seed & 1) ? 1.0 : -1.0)*(0.65+0.35*((seed >> 8)&255)/255.0);
    }
    const int elapsed = arc.tick < 0 ? 1 : std::max(0,std::min(4,now-arc.tick));
    arc.tick = now;
    const bool airborne = definition->flags & (_monster_flys | _monster_floats);
    double fade = std::max(0.0,std::min(1.0,(remaining-WORLD_ONE/2)/(2.0*WORLD_ONE)));
    if (MONSTER_HAS_VALID_TARGET(monster) && definition->melee_attack.type != NONE) {
        const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
        const double dx = double(target->location.x)-object->location.x;
        const double dy = double(target->location.y)-object->location.y;
        fade *= std::max(0.0,std::min(1.0,
            (std::sqrt(dx*dx+dy*dy)-definition->melee_attack.range-WORLD_ONE/4)/WORLD_ONE));
    }
    const auto climbing = sprintathon_float_motion.find(index);
    if (climbing != sprintathon_float_motion.end() && now < climbing->second.climb_until) fade = 0;
    if (now < arc.straight_until) {
        arc.turn = 0;
        return 0;
    }
    const double wanted = arc.bias*(airborne ? 0.30 : 0.20)*fade;
    arc.turn += (wanted-arc.turn)*(1-std::pow(0.85,elapsed));
    if (fade == 0) arc.turn = 0;
    if (std::abs(arc.turn) < 0.001) return 0;

    // Curvature is optional: require spare body clearance along both the
    // route and curved intent. Looking only at the curved endpoint lets the
    // actor drift off the doorway centreline before noticing its jamb.
    SprintathonShortcutContext context;
    context.object = object; context.definition = definition;
    get_monster_dimensions(index, &context.radius, &context.height);
    context.radius = world_distance(std::min(32767,
        int(context.radius)+std::max(int(WORLD_ONE/8), int(context.radius)/2)));
    const world_distance lookahead = world_distance(std::min(remaining,
        std::min(2.0*WORLD_ONE, std::max(1.5*WORLD_ONE,
            12.0*std::abs(int(definition->speed))))));
    auto roomy = [&](angle direction) {
        world_point3d probe = object->location;
        // Avoid wrapping map coordinates at the signed world-distance limits.
        const double theta = direction*6.283185307179586/FULL_CIRCLE;
        const double x = object->location.x+lookahead*std::cos(theta);
        const double y = object->location.y+lookahead*std::sin(theta);
        if (x < -32767 || x > 32766 || y < -32767 || y > 32766) return false;
        translate_point2d(&probe, lookahead, direction);
        short polygon = object->polygon;
        bool arrived = false;
        for (int i = 0; i < 32; ++i) {
            const short line = find_line_crossed_leaving_polygon(polygon, &object->location, &probe);
            if (line == NONE) { arrived = true; break; }
            polygon = find_adjacent_polygon(polygon, line);
            if (polygon == NONE) break;
        }
        world_point2d point = {probe.x, probe.y};
        return arrived && sprintathon_clear_shortcut(&point, polygon, polygon, &context) &&
            legal_monster_move(index, direction, &probe) == NONE;
    };
    const angle direction = NORMALIZE_ANGLE(heading+int(arc.turn*FULL_CIRCLE/6.283185307179586));
    if (!roomy(heading) || !roomy(direction)) {
        arc.turn = 0;
        arc.straight_until = now+TICKS_PER_SECOND;
        return 0;
    }
    return arc.turn;
}

void advance_monster_path(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	world_point2d path_goal;
	bool done= true;
    sprintathon_curve_waypoints.erase(monster_index);

	if (monster->path==NONE)
	{
		/* only locked monsters in their target’s polygon can advance without paths */
		if (monster->mode!=_monster_locked || object->polygon!=get_object_data(get_monster_data(monster->target_index)->object_index)->polygon)
		{
			monster_needs_path(monster_index, true);
			return;
		}
	}
	else
	{
        const auto* definition = get_monster_definition(monster->type);
        if (input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
            !game_is_networked && !game_is_being_replayed() &&
            !(definition->flags & _monster_uses_sniper_ledges)) {
            SprintathonShortcutContext context;
            context.object = object;
            context.definition = get_monster_definition(monster->type);
            get_monster_dimensions(monster_index, &context.radius, &context.height);
            done = move_along_path_with_lookahead(monster->path, &path_goal,
                sprintathon_clear_shortcut, &context);
        } else {
            done = move_along_path(monster->path, &path_goal);
        }
		if (done)
		monster->path= NONE;
	}

	/* if we’re locked without a path, head right for the bastard (he’s in our polygon) */
	if ((done||monster->path==NONE) && monster->mode==_monster_locked)
	{
		struct monster_data *target= get_monster_data(monster->target_index);
		struct object_data *target_object= get_object_data(target->object_index);
		
		if (object->polygon==target_object->polygon)
		{
			world_point3d location= get_object_data(get_monster_data(monster->target_index)->object_index)->location;
			path_goal.x= location.x;
			path_goal.y= location.y;
			done= false;
		}
	}
	
	if (done)
	{
		/* ask for a new path (never happens to locked monsters) */
		monster_needs_path(monster_index, false);
		if (monster->mode==_monster_unlocked)
		{
			monster->goal_polygon_index= NONE;
			if (MONSTER_TELEPORTS_OUT_WHEN_DEACTIVATED(monster)) deactivate_monster(monster_index);
		}
	}
	else
	{
        if (input_preferences->sprintathon_enabled && input_preferences->sprintathon_enhanced_enemy_ai &&
            !game_is_networked && !game_is_being_replayed())
            sprintathon_curve_waypoints[monster_index] = {monster->object_index, path_goal};
		/* point ourselves at this new point in the path */
		object->facing= arctangent(path_goal.x-object->location.x, path_goal.y-object->location.y);
		monster->path_segment_length= distance2d(&path_goal, &object->location);
	}
}

static bool sprintathon_hold_friendly_shot(short index, short polygon,
    const world_point3d& origin, const world_point3d& vector, short projectile_type);

static bool try_monster_attack(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct object_data *object= get_object_data(monster->object_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	short repetitions= NONE;
	short new_action= NONE, obstruction_index= NONE;
	angle theta = 0;

	if (MONSTER_HAS_VALID_TARGET(monster))
	{
		struct object_data *target_object= get_object_data(get_monster_data(monster->target_index)->object_index);
		world_point3d origin= object->location, destination= target_object->location;
		world_distance range= distance2d(&origin, &destination);
		short polygon_index;
		world_point3d _vector;
	
		theta= arctangent(destination.x-origin.x, destination.y-origin.y);
		angle delta_theta= NORMALIZE_ANGLE(theta-object->facing);
		
		if (!(definition->flags&_monster_cant_fire_backwards) || (delta_theta<QUARTER_CIRCLE+QUARTER_CIRCLE/2 || delta_theta>FULL_CIRCLE-QUARTER_CIRCLE-QUARTER_CIRCLE/2))
		{
			switch (monster->action)
			{
				case _monster_is_attacking_close:
				case _monster_is_attacking_far:
					new_action= monster->action;
					break;
				
				default:
					if (definition->ranged_attack.type!=NONE && range<definition->ranged_attack.range) new_action= _monster_is_attacking_far;
					if (definition->melee_attack.type!=NONE && range<definition->melee_attack.range)
					{
						new_action= _monster_is_attacking_close;
		
						if (definition->flags&_monster_chooses_weapons_randomly)
						{
							bool switch_to_ranged = true;
							if (film_profile.validate_random_ranged_attack)
							{
								if (definition->ranged_attack.type == NONE)
								{
									logWarning("Monster chooses weapons randomly, but has no ranged attack");
									definition->flags &= ~_monster_chooses_weapons_randomly;
									switch_to_ranged = false;
								}
								else
									switch_to_ranged = (range<definition->ranged_attack.range);
							}
							if (switch_to_ranged && global_random()&1)
								new_action= _monster_is_attacking_far;
						}
					}
					break;
			}

			/* if we have a melee attack and we're at short range, use it */
			if (new_action==_monster_is_attacking_close)
			{
				/* make sure this is a valid projectile, that we don’t hit any walls and that whatever
					we did hit is _hostile. */
				polygon_index= position_monster_projectile(monster_index, monster->target_index, &definition->melee_attack, &origin, &destination, &_vector, theta);
				if (preflight_projectile(&origin, polygon_index, &destination, definition->melee_attack.error,
					definition->melee_attack.type, monster_index, monster->type, &obstruction_index))
				{
					if ((obstruction_index!=NONE && get_monster_attitude(monster_index, obstruction_index)==_hostile) ||
						!line_is_obstructed(object->polygon, &object->location, target_object->polygon, &target_object->location))
					{
						repetitions= definition->melee_attack.repetitions;
					}
				}
			}
			else
			{
				/* make sure we have a ranged attack and our target is within range */
				if (new_action==_monster_is_attacking_far)
				{
					/* make sure this is a valid projectile, that we don’t hit any walls and that whatever
						we did hit is _hostile. */
					polygon_index= position_monster_projectile(monster_index, monster->target_index, &definition->ranged_attack, &origin, &destination, &_vector, theta);
                    if (sprintathon_hold_friendly_shot(monster_index, polygon_index, origin, _vector, definition->ranged_attack.type))
                        return false;
					if (preflight_projectile(&origin, polygon_index, &destination, definition->ranged_attack.error,
						definition->ranged_attack.type, monster_index, monster->type, &obstruction_index))
					{
						if ((obstruction_index!=NONE && get_monster_attitude(monster_index, obstruction_index)==_hostile) ||
							(obstruction_index==NONE && !line_is_obstructed(object->polygon, &object->location, target_object->polygon, &target_object->location)))
						{
							repetitions= definition->ranged_attack.repetitions;
						}
					}
				}
			}
		}
	}
	
	if (repetitions!=NONE)
	{
		/* we can attack; set monster facing, start the attack action and reset ticks_since_attack */
		object->facing= theta;
		if (monster->action!=new_action) /* if we’re already attacking, this is a chained attack */
		{
			switch (dynamic_world->game_information.difficulty_level)
			{
				case _wuss_level: case _easy_level: repetitions>>= 1;
				case _normal_level: repetitions= (repetitions<=1) ? repetitions : repetitions-1; break;
			}
			
			set_monster_action(monster_index, new_action);
			monster->attack_repetitions= repetitions;
		}
		
		/* on the highest level, hitting a monster in the middle of an attack doesn’t really
			stop him from continuing to attack because ticks_since_attack is never reset */
		switch (dynamic_world->game_information.difficulty_level)
		{
			case _total_carnage_level:
				break;
			
			default:
				monster->ticks_since_attack= 0;
				break;
		}
	}
	else
	{
		/* we can’t attack (for whatever reason), halve ticks_since_attack so we try again soon */
		monster->ticks_since_attack= 0;
        if (new_action == _monster_is_attacking_far && obstruction_index != NONE &&
            get_monster_attitude(monster_index, obstruction_index) == _friendly)
            sprintathon_request_clear_shot(monster_index);
			
		if (obstruction_index!=NONE && get_monster_attitude(monster_index, obstruction_index)==_friendly &&
			MONSTER_IS_PLAYER(get_monster_data(obstruction_index)))
		{
			play_object_sound(monster->object_index, definition->clear_sound);
		}
	}
	
	return new_action==NONE ? false : true;
}

// Recheck the actual firing direction at release, including each muzzle of
// symmetric attacks. An ally may have entered the lane during the animation.
static bool sprintathon_hold_friendly_shot(short index, short polygon,
    const world_point3d& origin, const world_point3d& vector, short projectile_type)
{
    const auto* monster = get_monster_data(index);
    const auto* definition = get_monster_definition(monster->type);
    if (!input_preferences->sprintathon_enabled || !input_preferences->sprintathon_enhanced_enemy_ai ||
        game_is_networked || game_is_being_replayed() || MONSTER_IS_PLAYER(monster) ||
        MONSTER_IS_BERSERK(monster) || !MONSTER_HAS_VALID_TARGET(monster) || polygon == NONE) return false;
    const auto* target = get_object_data(get_monster_data(monster->target_index)->object_index);
    const double dx = double(target->location.x)-origin.x, dy = double(target->location.y)-origin.y;
    const double length = std::sqrt(double(vector.x)*vector.x+double(vector.y)*vector.y);
    if (length < 1) return false;
    const double scale = std::sqrt(dx*dx+dy*dy)/length;
    auto coordinate = [](double value) { return static_cast<world_distance>(std::max(-32768.0, std::min(32767.0, value))); };
    world_point3d from = origin;
    world_point3d to = {coordinate(origin.x+vector.x*scale), coordinate(origin.y+vector.y*scale), coordinate(origin.z+vector.z*scale)};
    // Projectile collision rules can ignore same-species monsters. Check the
    // firing corridor ourselves before relying on projectile preflight.
    const double range = std::sqrt(dx*dx+dy*dy);
    for (short other = 0; other < MAXIMUM_MONSTERS_PER_MAP; ++other) {
        const auto* ally = monsters+other;
        if (!SLOT_IS_USED(ally) || other == index || other == monster->target_index || MONSTER_IS_DYING(ally)) continue;
        const auto* ally_object = get_object_data(ally->object_index);
        if (OBJECT_IS_INVISIBLE(ally_object)) continue;
        const auto* ally_definition = get_monster_definition(ally->type);
        // Explicit targets remain attackable, including deliberate infighting.
        if (get_monster_attitude(index, other) == _hostile &&
            !(definition->_class & ally_definition->_class)) continue;
        const double ax = double(ally_object->location.x)-origin.x;
        const double ay = double(ally_object->location.y)-origin.y;
        const double along = (ax*vector.x+ay*vector.y)/length;
        if (along <= 0 || along >= range) continue;
        const double across = (ax*vector.y-ay*vector.x)/length;
        const double clearance = ally_definition->radius + std::min(double(WORLD_ONE)/3, WORLD_ONE/12.0+along*0.06);
        if (std::abs(across) > clearance) continue;
        const double z = origin.z+vector.z*(along/length);
        if (z < ally_object->location.z-WORLD_ONE/8 ||
            z > double(ally_object->location.z)+ally_definition->height+WORLD_ONE/8) continue;
        // Reject unrelated overlapping map layers and allies behind walls.
        if (!clear_line_of_sight(index, other, true)) continue;
        get_monster_data(index)->attack_repetitions = 0;
        if (sprintathon_ground_ai(monster, definition)) sprintathon_request_ground_shot(index);
        else sprintathon_request_clear_shot(index);
        return true;
    }
    // Probe a small firing corridor rather than only its exact centerline.
    const double margin = std::min(double(WORLD_ONE)/3, definition->radius/2.0 + std::sqrt(dx*dx+dy*dy)*0.04);
    for (int probe : {0, -1, 1}) {
        world_point3d ray_end = to;
        ray_end.x = coordinate(to.x-probe*margin*vector.y/length);
        ray_end.y = coordinate(to.y+probe*margin*vector.x/length);
        short obstruction = NONE;
        preflight_projectile(&from, polygon, &ray_end, 0, projectile_type, index, monster->type, &obstruction);
        if (obstruction == NONE || obstruction == monster->target_index) continue;
        const auto* blocker = get_monster_data(obstruction);
        const auto* blocker_definition = get_monster_definition(blocker->type);
        const bool ally = get_monster_attitude(index, obstruction) != _hostile ||
            (definition->_class & blocker_definition->_class) != 0;
        if (ally) {
            if (sprintathon_ground_ai(monster, definition)) sprintathon_request_ground_shot(index);
            else sprintathon_request_clear_shot(index);
            return true;
        }
    }
    return false;
}

static void execute_monster_attack(
	short monster_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	
	/* we used to assert that the attacking monster was locked, but monsters can be deactivated
		or lose lock during an attack (!) so we just abort if we no longer have a valid target */
	if (MONSTER_HAS_VALID_TARGET(monster))
	{
		struct monster_definition *definition= get_monster_definition(monster->type);
		struct object_data *object= get_object_data(monster->object_index);
		struct attack_definition *attack= (monster->action==_monster_is_attacking_close) ? &definition->melee_attack : &definition->ranged_attack;
		short projectile_polygon_index;
		world_point3d origin= object->location;
		world_point3d _vector;
		
		projectile_polygon_index= position_monster_projectile(monster_index, monster->target_index, attack, &origin, NULL, &_vector, object->facing);
		if (projectile_polygon_index != NONE &&
            (monster->action != _monster_is_attacking_far ||
             !sprintathon_hold_friendly_shot(monster_index, projectile_polygon_index, origin, _vector, attack->type)))
			new_projectile(&origin, projectile_polygon_index, &_vector, attack->error, attack->type, monster_index, monster->type, monster->target_index, FIXED_ONE);
		if (definition->flags&_monster_fires_symmetrically)
		{
			attack->dy= -attack->dy;
			projectile_polygon_index= position_monster_projectile(monster_index, monster->target_index, attack, &origin, NULL, &_vector, object->facing);
			if (projectile_polygon_index != NONE &&
            (monster->action != _monster_is_attacking_far ||
             !sprintathon_hold_friendly_shot(monster_index, projectile_polygon_index, origin, _vector, attack->type)))
				new_projectile(&origin, projectile_polygon_index, &_vector, attack->error, attack->type, monster_index, monster->type, monster->target_index, FIXED_ONE);
			attack->dy= -attack->dy;
		}
	}
}

int32 monster_pathfinding_cost_function(
	short source_polygon_index,
	short line_index,
	short destination_polygon_index,
	void *vdata)
{
	struct monster_pathfinding_data *data=(struct monster_pathfinding_data *)vdata;
	struct monster_definition *definition= data->definition;
	struct polygon_data *destination_polygon= get_polygon_data(destination_polygon_index);
	struct polygon_data *source_polygon= get_polygon_data(source_polygon_index);
	struct line_data *line= get_line_data(line_index);
	bool respect_polygon_heights= true;
	struct object_data *object;
	short object_index;
	int32 cost;
		
	/* base cost is the area of the polygon we’re leaving */
	cost= source_polygon->area;

	/* no solid lines (baby) */
	if (LINE_IS_SOLID(line) && !LINE_IS_VARIABLE_ELEVATION(line)) cost= -1;

	/* count up the monsters in destination_polygon and add a constant cost, MONSTER_PATHFINDING_OBSTRUCTION_PENALTY,
		for each of them to discourage overcrowding */
	for (object_index= destination_polygon->first_object; object_index!=NONE; object_index=  object->next_object)
	{
		object= get_object_data(object_index);
		if (GET_OBJECT_OWNER(object)==_object_is_monster) cost+= MONSTER_PATHFINDING_OBSTRUCTION_COST;
	}

	/* if we’re trying to move into a polygon with an area smaller than MINIMUM_MONSTER_PATHFINDING_POLYGON_AREA, disallow the move */
	if (source_polygon->area<MINIMUM_MONSTER_PATHFINDING_POLYGON_AREA) cost= -1;

	// do platform stuff	
	if (cost>0)
	{
		if (destination_polygon->type==_polygon_is_platform)
		{
			switch (monster_can_enter_platform(destination_polygon->permutation, source_polygon_index, definition->height, definition->minimum_ledge_delta, definition->maximum_ledge_delta))
			{
				case _platform_will_never_be_accessable: cost= -1; break;
				default: cost+= MONSTER_PATHFINDING_PLATFORM_COST; respect_polygon_heights= false; break;
			}
            
            // don't move into flooded platforms
            if ((static_world->environment_flags&_environment_ouch_m1) &&
                !(definition->flags&(_monster_flys|_monster_floats)) &&
                PLATFORM_IS_FLOODED(get_platform_data(destination_polygon->permutation)) &&
                (find_flooding_polygon(destination_polygon_index) != NONE))
                cost= -1;
		}
		if (source_polygon->type==_polygon_is_platform)
		{
			switch (monster_can_leave_platform(source_polygon->permutation, destination_polygon_index, definition->height, definition->minimum_ledge_delta, definition->maximum_ledge_delta))
			{
				case _exit_will_never_be_accessable: cost= -1; break;
				default: respect_polygon_heights= false; break;
			}
		}
	}
		
	/* if the ledge between polygons is too high, the fall is too far, or there just
		isn’t enough vertical space, disallow the move (and ignore this if we’re dealing with
		platforms or doors) */
	if (respect_polygon_heights)
	{
		world_distance delta_height= destination_polygon->floor_height-source_polygon->floor_height;
		
		if (delta_height<definition->minimum_ledge_delta||delta_height>definition->maximum_ledge_delta) cost= -1;
		if (line->lowest_adjacent_ceiling-line->highest_adjacent_floor<definition->height) cost= -1;
		
		if (cost>0) cost+= delta_height*delta_height; /* prefer not to change heights */
	}
	
	/* if this line not wide enough, disallow the move */
	if (line->length<2*definition->radius) cost= -1;

	if (cost>0)
	{
		/* if we’re trying to move into an impassable polygon, disallow the move */
		switch (destination_polygon->type)
		{
			case _polygon_is_zone_border:
				if (!data->cross_zone_boundaries) cost= -1;
				break;
			
			case _polygon_is_monster_impassable:
			case _polygon_is_teleporter:
				cost= -1;
				break;
            
			case _polygon_is_minor_ouch:
			case _polygon_is_major_ouch:
				if ((static_world->environment_flags&_environment_ouch_m1) &&
				    !(definition->flags&(_monster_flys|_monster_floats)))
					cost= -1;
				break;
		}
	}

	if (cost>0)
	{
		/* if we’re trying to move into media, pay the penalty */
		if (destination_polygon->media_index!=NONE)
		{
			struct media_data *media= get_media_data(destination_polygon->media_index);
			
			// LP change: idiot-proofed this
			if (media)
			{
				if (media->height>destination_polygon->floor_height)
				{
					cost+= 2*destination_polygon->area;
				}
			}
		}
	}

	return cost;
}

/* returns the type and index of any interesting terrain feature (platform or door) in front
	of the given monster in his current direction; this lets us open doors and wait for
	platforms.  relevant_polygon_index is the polygon_index we have to pass to platform_is_accessable */
static short find_obstructing_terrain_feature(
	short monster_index,
	short *feature_index,
	short *relevant_polygon_index)
{
	struct monster_data *monster= get_monster_data(monster_index);
	struct monster_definition *definition= get_monster_definition(monster->type);
	struct object_data *object= get_object_data(monster->object_index);
	short polygon_index, feature_type;
	world_point2d p1;

	ray_to_line_segment(&object->location, &p1, object->facing, MONSTER_PLATFORM_BUFFER_DISTANCE+definition->radius);
	
	feature_type= NONE;
	*feature_index= NONE;
	*relevant_polygon_index= polygon_index= object->polygon;
	do
	{
		struct polygon_data *polygon= get_polygon_data(polygon_index);
		short line_index= find_line_crossed_leaving_polygon(polygon_index, &object->location, &p1);
		
		switch (polygon->type)
		{
			case _polygon_is_platform:
				if (object->polygon==polygon_index)
				{
					/* we’re standing on the platform: find out where we’re headed (if we’re
						going nowhere then pretend like everything is o.k.) */

					polygon_index= line_index==NONE ? NONE : find_adjacent_polygon(polygon_index, line_index);
					if (polygon_index!=NONE)
					{
						*relevant_polygon_index= polygon_index;
						*feature_index= polygon->permutation;
						feature_type= _leaving_platform_polygon;
						assert(*feature_index!=NONE);
					}
				}
				else
				{
					feature_type= _entering_platform_polygon;
					*feature_index= polygon->permutation;
					assert(*feature_index!=NONE);
				}
				break;
			
			default:
				if (((definition->flags&_monster_floats) && polygon->floor_height>monster->desired_height) ||
					object->location.z+definition->height>polygon->ceiling_height)
				{
					monster->desired_height= polygon->floor_height;
                    if (sprintathon_soft_float(monster, definition) &&
                        polygon->floor_height > get_polygon_data(object->polygon)->floor_height) {
                        auto& motion = sprintathon_float_motion[monster_index];
                        motion.climb_floor = polygon->floor_height;
                        // A little clearance prevents damped motion from
                        // asymptotically stopping just below the step's lip.
                        motion.climb_goal = std::min(int(polygon->ceiling_height)-definition->height,
                            int(polygon->floor_height)+WORLD_ONE/32);
                        motion.climb_until = dynamic_world->tick_count + 4*TICKS_PER_SECOND;
                    }
					feature_type= _flying_or_floating_transition;
					*feature_index= 0;
				}
				if (definition->flags&_monster_flys)
				{
					if ((polygon->floor_height>monster->desired_height) ||
						(polygon->ceiling_height<monster->desired_height+definition->height))
					{
						monster->desired_height= (polygon->floor_height>monster->desired_height) ?
							polygon->floor_height : (polygon->ceiling_height - definition->height);
						feature_type= _flying_or_floating_transition;
						*feature_index= 0;
					}
					
					if (object->location.z<polygon->floor_height || object->location.z+definition->height>polygon->ceiling_height)
					{
						feature_type= _flying_or_floating_transition;
						*feature_index= 0;
					}
				}
				if (definition->flags&_monster_uses_sniper_ledges)
				{
					if ((polygon->floor_height+MINIMUM_SNIPER_ELEVATION<monster->desired_height) &&
						monster->mode==_monster_locked)
					{
						feature_type= _standing_on_sniper_ledge;
					}
				}
				if (!(definition->flags&(_monster_floats|_monster_flys)) && polygon->media_index!=NONE)
				{
					struct media_data *media= get_media_data(polygon->media_index);
					// Monster will normally wade to half-height
					world_distance height= definition->height>>1;
					// LP change: idiot-proofing
					if (media)
					{
						// In dangerous media, wade only to zero height (that is, don't wade at all)
						if (IsMediaDangerous(media->type)) height= 0;
						
						switch (media->type)
						{
							case _media_water: if (definition->flags&_monster_is_not_afraid_of_water) media= (struct media_data *) NULL; break;
							case _media_jjaro: // LP addition: monsters treat Jjaro goo like sewage
							case _media_sewage: if (definition->flags&_monster_is_not_afraid_of_sewage) media= (struct media_data *) NULL; break;
							case _media_lava: /* height= 0; */ if (definition->flags&_monster_is_not_afraid_of_lava) media= (struct media_data *) NULL; break;
							case _media_goo: /* height= 0; */ if (definition->flags&_monster_is_not_afraid_of_goo) media= (struct media_data *) NULL; break;
						}
					}
					if (media && media->height-polygon->floor_height>height)
					{
						if (get_polygon_data(object->polygon)->floor_height>polygon->floor_height)
						{
							feature_type= _standing_on_sniper_ledge;
							if (monster->mode!=_monster_locked) monster_needs_path(monster_index, false);
						}
					}
				}
				polygon_index= line_index==NONE ? NONE : find_adjacent_polygon(polygon_index, line_index);
				break;
		}
		
		if (line_index!=NONE && polygon_index==NONE)
		{
			if (monster->path_segment_length<MONSTER_PLATFORM_BUFFER_DISTANCE)
			{
				monster->path_segment_length= 0;
			}
			else
			{
				/* we’re headed for a wall solid; freeze and get a new path, pronto */
				feature_type= _standing_on_sniper_ledge;
				monster_needs_path(monster_index, true);
			}
		}
	}
	while (polygon_index!=NONE&&(feature_type==NONE||feature_type==_flying_or_floating_transition));
	
	return feature_type;
}

/* returns new polygon index; if destination is NULL then we fire along the monster’s facing
	and elevation, if destination is not NULL then we set it correctly and save the elevation angle */
static short position_monster_projectile(
	short aggressor_index,
	short target_index,
	struct attack_definition *attack,
	world_point3d *origin,
	world_point3d *destination,
	world_point3d *_vector,
	angle theta)
{
	struct monster_data *aggressor= get_monster_data(aggressor_index);
	struct monster_data *target= get_monster_data(target_index);
	struct object_data *aggressor_object= get_object_data(aggressor->object_index);
	struct object_data *target_object= get_object_data(target->object_index);
	world_distance radius, height;

//	dprintf("positioning #%d to #%d", aggressor_index, target_index);

	/* adjust origin */
	*origin= aggressor_object->location;
	origin->z+= attack->dz;
	translate_point2d(origin, attack->dy, NORMALIZE_ANGLE(theta+QUARTER_CIRCLE));
	translate_point2d(origin, attack->dx, theta);
	
	if (destination)
	{
		world_distance distance;
		
		/* adjust destination */
		get_monster_dimensions(target_index, &radius, &height);
		*destination= target_object->location;
		destination->z+= (height>>1) + (height>>2); /* shoot 3/4ths up the target */
		
		/* calculate outbound vector */
		_vector->x= destination->x-origin->x;
		_vector->y= destination->y-origin->y;
		_vector->z= destination->z-origin->z;
		
		distance= isqrt(_vector->x*_vector->x + _vector->y*_vector->y);
		aggressor->elevation= distance ? (_vector->z*TRIG_MAGNITUDE)/distance : 0;
	}
	else
	{
		_vector->x= cosine_table[theta];
		_vector->y= sine_table[theta];
		_vector->z= aggressor->elevation;
	}
	
	/* return polygon_index of the new origin point */
	return find_new_object_polygon(&aggressor_object->location,
		origin, aggressor_object->polygon);
}

short nearest_goal_polygon_index(
	short polygon_index)
{
	polygon_index= flood_map(polygon_index, INT32_MAX, nearest_goal_cost_function, _breadth_first, (void *) NULL);
	while (polygon_index!=NONE)
	{
		struct polygon_data *polygon= get_polygon_data(polygon_index);

		if (polygon->type==_polygon_is_goal) break;

		polygon_index= flood_map(NONE, INT32_MAX, nearest_goal_cost_function, _breadth_first, (void *) NULL);
	}
	
	return polygon_index;
}

static int32 nearest_goal_cost_function(
	short source_polygon_index,
	short line_index,
	short destination_polygon_index,
	void *unused)
{
	struct polygon_data *destination_polygon= get_polygon_data(destination_polygon_index);
//	struct polygon_data *source_polygon= get_polygon_data(source_polygon_index);
//	struct line_data *line= get_line_data(line_index);
	int32 cost= 1;
	
	(void) (unused);
	(void) (source_polygon_index);
	(void) (line_index);

	if (destination_polygon->type==_polygon_is_zone_border) cost= -1;
	
	return cost;
}


// LP: will set player view attributes when trying to shoot a guided projectile.
void SetPlayerViewAttribs(int16 half_visual_arc, int16 half_vertical_visual_arc,
	world_distance visual_range, world_distance dark_visual_range)
{
	// Added a modified version of AlexJS's changes: change only if necessary
	// Restoring AlexJLS's changes
	monster_definition& PlayerAsMonster = monster_definitions[_monster_marine];
	if (half_visual_arc > 0 || PlayerAsMonster.half_visual_arc > 0)
		PlayerAsMonster.half_visual_arc = half_visual_arc;
	if (half_vertical_visual_arc > 0 || PlayerAsMonster.half_vertical_visual_arc > 0)
		PlayerAsMonster.half_vertical_visual_arc = half_vertical_visual_arc;
	if (visual_range > 0 || PlayerAsMonster.visual_range > 0)
		PlayerAsMonster.visual_range = visual_range;
	if (dark_visual_range > 0 || PlayerAsMonster.dark_visual_range > 0)
		PlayerAsMonster.dark_visual_range = dark_visual_range;
}


uint8 *unpack_monster_data(uint8 *Stream, monster_data *Objects, size_t Count)
{
	uint8* S = Stream;
	monster_data* ObjPtr = Objects;
	
	for (size_t k = 0; k < Count; k++, ObjPtr++)
	{
		StreamToValue(S,ObjPtr->type);
		StreamToValue(S,ObjPtr->vitality);
		StreamToValue(S,ObjPtr->flags);
		
		StreamToValue(S,ObjPtr->path);
		StreamToValue(S,ObjPtr->path_segment_length);
		StreamToValue(S,ObjPtr->desired_height);
		
		StreamToValue(S,ObjPtr->mode);
		StreamToValue(S,ObjPtr->action);
		StreamToValue(S,ObjPtr->target_index);
		StreamToValue(S,ObjPtr->external_velocity);
		StreamToValue(S,ObjPtr->vertical_velocity);
		StreamToValue(S,ObjPtr->ticks_since_attack);
		StreamToValue(S,ObjPtr->attack_repetitions);
		StreamToValue(S,ObjPtr->changes_until_lock_lost);
		
		StreamToValue(S,ObjPtr->elevation);
		
		StreamToValue(S,ObjPtr->object_index);
		
		StreamToValue(S,ObjPtr->ticks_since_last_activation);
		
		StreamToValue(S,ObjPtr->activation_bias);
		
		StreamToValue(S,ObjPtr->goal_polygon_index);
		
		StreamToValue(S,ObjPtr->sound_location.x);
		StreamToValue(S,ObjPtr->sound_location.y);
		StreamToValue(S,ObjPtr->sound_location.z);
		StreamToValue(S,ObjPtr->sound_polygon_index);
		
		StreamToValue(S,ObjPtr->random_desired_height);
		
		S += 7*2;
	}
	
	assert((S - Stream) == static_cast<ptrdiff_t>(Count*SIZEOF_monster_data));
	return S;
}

uint8 *pack_monster_data(uint8 *Stream, monster_data *Objects, size_t Count)
{
	uint8* S = Stream;
	monster_data* ObjPtr = Objects;
	
	for (size_t k = 0; k < Count; k++, ObjPtr++)
	{
		ValueToStream(S,ObjPtr->type);
		ValueToStream(S,ObjPtr->vitality);
		ValueToStream(S,ObjPtr->flags);
		
		ValueToStream(S,ObjPtr->path);
		ValueToStream(S,ObjPtr->path_segment_length);
		ValueToStream(S,ObjPtr->desired_height);
		
		ValueToStream(S,ObjPtr->mode);
		ValueToStream(S,ObjPtr->action);
		ValueToStream(S,ObjPtr->target_index);
		ValueToStream(S,ObjPtr->external_velocity);
		ValueToStream(S,ObjPtr->vertical_velocity);
		ValueToStream(S,ObjPtr->ticks_since_attack);
		ValueToStream(S,ObjPtr->attack_repetitions);
		ValueToStream(S,ObjPtr->changes_until_lock_lost);
		
		ValueToStream(S,ObjPtr->elevation);
		
		ValueToStream(S,ObjPtr->object_index);
		
		ValueToStream(S,ObjPtr->ticks_since_last_activation);
		
		ValueToStream(S,ObjPtr->activation_bias);
		
		ValueToStream(S,ObjPtr->goal_polygon_index);
		
		ValueToStream(S,ObjPtr->sound_location.x);
		ValueToStream(S,ObjPtr->sound_location.y);
		ValueToStream(S,ObjPtr->sound_location.z);
		ValueToStream(S,ObjPtr->sound_polygon_index);
		
		ValueToStream(S,ObjPtr->random_desired_height);
		
		S += 7*2;
	}
	
	assert((S - Stream) == static_cast<ptrdiff_t>(Count*SIZEOF_monster_data));
	return S;
}


inline void StreamToAttackDef(uint8* &S, attack_definition& Object)
{
	StreamToValue(S,Object.type);
	StreamToValue(S,Object.repetitions);
	StreamToValue(S,Object.error);
	StreamToValue(S,Object.range);
	StreamToValue(S,Object.attack_shape);
	
	StreamToValue(S,Object.dx);
	StreamToValue(S,Object.dy);
	StreamToValue(S,Object.dz);
}

inline void AttackDefToStream(uint8* &S, attack_definition& Object)
{
	ValueToStream(S,Object.type);
	ValueToStream(S,Object.repetitions);
	ValueToStream(S,Object.error);
	ValueToStream(S,Object.range);
	ValueToStream(S,Object.attack_shape);
	
	ValueToStream(S,Object.dx);
	ValueToStream(S,Object.dy);
	ValueToStream(S,Object.dz);
}


uint8 *unpack_monster_definition(uint8 *Stream, size_t Count)
{
	return unpack_monster_definition(Stream,monster_definitions,Count);
}

uint8 *unpack_monster_definition(uint8 *Stream, monster_definition* Objects, size_t Count)
{
	uint8* S = Stream;
	monster_definition* ObjPtr = Objects;
	
	for (size_t k = 0; k < Count; k++, ObjPtr++)
	{
		StreamToValue(S,ObjPtr->collection);
		
		StreamToValue(S,ObjPtr->vitality);
		StreamToValue(S,ObjPtr->immunities);
		StreamToValue(S,ObjPtr->weaknesses);
		StreamToValue(S,ObjPtr->flags);
		
		StreamToValue(S,ObjPtr->_class);
		StreamToValue(S,ObjPtr->friends);
		StreamToValue(S,ObjPtr->enemies);
		
		StreamToValue(S,ObjPtr->sound_pitch);
		StreamToValue(S,ObjPtr->activation_sound);
		StreamToValue(S,ObjPtr->friendly_activation_sound);
		StreamToValue(S,ObjPtr->clear_sound);
		StreamToValue(S,ObjPtr->kill_sound);
		StreamToValue(S,ObjPtr->apology_sound);
		StreamToValue(S,ObjPtr->friendly_fire_sound);
		StreamToValue(S,ObjPtr->flaming_sound);
		StreamToValue(S,ObjPtr->random_sound);
		StreamToValue(S,ObjPtr->random_sound_mask);
		
		StreamToValue(S,ObjPtr->carrying_item_type);
		
		StreamToValue(S,ObjPtr->radius);
		StreamToValue(S,ObjPtr->height);
		StreamToValue(S,ObjPtr->preferred_hover_height);	
		StreamToValue(S,ObjPtr->minimum_ledge_delta);
		StreamToValue(S,ObjPtr->maximum_ledge_delta);
		StreamToValue(S,ObjPtr->external_velocity_scale);
		StreamToValue(S,ObjPtr->impact_effect);
		StreamToValue(S,ObjPtr->melee_impact_effect);
		StreamToValue(S,ObjPtr->contrail_effect);
		
		StreamToValue(S,ObjPtr->half_visual_arc);
		StreamToValue(S,ObjPtr->half_vertical_visual_arc);
		StreamToValue(S,ObjPtr->visual_range);	
		StreamToValue(S,ObjPtr->dark_visual_range);
		StreamToValue(S,ObjPtr->intelligence);
		StreamToValue(S,ObjPtr->speed);
		StreamToValue(S,ObjPtr->gravity);
		StreamToValue(S,ObjPtr->terminal_velocity);
		StreamToValue(S,ObjPtr->door_retry_mask);
		StreamToValue(S,ObjPtr->shrapnel_radius);
		S = unpack_damage_definition(S,&ObjPtr->shrapnel_damage,1);
		
		StreamToValue(S,ObjPtr->hit_shapes);
		StreamToValue(S,ObjPtr->hard_dying_shape);
		StreamToValue(S,ObjPtr->soft_dying_shape);
		StreamToValue(S,ObjPtr->hard_dead_shapes);
		StreamToValue(S,ObjPtr->soft_dead_shapes);
		StreamToValue(S,ObjPtr->stationary_shape);
		StreamToValue(S,ObjPtr->moving_shape);
		StreamToValue(S,ObjPtr->teleport_in_shape);
		StreamToValue(S,ObjPtr->teleport_out_shape);
		
		StreamToValue(S,ObjPtr->attack_frequency);
		StreamToAttackDef(S,ObjPtr->melee_attack);
		StreamToAttackDef(S,ObjPtr->ranged_attack);
	}
	
	assert((S - Stream) == static_cast<ptrdiff_t>(Count*SIZEOF_monster_definition));
	return S;
}

uint8* unpack_m1_monster_definition(uint8 *Stream, size_t Count)
{
	uint8* S = Stream;
	monster_definition* ObjPtr = monster_definitions;

	for (size_t k = 0; k < Count; k++, ObjPtr++)
	{
		StreamToValue(S, ObjPtr->collection);

		StreamToValue(S, ObjPtr->vitality);
		StreamToValue(S, ObjPtr->immunities);
		StreamToValue(S, ObjPtr->weaknesses);
		StreamToValue(S, ObjPtr->flags);

		StreamToValue(S, ObjPtr->_class);
		StreamToValue(S, ObjPtr->friends);
		StreamToValue(S, ObjPtr->enemies);
		
		ObjPtr->sound_pitch = FIXED_ONE;
		StreamToValue(S, ObjPtr->activation_sound);
		S += 2; // ignore conversation sound

		// Marathon doesn't have these
		ObjPtr->friendly_activation_sound = NONE;
		ObjPtr->clear_sound = NONE;
		ObjPtr->kill_sound = NONE;
		ObjPtr->apology_sound = NONE;
		ObjPtr->friendly_fire_sound = NONE;
		
		StreamToValue(S, ObjPtr->flaming_sound);
		StreamToValue(S, ObjPtr->random_sound);
		StreamToValue(S, ObjPtr->random_sound_mask);

		StreamToValue(S, ObjPtr->carrying_item_type);

		StreamToValue(S, ObjPtr->radius);
		StreamToValue(S, ObjPtr->height);
		StreamToValue(S, ObjPtr->preferred_hover_height);
		StreamToValue(S, ObjPtr->minimum_ledge_delta);
		StreamToValue(S, ObjPtr->maximum_ledge_delta);
		StreamToValue(S, ObjPtr->external_velocity_scale);

		StreamToValue(S, ObjPtr->impact_effect);
		StreamToValue(S, ObjPtr->melee_impact_effect);
		ObjPtr->contrail_effect = NONE;
		
		StreamToValue(S,ObjPtr->half_visual_arc);
		StreamToValue(S,ObjPtr->half_vertical_visual_arc);
		StreamToValue(S,ObjPtr->visual_range);	
		StreamToValue(S,ObjPtr->dark_visual_range);
		StreamToValue(S,ObjPtr->intelligence);
		StreamToValue(S,ObjPtr->speed);
		StreamToValue(S,ObjPtr->gravity);
		StreamToValue(S,ObjPtr->terminal_velocity);
		StreamToValue(S,ObjPtr->door_retry_mask);
		StreamToValue(S,ObjPtr->shrapnel_radius);

		S = unpack_damage_definition(S, &ObjPtr->shrapnel_damage, 1);

		StreamToValue(S,ObjPtr->hit_shapes);
		StreamToValue(S,ObjPtr->hard_dying_shape);
		StreamToValue(S,ObjPtr->soft_dying_shape);
		StreamToValue(S,ObjPtr->hard_dead_shapes);
		StreamToValue(S,ObjPtr->soft_dead_shapes);
		StreamToValue(S,ObjPtr->stationary_shape);
		StreamToValue(S,ObjPtr->moving_shape);

		ObjPtr->teleport_in_shape = ObjPtr->stationary_shape;
		ObjPtr->teleport_out_shape = ObjPtr->teleport_out_shape;

		StreamToValue(S, ObjPtr->attack_frequency);
		StreamToAttackDef(S, ObjPtr->melee_attack);
		StreamToAttackDef(S, ObjPtr->ranged_attack);

		ObjPtr->flags |= _monster_weaknesses_cause_soft_death;
		ObjPtr->flags |= _monster_screams_when_crushed;
		ObjPtr->flags |= _monster_makes_sound_when_activated;
		ObjPtr->flags |= _monster_can_grenade_climb;
	}

	return S;
}

uint8 *pack_monster_definition(uint8 *Stream, size_t Count)
{
	return pack_monster_definition(Stream,monster_definitions,Count);
}

uint8 *pack_monster_definition(uint8 *Stream, monster_definition *Objects, size_t Count)
{
	uint8* S = Stream;
	monster_definition* ObjPtr = Objects;
	
	for (size_t k = 0; k < Count; k++, ObjPtr++)
	{
		ValueToStream(S,ObjPtr->collection);
		
		ValueToStream(S,ObjPtr->vitality);
		ValueToStream(S,ObjPtr->immunities);
		ValueToStream(S,ObjPtr->weaknesses);
		ValueToStream(S,ObjPtr->flags);
		
		ValueToStream(S,ObjPtr->_class);
		ValueToStream(S,ObjPtr->friends);
		ValueToStream(S,ObjPtr->enemies);
		
		ValueToStream(S,ObjPtr->sound_pitch);
		ValueToStream(S,ObjPtr->activation_sound);
		ValueToStream(S,ObjPtr->friendly_activation_sound);
		ValueToStream(S,ObjPtr->clear_sound);
		ValueToStream(S,ObjPtr->kill_sound);
		ValueToStream(S,ObjPtr->apology_sound);
		ValueToStream(S,ObjPtr->friendly_fire_sound);
		ValueToStream(S,ObjPtr->flaming_sound);
		ValueToStream(S,ObjPtr->random_sound);
		ValueToStream(S,ObjPtr->random_sound_mask);
		
		ValueToStream(S,ObjPtr->carrying_item_type);
		
		ValueToStream(S,ObjPtr->radius);
		ValueToStream(S,ObjPtr->height);
		ValueToStream(S,ObjPtr->preferred_hover_height);	
		ValueToStream(S,ObjPtr->minimum_ledge_delta);
		ValueToStream(S,ObjPtr->maximum_ledge_delta);
		ValueToStream(S,ObjPtr->external_velocity_scale);
		ValueToStream(S,ObjPtr->impact_effect);
		ValueToStream(S,ObjPtr->melee_impact_effect);
		ValueToStream(S,ObjPtr->contrail_effect);
		
		ValueToStream(S,ObjPtr->half_visual_arc);
		ValueToStream(S,ObjPtr->half_vertical_visual_arc);
		ValueToStream(S,ObjPtr->visual_range);	
		ValueToStream(S,ObjPtr->dark_visual_range);
		ValueToStream(S,ObjPtr->intelligence);
		ValueToStream(S,ObjPtr->speed);
		ValueToStream(S,ObjPtr->gravity);
		ValueToStream(S,ObjPtr->terminal_velocity);
		ValueToStream(S,ObjPtr->door_retry_mask);
		ValueToStream(S,ObjPtr->shrapnel_radius);
		S = pack_damage_definition(S,&ObjPtr->shrapnel_damage,1);
		
		ValueToStream(S,ObjPtr->hit_shapes);
		ValueToStream(S,ObjPtr->hard_dying_shape);
		ValueToStream(S,ObjPtr->soft_dying_shape);
		ValueToStream(S,ObjPtr->hard_dead_shapes);
		ValueToStream(S,ObjPtr->soft_dead_shapes);
		ValueToStream(S,ObjPtr->stationary_shape);
		ValueToStream(S,ObjPtr->moving_shape);
		ValueToStream(S,ObjPtr->teleport_in_shape);
		ValueToStream(S,ObjPtr->teleport_out_shape);
		
		ValueToStream(S,ObjPtr->attack_frequency);
		AttackDefToStream(S,ObjPtr->melee_attack);
		AttackDefToStream(S,ObjPtr->ranged_attack);
	}
	
	assert((S - Stream) == static_cast<ptrdiff_t>(Count*SIZEOF_monster_definition));
	return S;
}

void init_monster_definitions()
{
	memcpy(monster_definitions, original_monster_definitions, sizeof(monster_definitions));
}

struct damage_kick_definition *original_damage_kick_definitions = NULL;

void reset_mml_damage_kicks()
{
	if (original_damage_kick_definitions) {
		for (unsigned i = 0; i < NUMBER_OF_DAMAGE_TYPES; i++)
			damage_kick_definitions[i] = original_damage_kick_definitions[i];
		free(original_damage_kick_definitions);
		original_damage_kick_definitions = NULL;
	}
}

void parse_mml_damage_kicks(const InfoTree& root)
{
	// back up old values first
	if (!original_damage_kick_definitions) {
		original_damage_kick_definitions = (struct damage_kick_definition *) malloc(sizeof(struct damage_kick_definition) * NUMBER_OF_DAMAGE_TYPES);
		assert(original_damage_kick_definitions);
		for (unsigned i = 0; i < NUMBER_OF_DAMAGE_TYPES; i++)
			original_damage_kick_definitions[i] = damage_kick_definitions[i];
	}
	
	for (const InfoTree &kick : root.children_named("kick"))
	{
		int16 index;
		if (!kick.read_indexed("index", index, NUMBER_OF_DAMAGE_TYPES))
			continue;
		damage_kick_definition& def = damage_kick_definitions[index];
		
		kick.read_attr("base", def.base_value);
		kick.read_attr("mult", def.delta_vitality_multiplier);
		kick.read_attr("vertical", def.is_also_vertical);
		kick.read_attr("death_action", def.death_action);
	}
}

void reset_mml_monsters()
{
	monster_must_be_exterminated.clear();
	monster_must_be_exterminated.resize(NUMBER_OF_MONSTER_TYPES, false);
}

void parse_mml_monsters(const InfoTree& root)
{
	for (const InfoTree &monster : root.children_named("monster"))
	{
		int16 index;
		if (!monster.read_indexed("index", index, NUMBER_OF_MONSTER_TYPES))
			continue;
		bool exterminate;
		if (monster.read_attr("must_be_exterminated", exterminate))
			monster_must_be_exterminated[index] = exterminate;
	}
}
