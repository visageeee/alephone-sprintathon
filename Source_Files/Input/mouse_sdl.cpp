/*

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

*/

/*
 *  mouse_sdl.cpp - Mouse handling, SDL specific implementation
 *
 *  May 16, 2002 (Woody Zenfell):
 *      Configurable mouse sensitivity
 *      Semi-hacky scheme to let mouse buttons simulate keypresses
 */

#include "cseries.h"
#include <math.h>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include "interface.h"
#include "../Videos/Movie.h"
#include "vbl.h"

#include "mouse.h"
#include "player.h"
#include "shell.h"
#include "preferences.h"
#include "screen.h"

// Global variables
static bool mouse_active = false;
static uint8 button_mask = 0;		// Mask of enabled buttons
static fixed_yaw_pitch mouselook_delta = {0, 0};
static fixed_yaw_pitch previous_sprintathon_mouselook_delta = {0, 0};
static _fixed snapshot_delta_scrollwheel;
static int snapshot_delta_x, snapshot_delta_y;
static fixed_yaw_pitch frame_mouse_pending = {0, 0};
static double frame_mouse_lag_x = 0, frame_mouse_lag_y = 0;
static double frame_mouse_remainder_x = 0, frame_mouse_remainder_y = 0;
static Uint64 frame_mouse_time = 0;
static bool frame_mouse_was_enabled = false;

bool sprintathon_frame_mouse_enabled()
{
    static const bool requested = [] {
        const char* value = std::getenv("SPRINTATHON_FRAME_MOUSE");
        return !value || value[0] != '0';
    }();
    return requested && mouse_active && input_preferences->sprintathon_enabled &&
        input_preferences->input_device == _mouse_yaw_pitch &&
        input_preferences->mouse_accel_type == _mouse_accel_none &&
        input_preferences->extra_mouse_precision &&
        !game_is_networked && !game_is_being_replayed() &&
        !Movie::instance()->IsRecording() && get_keyboard_controller_status() &&
        local_player && !PLAYER_IS_DEAD(local_player);
}

static void reset_frame_mouse()
{
    frame_mouse_pending = {0, 0};
    frame_mouse_lag_x = frame_mouse_lag_y = 0;
    frame_mouse_remainder_x = frame_mouse_remainder_y = 0;
    frame_mouse_time = SDL_GetPerformanceCounter();
    frame_mouse_was_enabled = false;
}

// Exact first-order integration for movement spread over this sampling interval.
// Unlike a fixed blend per frame, a constant mouse speed has the same response
// at 60, 144, or variable FPS. Retain sub-fixed-unit rounding error.
static fixed_angle filter_frame_axis(double delta, double dt, double tau,
                                    double& lag, double& remainder)
{
    if (tau <= 0)
    {
        const double output = lag + delta + remainder;
        const fixed_angle result = static_cast<fixed_angle>(output);
        remainder = output - result;
        lag = 0;
        return result;
    }
    const double retained = exp(-dt / tau);
    const double new_lag = retained * lag + delta * (tau / dt) * (1 - retained);
    const double output = lag + delta - new_lag + remainder;
    const fixed_angle result = static_cast<fixed_angle>(output);
    remainder = output - result;
    lag = new_lag;
    return result;
}

void sprintathon_update_frame_mouse()
{
    const Uint64 now = SDL_GetPerformanceCounter();
    if (!sprintathon_frame_mouse_enabled())
    {
        reset_frame_mouse();
        return;
    }
    const bool just_enabled = !frame_mouse_was_enabled;
    if (just_enabled)
    {
        previous_sprintathon_mouselook_delta = {0, 0};
        frame_mouse_was_enabled = true;
    }
    const double dt = double(now - frame_mouse_time) / SDL_GetPerformanceFrequency();
    frame_mouse_time = now;
    if (dt <= 0) return;
    const double tau = std::max(0, std::min(40,
        int(input_preferences->sprintathon_mouse_smoothing_ms))) / 1000.0;
    if (just_enabled)
        if (std::getenv("SPRINTATHON_DIAGNOSTICS") && std::getenv("SPRINTATHON_DIAGNOSTICS")[0]=='1') std::fprintf(stderr, "Sprintathon frame mouse: active, %.1f ms smoothing\n", tau * 1000.0);
    double dx = snapshot_delta_x;
    double dy = -snapshot_delta_y;
    snapshot_delta_x = snapshot_delta_y = 0;
    if (TEST_FLAG(input_preferences->modifiers, _inputmod_invert_mouse)) dy = -dy;
    dx *= (128 / 66.0) * input_preferences->sens_horizontal;
    dy *= (128 / 66.0) * input_preferences->sens_vertical *
        (input_preferences->classic_vertical_aim ? 0.25 : 1.0);
    frame_mouse_pending.yaw += filter_frame_axis(dx, dt, tau,
        frame_mouse_lag_x, frame_mouse_remainder_x);
    frame_mouse_pending.pitch += filter_frame_axis(dy, dt, tau,
        frame_mouse_lag_y, frame_mouse_remainder_y);
}

fixed_yaw_pitch sprintathon_pending_frame_mouse()
{
    return frame_mouse_pending;
}



/*
 *  Initialize in-game mouse handling
 */

void enter_mouse(short type)
{
	if (type != _keyboard_or_game_pad) {
		MainScreenCenterMouse();
		
		SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, input_preferences->raw_mouse_input ? "0" : "1");
		SDL_SetRelativeMouseMode(SDL_TRUE);
		mouse_active = true;
		reset_frame_mouse();
		mouselook_delta = {0, 0};
		previous_sprintathon_mouselook_delta = {0, 0};
		snapshot_delta_scrollwheel = 0;
		snapshot_delta_x = snapshot_delta_y = 0;
		button_mask = 0;	// Disable all buttons (so a shot won't be fired if we enter the game with a mouse button down from clicking a GUI widget)
	}
}


/*
 *  Shutdown in-game mouse handling
 */

void exit_mouse(short type)
{
	if (type != _keyboard_or_game_pad) {
		SDL_SetRelativeMouseMode(SDL_FALSE);
		mouse_active = false;
		reset_frame_mouse();
	}
}


/*
 *  Calculate new center mouse position when screen size has changed
 */

void recenter_mouse(void)
{
	if (mouse_active) {
		MainScreenCenterMouse();
	}
}

static inline float MIX(float start, float end, float factor)
{
	return (start * (1.f - factor)) + (end * factor);
}

/*
 *  Take a snapshot of the current mouse state
 */

void mouse_idle(short type)
{
    if (sprintathon_frame_mouse_enabled())
    {
        mouselook_delta = frame_mouse_pending;
        frame_mouse_pending = {0, 0};
        return;
    }
	if (mouse_active) {

		// Calculate axis deltas
		float dx = snapshot_delta_x;
		float dy = -snapshot_delta_y;
		snapshot_delta_x = 0;
		snapshot_delta_y = 0;
		
		// Mouse inversion
		if (TEST_FLAG(input_preferences->modifiers, _inputmod_invert_mouse))
			dy = -dy;
		
		// Delta sensitivities
		const float angle_per_scaled_delta = 128/66.f; // assuming _mouse_accel_none
		float sx = angle_per_scaled_delta * (input_preferences->sens_horizontal / float{FIXED_ONE});
		float sy = angle_per_scaled_delta * (input_preferences->sens_vertical / float{FIXED_ONE}) * (input_preferences->classic_vertical_aim ? 0.25f : 1.f);
		switch (input_preferences->mouse_accel_type)
		{
			case _mouse_accel_classic:
				sx *= MIX(1.f, (1/32.f) * fabs(dx * sx), input_preferences->mouse_accel_scale);
				sy *= MIX(1.f, (1/(input_preferences->classic_vertical_aim ? 8.f : 32.f)) * fabs(dy * sy), input_preferences->mouse_accel_scale);
				break;
			case _mouse_accel_none:
			default:
				break;
		}
		
		// Angular deltas
		fixed_angle dyaw = static_cast<fixed_angle>(sx * dx * FIXED_ONE);
		fixed_angle dpitch = static_cast<fixed_angle>(sy * dy * FIXED_ONE);

		/*
		 * Soften changes in mouse velocity between 30 Hz simulation samples.
		 * This short, total-preserving filter works with render interpolation
		 * without giving Sprintathon mouselook a long floaty tail.
		 */
		if (input_preferences->sprintathon_enabled)
		{
			const fixed_yaw_pitch raw_delta = {dyaw, dpitch};
			dyaw = static_cast<fixed_angle>(
				(3LL*dyaw + previous_sprintathon_mouselook_delta.yaw)/4);
			dpitch = static_cast<fixed_angle>(
				(3LL*dpitch + previous_sprintathon_mouselook_delta.pitch)/4);
			previous_sprintathon_mouselook_delta = raw_delta;
		}
		else
		{
			previous_sprintathon_mouselook_delta = {0, 0};
		}
		
		// Push mouselook delta
		mouselook_delta = {dyaw, dpitch};
	}
}

fixed_yaw_pitch pull_mouselook_delta()
{
	auto delta = mouselook_delta;
	mouselook_delta = {0, 0};
	return delta;
}


void
mouse_buttons_become_keypresses(Uint8* ioKeyMap)
{
		uint8 buttons = SDL_GetMouseState(NULL, NULL);
		uint8 orig_buttons = buttons;
		buttons &= button_mask;				// Mask out disabled buttons

        for(int i = 0; i < NUM_SDL_REAL_MOUSE_BUTTONS; i++) {
            ioKeyMap[AO_SCANCODE_BASE_MOUSE_BUTTON + i] =
                (buttons & SDL_BUTTON(i+1)) ? SDL_PRESSED : SDL_RELEASED;
        }
		ioKeyMap[AO_SCANCODE_MOUSESCROLL_UP] = (snapshot_delta_scrollwheel > 0) ? SDL_PRESSED : SDL_RELEASED;
		ioKeyMap[AO_SCANCODE_MOUSESCROLL_DOWN] = (snapshot_delta_scrollwheel < 0) ? SDL_PRESSED : SDL_RELEASED;
		snapshot_delta_scrollwheel = 0;

        button_mask |= ~orig_buttons;		// A button must be released at least once to become enabled
}

/*
 *  Hide/show mouse pointer
 */

void hide_cursor(void)
{
	SDL_ShowCursor(0);
}

void show_cursor(void)
{
	SDL_ShowCursor(1);
}


void mouse_scroll(bool up)
{
	if (up)
		snapshot_delta_scrollwheel += 1;
	else
		snapshot_delta_scrollwheel -= 1;
}

void mouse_moved(int delta_x, int delta_y)
{
	snapshot_delta_x += delta_x;
	snapshot_delta_y += delta_y;
}
