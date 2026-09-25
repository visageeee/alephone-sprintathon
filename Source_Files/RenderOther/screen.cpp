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
 *  screen_sdl.cpp - Screen management, SDL implementation
 *
 *  Written in 2000 by Christian Bauer
 * 
 *  Loren Petrich, Dec 23, 2000; moved shared content into screen_shared.cpp
 */
#include "cseries.h"

#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_OPENGL
#include "OGL_Headers.h"
#include "OGL_Blitter.h"
#include "OGL_Faders.h"
#include "OGL_Textures.h"
#endif

#include "world.h"
#include "map.h"
#include "render.h"
#include "shell.h"
#include "interface.h"
#include "interpolated_world.h"
#include "player.h"
#include "overhead_map.h"
#include "fades.h"
#include "game_window.h"
#include "screen.h"
#include "preferences.h"
#include "computer_interface.h"
#include "Crosshairs.h"
#include "OGL_Render.h"
#include "ViewControl.h"
#include "screen_drawing.h"
#include "mouse.h"
#include "network.h"
#include "images.h"
#include "motion_sensor.h"
#include "Logging.h"

#include "sdl_fonts.h"

#include "lua_script.h"
#include "lua_hud_script.h"
#include "HUDRenderer_Lua.h"
#include "Movie.h"
#include "shell_options.h"

#include <algorithm>
#include <cmath>

#if defined(__WIN32__) || (defined(__MACH__) && defined(__APPLE__))
#define MUST_RELOAD_VIEW_CONTEXT
#endif

// Global variables
static SDL_Surface *main_surface;	// Main (display) surface
static SDL_Window *main_screen;
static SDL_Renderer *main_render;
static SDL_Texture *main_texture;

// Rendering buffer for the main view, the overhead map, and the terminals.
// The HUD has a separate buffer.
// It is initialized to NULL so as to allow its initing to be lazy.
SDL_Surface *world_pixels = NULL;
SDL_Surface *world_pixels_corrected = NULL;
SDL_Surface *HUD_Buffer = NULL;
SDL_Surface *Term_Buffer = NULL;
SDL_Surface *Intro_Buffer = NULL; // intro screens, main menu, chapters, credits, etc.
SDL_Surface *Intro_Buffer_corrected = NULL;
bool intro_buffer_changed = false;
SDL_Surface *Map_Buffer = NULL;

// A bitmap_definition view of world_pixels for software rendering
static bitmap_definition_buffer software_render_dest;

#ifdef HAVE_OPENGL
static OGL_Blitter Term_Blitter;
static OGL_Blitter Intro_Blitter;
#endif

// Initial gamma table
bool default_gamma_inited = false;
uint16 default_gamma_r[256];
uint16 default_gamma_g[256];
uint16 default_gamma_b[256];
uint16 current_gamma_r[256];
uint16 current_gamma_g[256];
uint16 current_gamma_b[256];
bool using_default_gamma = true;

static bool PrevFullscreen = false;
static bool in_game = false;	// Flag: menu (fixed 640x480) or in-game (variable size) display

static int failed_multisamples = 0;		// remember when GL multisample setting didn't succeed
static bool passed_shader = false;      // remember when we passed Shader tests

#include "screen_shared.h"

using namespace alephone;

Screen Screen::m_instance;


// Prototypes
static bool need_mode_change(int window_width, int window_height, int log_width, int log_height, int depth, bool nogl);
static void change_screen_mode(int width, int height, int depth, bool nogl, bool force_menu, bool force_resize_hud = false);
static bool get_auto_resolution_size(short *w, short *h, struct screen_mode_data *mode);
static void build_sdl_color_table(const color_table *color_table, SDL_Color *colors);
static void reallocate_world_pixels(int width, int height);
static void reallocate_map_pixels(int width, int height);
static void apply_gamma(SDL_Surface *src, SDL_Surface *dst);
static void update_screen(SDL_Rect &source, SDL_Rect &destination, bool hi_rez, bool every_other_line);
static void update_fps_display(SDL_Surface *s);
static void DisplayPosition(SDL_Surface *s);
static void DisplayMessages(SDL_Surface *s);
static void DrawSurface(SDL_Surface *s, SDL_Rect &dest_rect, SDL_Rect &src_rect);
static void clear_screen_margin();
#ifdef HAVE_OPENGL
static void draw_sprintathon_bullet_time_effect();
#endif

SDL_PixelFormat pixel_format_16, pixel_format_32;

static bitmap_definition_buffer bitmap_definition_of_sdl_surface(const SDL_Surface* surface)
{
	assert(surface);
	bitmap_definition_buffer buf(/*row_count:*/ surface->h);
	auto& def = *buf.get();
	def.width = surface->w;
	def.height = surface->h;
	def.bytes_per_row = surface->pitch;
	def.flags = 0;
	def.bit_depth = surface->format->BitsPerPixel;
	def.row_addresses[0] = static_cast<pixel8*>(surface->pixels);
	precalculate_bitmap_row_addresses(&def);
	return buf;
}

// LP addition:
void start_tunnel_vision_effect(
	bool out)
{
	// LP change: doing this by setting targets
  world_view->target_field_of_view = (out && NetAllowTunnelVision()) ? TUNNEL_VISION_FIELD_OF_VIEW : 
		((current_player->extravision_duration) ? EXTRAVISION_FIELD_OF_VIEW : NORMAL_FIELD_OF_VIEW);
}

/*
 *  Initialize screen management
 */

void Screen::Initialize(screen_mode_data* mode)
//void initialize_screen(struct screen_mode_data *mode, bool ShowFreqDialog)
{
	interface_bit_depth = bit_depth = mode->bit_depth;

	if (!screen_initialized) {

		SDL_PixelFormat *pf = SDL_AllocFormat(SDL_PIXELFORMAT_RGB565);
		pixel_format_16 = *pf;
		SDL_FreeFormat(pf);
		pf = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
		pixel_format_32 = *pf;
		SDL_FreeFormat(pf);

		Intro_Buffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 32, pixel_format_32.Rmask, pixel_format_32.Gmask, pixel_format_32.Bmask, 0);
		Intro_Buffer_corrected = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 32, pixel_format_32.Rmask, pixel_format_32.Gmask, pixel_format_32.Bmask, 0);

		SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

		uncorrected_color_table = (struct color_table *)malloc(sizeof(struct color_table));
		world_color_table = (struct color_table *)malloc(sizeof(struct color_table));
		visible_color_table = (struct color_table *)malloc(sizeof(struct color_table));
		interface_color_table = (struct color_table *)malloc(sizeof(struct color_table));
		assert(uncorrected_color_table && world_color_table && visible_color_table && interface_color_table);
		memset(uncorrected_color_table, 0, sizeof(struct color_table));
		memset(world_color_table, 0, sizeof(struct color_table));
		memset(visible_color_table, 0, sizeof(struct color_table));
		memset(interface_color_table, 0, sizeof(struct color_table));

		// Allocate and initialize our view_data structure
		world_view = (struct view_data *)malloc(sizeof(struct view_data));
		assert(world_view);
		world_view->field_of_view = NORMAL_FIELD_OF_VIEW; // degrees (was 74 for a long, long time)
		world_view->target_field_of_view = NORMAL_FIELD_OF_VIEW; // for no change in FOV
		world_view->overhead_map_scale = DEFAULT_OVERHEAD_MAP_SCALE;
		world_view->overhead_map_active = false;
		world_view->terminal_mode_active = false;
		world_view->horizontal_scale = 1;
		world_view->vertical_scale = 1;
		world_view->tunnel_vision_active = false;
		
		m_modes.clear();
		SDL_DisplayMode desktop;
		if (SDL_GetDesktopDisplayMode(0, &desktop) == 0)
		{
			if (desktop.w >= 640 && desktop.h >= 480)
			{
				m_modes.push_back(std::pair<int, int>(desktop.w, desktop.h));
			}
		}
		if (m_modes.empty())
		{
			// assume a decent screen size
			m_modes.push_back(std::pair<int, int>(1600, 900));
		}
		
		if (1)
		{
			// insert some choices for windowed mode
			std::vector<std::pair<int, int> > common_modes;
			common_modes.push_back(std::pair<int, int>(1920, 1080));
			common_modes.push_back(std::pair<int, int>(1600, 900));
			common_modes.push_back(std::pair<int, int>(1200, 900));
			common_modes.push_back(std::pair<int, int>(1366, 768));
			common_modes.push_back(std::pair<int, int>(1280, 720));
			common_modes.push_back(std::pair<int, int>(960, 720));
			common_modes.push_back(std::pair<int, int>(800, 600));
			common_modes.push_back(std::pair<int, int>(640, 480));
			common_modes.push_back(std::pair<int, int>(480, 240));
			common_modes.push_back(std::pair<int, int>(320, 160));
			
			for (std::vector<std::pair<int, int> >::const_iterator it = common_modes.begin(); it != common_modes.end(); ++it)
			{
				if (it->first <= m_modes[0].first && it->second <= m_modes[0].second && !(it->first == m_modes[0].first && it->second == m_modes[0].second))
				{
					m_modes.push_back(*it);
				}
			}
		}

		// insert custom mode if it's in the prefs
		if (graphics_preferences->screen_mode.width <= m_modes[0].first && graphics_preferences->screen_mode.height <= m_modes[0].second)
		{
			// sort it into the list
			for (std::vector<std::pair<int, int> >::iterator it = m_modes.begin(); it != m_modes.end(); ++it)
			{
				if (graphics_preferences->screen_mode.width >= it->first && graphics_preferences->screen_mode.height >= it->second)
				{
					if (graphics_preferences->screen_mode.width != it->first || graphics_preferences->screen_mode.height != it->second)
					{
						m_modes.insert(it, std::pair<int, int>(graphics_preferences->screen_mode.width, graphics_preferences->screen_mode.height));
					}
					break;
				}
			}
		}

		// these are not validated in graphics prefs because
		// SDL is not initialized yet when prefs load, so
		// validate them here
		if (Screen::instance()->FindMode(graphics_preferences->screen_mode.width, graphics_preferences->screen_mode.height) < 0)
		{
			graphics_preferences->screen_mode.width = 640;
			graphics_preferences->screen_mode.height = 480;
			write_preferences();
		}
	} else {

		unload_all_collections();
		if (world_pixels)
			SDL_FreeSurface(world_pixels);
		if (world_pixels_corrected)
			SDL_FreeSurface(world_pixels_corrected);
	}
	world_pixels = NULL;
	world_pixels_corrected = NULL;

	screen_mode = *mode;
	change_screen_mode(&screen_mode, true);
	screen_initialized = true;

}

int Screen::height()
{
	return MainScreenLogicalHeight();
}

int Screen::width()
{
	return MainScreenLogicalWidth();
}

float Screen::pixel_scale()
{
	return MainScreenPixelScale();
}

int Screen::window_height()
{
	return std::max(static_cast<short>(480), screen_mode.height);
}

int Screen::window_width()
{
	return std::max(static_cast<short>(640), screen_mode.width);
}

bool Screen::hud()
{
	return screen_mode.hud && !screenshot_mode_active();
}

bool Screen::lua_hud()
{
	return screen_mode.hud && !screenshot_mode_active() && LuaHUDRunning();
}

bool Screen::openGL()
{
	return screen_mode.acceleration != _no_acceleration;
}

bool Screen::fifty_percent()
{
	return screen_mode.height == 160;
}

bool Screen::seventyfive_percent()
{
	return screen_mode.height == 240;
}

SDL_Rect Screen::window_rect()
{
	SDL_Rect r;
	r.w = window_width();
	r.h = window_height();
	r.x = (width() - r.w) / 2;
	r.y = (height() - r.h) / 2;
	return r;
}

SDL_Rect Screen::OpenGLViewPort()
{
	return m_viewport_rect;
}

SDL_Rect Screen::view_rect()
{
	SDL_Rect r;
	if (lua_hud())
	{
		r.x = lua_view_rect.x + (width() - window_width()) / 2;
		r.y = lua_view_rect.y + (height() - window_height()) / 2;
		r.w = MIN(lua_view_rect.w, window_width() - lua_view_rect.x);
		r.h = MIN(lua_view_rect.h, window_height() - lua_view_rect.y);
	}
	else if (!hud())
	{
		r.x = (width() - window_width()) / 2;
		r.y = (height() - window_height()) / 2;
		r.w = window_width();
		r.h = window_height();
	}
	else
	{
		int available_height = window_height() - hud_rect().h;
		if (window_width() > available_height * 2)
		{
			r.w = available_height * 2;
			r.h = available_height;
		}
		else
		{
			r.w = window_width();
			r.h = window_width() / 2;
		}
		r.x = (width() - r.w) / 2;
		r.y = (height() - window_height()) / 2 + (available_height - r.h) / 2;
	}

	if (fifty_percent())
	{
		r.y += r.h / 4;
		r.x += r.w / 4;
		r.w /= 2;
		r.h /= 2;
	}
	else if (seventyfive_percent())
	{
		r.y += r.h / 8;
		r.x += r.w / 8;
		r.w = r.w * 3 / 4;
		r.h = r.h * 3 / 4;
	}

	return r;
}

SDL_Rect Screen::map_rect()
{
	SDL_Rect r;
	if (lua_hud())
    {
		r.x = lua_map_rect.x + (width() - window_width()) / 2;
		r.y = lua_map_rect.y + (height() - window_height()) / 2;
		r.w = MIN(lua_map_rect.w, window_width() - lua_map_rect.x);
		r.h = MIN(lua_map_rect.h, window_height() - lua_map_rect.y);
        return r;
    }
	if (map_is_translucent())
		return view_rect();
	
	r.w = window_width();
	r.h = window_height();
	if (hud()) 
		r.h -= hud_rect().h;

	r.x = (width() - window_width()) / 2;
	r.y = (height() - window_height()) / 2;

	return r;
}

SDL_Rect Screen::term_rect()
{
	int wh = window_height();
	int ww = window_width();
	int wx = (width() - ww)/2;
	int wy = (height() - wh)/2;
	
	if (lua_hud())
	{
		wx += lua_term_rect.x;
		wy += lua_term_rect.y;
		ww = MIN(lua_term_rect.w, ww - lua_term_rect.x);
		wh = MIN(lua_term_rect.h, wh - lua_term_rect.y);
	}
	
	SDL_Rect r;
	
	int available_height = wh;
	if (hud() && !lua_hud())
		available_height -= hud_rect().h;
	
	screen_rectangle *term_rect = get_interface_rectangle(_terminal_screen_rect);
	r.w = RECTANGLE_WIDTH(term_rect);
	r.h = RECTANGLE_HEIGHT(term_rect);
	float aspect = r.w / static_cast<float>(r.h);
	switch (screen_mode.term_scale_level)
	{
		case 1:
			if (available_height >= (r.h * 2) && ww >= (r.w * 2))
				r.w *= 2;
			break;
		case 2:
			r.w = std::min(ww, std::max(static_cast<int>(r.w), static_cast<int>(aspect * available_height)));
			break;
	}
	r.h = r.w / aspect;
	r.x = wx + (ww - r.w) / 2;
	r.y = wy + (available_height - r.h) / 2;

	return r;
}

SDL_Rect Screen::hud_rect()
{
	SDL_Rect r;
	r.w = 640;
	switch (screen_mode.hud_scale_level)
	{
		case 1:
			if (window_height() >= 960 && window_width() >= 1280)
				r.w *= 2;
			break;
		case 2:
			r.w = std::min(window_width(), std::max(640, 4 * window_height() / 3));
			break;
	}
	r.h = r.w / 4;
	r.x = (width() - r.w) / 2;
	r.y = window_height() - r.h + (height() - window_height()) / 2;

	return r;
}

void Screen::bound_screen(bool in_game)
{
	SDL_Rect r = { 0, 0, in_game ? window_width() : 640, in_game ? window_height() : 480 };
	bound_screen_to_rect(r, in_game);
}

void Screen::bound_screen_to_rect(SDL_Rect &r, bool in_game)
{
#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL())
	{
		int pixw = MainScreenPixelWidth();
		int pixh = MainScreenPixelHeight();
		int virw = in_game ? window_width() : 640;
		int virh = in_game ? window_height() : 480;
		
		float vscale = MIN(pixw / static_cast<float>(virw), pixh / static_cast<float>(virh));
		int vpw = static_cast<int>(r.w * vscale + 0.5f);
		int vph = static_cast<int>(r.h * vscale + 0.5f);
		int vpx = static_cast<int>(pixw/2.0f - (virw * vscale)/2.0f + (r.x * vscale) + 0.5f);
		int vpy = static_cast<int>(pixh/2.0f - (virh * vscale)/2.0f + (r.y * vscale) + 0.5f);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glViewport(vpx, pixh - vph - vpy, vpw, vph);
		m_viewport_rect.x = vpx;
		m_viewport_rect.y = pixh - vph - vpy;
		m_viewport_rect.w = vpw;
		m_viewport_rect.h = vph;
		glOrtho(0, r.w, r.h, 0, -1.0, 1.0);
		m_ortho_rect.x = m_ortho_rect.y = 0;
		m_ortho_rect.w = r.w;
		m_ortho_rect.h = r.h;
	}
#endif
}

void Screen::scissor_screen_to_rect(SDL_Rect &r)
{
#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL())
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(m_viewport_rect.x + (r.x * m_viewport_rect.w/m_ortho_rect.w),
				  m_viewport_rect.y + ((m_ortho_rect.h - r.y - r.h) * m_viewport_rect.h/m_ortho_rect.h),
				  r.w * m_viewport_rect.w/m_ortho_rect.w,
				  r.h * m_viewport_rect.h/m_ortho_rect.h);
	}
#endif
}


void Screen::window_to_screen(int &x, int &y)
{
#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL())
	{
		int winw = MainScreenWindowWidth();
		int winh = MainScreenWindowHeight();
		int virw = 640;
		int virh = 480;
		
		float wina = winw / static_cast<float>(winh);
		float vira = virw / static_cast<float>(virh);

		if (wina >= vira)
		{
			float scale = winh / static_cast<float>(virh);
			x -= (winw - (virw * scale))/2;
			x /= scale;
			y /= scale;
		}
		else
		{
			float scale = winw / static_cast<float>(virw);
			y -= (winh - (virh * scale))/2;
			x /= scale;
			y /= scale;
		}
	}
#endif
}

/*
 *  (Re)allocate off-screen buffer
 */

static void reallocate_world_pixels(int width, int height)
{
	if (world_pixels) {
		SDL_FreeSurface(world_pixels);
		world_pixels = NULL;
	}
	if (world_pixels_corrected) {
		SDL_FreeSurface(world_pixels_corrected);
		world_pixels_corrected = NULL;
	}

	switch (bit_depth)
	{
	case 8:
		world_pixels = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);
		break;
	case 16:
		world_pixels = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 16, pixel_format_16.Rmask, pixel_format_16.Gmask, pixel_format_16.Bmask, 0);
		break;
	default:
		world_pixels = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32, pixel_format_32.Rmask, pixel_format_32.Gmask, pixel_format_32.Bmask, 0);
		break;

	}

	if (world_pixels == NULL)
		alert_out_of_memory();
	else if (bit_depth == 8) {
		SDL_Color colors[256];
		build_sdl_color_table(world_color_table, colors);
		SDL_SetPaletteColors(world_pixels->format->palette, colors, 0, 256);
	} else
		world_pixels_corrected = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, world_pixels->format->BitsPerPixel, world_pixels->format->Rmask, world_pixels->format->Gmask, world_pixels->format->Bmask, 0);
}

static void reallocate_map_pixels(int width, int height)
{
	if (Map_Buffer) {
		SDL_FreeSurface(Map_Buffer);
		Map_Buffer = NULL;
	}
	Map_Buffer = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, main_surface->format->BitsPerPixel, main_surface->format->Rmask, main_surface->format->Gmask, main_surface->format->Bmask, 0);
	if (Map_Buffer == NULL)
		alert_out_of_memory();
	if (map_is_translucent()) {
		SDL_SetSurfaceAlphaMod(Map_Buffer, 128);
		SDL_SetColorKey(Map_Buffer, SDL_TRUE, SDL_MapRGB(Map_Buffer->format, 0, 0, 0));
	}
}


/*
 *  Force reload of view context
 */

void ReloadViewContext(void)
{
#ifdef HAVE_OPENGL
	if (in_game && screen_mode.acceleration != _no_acceleration)
		OGL_StartRun();
#endif
}

/*
 *  Determine if the transparent map is in use
 *  (may be disallowed for network games)
 */

bool map_is_translucent(void)
{
	return (screen_mode.translucent_map && NetAllowOverlayMap());
}

/*
 *  Enter game screen
 */

void enter_screen(void)
{
	if (world_view->overhead_map_active)
		set_overhead_map_status(false);
	if (world_view->terminal_mode_active)
		set_terminal_status(false);

	// Adding this view-effect resetting here since initialize_world_view() no longer resets it
	world_view->effect = NONE;
	
	// Set screen to selected size
	in_game = true;
	change_screen_mode(_screentype_level);
	PrevFullscreen = screen_mode.fullscreen;

#if defined(HAVE_OPENGL) && !defined(MUST_RELOAD_VIEW_CONTEXT)
	// if MUST_RELOAD_VIEW_CONTEXT, we know this just happened in
	// change_screen_mode
	if (screen_mode.acceleration != _no_acceleration)
		OGL_StartRun();
#endif

	// Reset modifier key status
	SDL_SetModState(KMOD_NONE);
	
	Screen *scr = Screen::instance();
	int w = scr->width();
	int h = scr->height();
	int ww = scr->window_width();
	int wh = scr->window_height();
	
	scr->lua_clip_rect.x = 0;
	scr->lua_clip_rect.y = 0;
	scr->lua_clip_rect.w = w;
	scr->lua_clip_rect.h = h;
	
	scr->lua_view_rect.x = scr->lua_map_rect.x = (w - ww) / 2;
	scr->lua_view_rect.y = scr->lua_map_rect.y = (h - wh) / 2;
	scr->lua_view_rect.w = scr->lua_map_rect.w = ww;
	scr->lua_view_rect.h = scr->lua_map_rect.h = wh;

	scr->lua_text_margins.top = 0;
	scr->lua_text_margins.left = 0;
	scr->lua_text_margins.bottom = 0;
	scr->lua_text_margins.right = 0;
	
    screen_rectangle *term_rect = get_interface_rectangle(_terminal_screen_rect);
	scr->lua_term_rect.x = (w - RECTANGLE_WIDTH(term_rect)) / 2;
	scr->lua_term_rect.y = (h - RECTANGLE_HEIGHT(term_rect)) / 2;
	scr->lua_term_rect.w = RECTANGLE_WIDTH(term_rect);
	scr->lua_term_rect.h = RECTANGLE_HEIGHT(term_rect);

	L_Call_HUDResize();
}


/*
 *  Exit game screen
 */

void exit_screen(void)
{
	in_game = false;
#ifdef HAVE_OPENGL
	OGL_StopRun();
#endif
}


/*
 *  Change screen mode
 */

static bool need_mode_change(int window_width, int window_height,
							 int log_width, int log_height,
							 int depth, bool nogl)
{
	// have we set up any window at all yet?
	if (main_screen == NULL)
		return true;
	
	// are we switching to/from high-dpi?
	bool current_high_dpi = (SDL_GetWindowFlags(main_screen) & SDL_WINDOW_ALLOW_HIGHDPI);
	if (screen_mode.high_dpi != current_high_dpi) {
		return true;
	}
	
	// are we switching to/from OpenGL?
	bool wantgl = false;
	bool hasgl = MainScreenIsOpenGL();
#ifdef HAVE_OPENGL
	wantgl = !nogl && (screen_mode.acceleration != _no_acceleration);
	if (wantgl != hasgl)
		return true;
	if (wantgl) {
		// check GL-specific attributes
		int atval = 0;
		
		int want_samples = Get_OGL_ConfigureData().Multisamples;
		if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &atval) == 0) {
			if (atval != want_samples && !(want_samples == failed_multisamples && atval == 0))
				SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, want_samples);
		}
		
		int want_vsync = Get_OGL_ConfigureData().WaitForVSync ? 1 : 0;
		int has_vsync = SDL_GL_GetSwapInterval();
		if ((has_vsync == 0) != (want_vsync == 0))
			SDL_GL_SetSwapInterval(want_vsync);
	}
#endif
		
	// are we switching to/from fullscreen?
	bool current_fullscreen = (SDL_GetWindowFlags(main_screen) & SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (screen_mode.fullscreen != current_fullscreen) {
		SDL_SetWindowFullscreen(main_screen, screen_mode.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	}
	
	// are we switching resolution?
	if (!screen_mode.fullscreen) {
		int w, h;
		SDL_GetWindowSize(main_screen, &w, &h);
		if (w != window_width || h != window_height) {
			SDL_SetWindowSize(main_screen, window_width, window_height);
			SDL_SetWindowPosition(main_screen, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		}
	}
	if (!hasgl) {
		int w, h;
		SDL_RenderGetLogicalSize(main_render, &w, &h);
		if (w != log_width || h != log_height) {
			SDL_RenderSetLogicalSize(main_render, log_width, log_height);
		}
	}

	// force rebuild of main_surface and/or main_texture, if necessary
	if (main_surface != NULL && (main_surface->w != log_width || main_surface->h != log_height)) {
		SDL_FreeSurface(main_surface);
		main_surface = NULL;
	}
	if (main_texture != NULL) {
		int w, h;
		SDL_QueryTexture(main_texture, NULL, NULL, &w, &h);
		if (w != log_width || h != log_height) {
			SDL_DestroyTexture(main_texture);
			main_texture = NULL;
		}
	}
	
	// reset title, since SDL forgets sometimes
	SDL_SetWindowTitle(main_screen, get_application_name().c_str());
	
	return false;
}

static int change_window_filter(void *ctx, SDL_Event *event)
{
	Uint32 *window_id = static_cast<Uint32 *>(ctx);
	
	if (event->type == SDL_WINDOWEVENT &&
		event->window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
		event->window.windowID == *window_id)
		return 0;
	return 1;
}

static void change_screen_mode(int width, int height, int depth, bool nogl, bool force_menu, bool force_resize_hud)
{
	int prev_width = 0;
	int prev_height = 0;
	if (main_surface)
	{
		prev_width = main_surface->w;
		prev_height = main_surface->h;
	}
	
	int vmode_height = height;
	int vmode_width = width;
	uint32 flags = (screen_mode.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	if (screen_mode.high_dpi)
		flags |= SDL_WINDOW_ALLOW_HIGHDPI;
	
	int sdl_width = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : vmode_width;
	int sdl_height = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : vmode_height;
	if (force_menu)
	{
		vmode_width = 640;
		vmode_height = 480;
	}
	
//#ifdef HAVE_OPENGL
//	if (!context_created && !nogl && screen_mode.acceleration != _no_acceleration) {
//		SDL_GL_CreateContext(main_screen);
//		context_created = true;
//	}
//#endif
//	if (nogl || screen_mode.acceleration == _no_acceleration) {
//		main_render = SDL_CreateRenderer(main_screen, -1, 0);
//		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
//		SDL_RenderSetLogicalSize(main_render, vmode_width, vmode_height);
//		main_texture = SDL_CreateTexture(main_render, pixel_format_32.format, SDL_TEXTUREACCESS_STREAMING, vmode_width, vmode_height);
//	}
//	main_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, vmode_width, vmode_height, 32, pixel_format_32.Rmask, pixel_format_32.Gmask, pixel_format_32.Bmask, 0);

	
	if (nogl || screen_mode.acceleration == _no_acceleration) {
		switch (graphics_preferences->software_sdl_driver) {
			case _sw_driver_none:
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
				break;
			case _sw_driver_opengl:
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
				break;
			case _sw_driver_direct3d:
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
				break;
			case _sw_driver_default:
			default:
				SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
				break;
		}
	}
	
	if (need_mode_change(sdl_width, sdl_height, vmode_width, vmode_height, depth, nogl)) {
#ifdef HAVE_OPENGL
	if (!nogl && screen_mode.acceleration != _no_acceleration) {
		passed_shader = false;
		flags |= SDL_WINDOW_OPENGL;
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		if (Get_OGL_ConfigureData().Multisamples > 0) {
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, Get_OGL_ConfigureData().Multisamples);
		} else {
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		}
		SDL_GL_SetSwapInterval(Get_OGL_ConfigureData().WaitForVSync ? 1 : 0);
	}
#endif 

		
		if (main_surface != NULL) {
			SDL_FreeSurface(main_surface);
			main_surface = NULL;
		}
		if (main_texture != NULL) {
			SDL_DestroyTexture(main_texture);
			main_texture = NULL;
		}
		if (main_render != NULL) {
			SDL_DestroyRenderer(main_render);
			main_render = NULL;
		}
	if (main_screen != NULL) {
		Uint32 window_id = SDL_GetWindowID(main_screen);
	    SDL_DestroyWindow(main_screen);
		main_screen = NULL;
		SDL_FilterEvents(change_window_filter, &window_id);
	}
	main_screen = SDL_CreateWindow(get_application_name().c_str(),
								   SDL_WINDOWPOS_CENTERED,
								   SDL_WINDOWPOS_CENTERED,
								   sdl_width, sdl_height,
								   flags);

	bool context_created = false;
#ifdef HAVE_OPENGL
	if (main_screen == NULL && !nogl && screen_mode.acceleration != _no_acceleration && Get_OGL_ConfigureData().Multisamples > 0) {
		// retry with multisampling off
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		main_screen = SDL_CreateWindow(get_application_name().c_str(),
									   SDL_WINDOWPOS_CENTERED,
									   SDL_WINDOWPOS_CENTERED,
									   sdl_width, sdl_height,
									   flags);
		if (main_screen)
			failed_multisamples = Get_OGL_ConfigureData().Multisamples;
	}
#endif
	if (main_screen == NULL && !nogl && screen_mode.acceleration != _no_acceleration) {
		fprintf(stderr, "WARNING: Failed to initialize OpenGL with 24 bit depth\n");
		fprintf(stderr, "WARNING: Retrying with 16 bit depth\n");
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
		main_screen = SDL_CreateWindow(get_application_name().c_str(),
									   SDL_WINDOWPOS_CENTERED,
									   SDL_WINDOWPOS_CENTERED,
									   sdl_width, sdl_height,
									   flags);
		if (main_screen)
			logWarning("Stencil buffer is not available");
	}
	if (main_screen != NULL && !nogl && screen_mode.acceleration == _opengl_acceleration)
	{
		// see if we can actually run shaders
		if (!context_created) {
			SDL_GL_CreateContext(main_screen);
			context_created = true;
		}
#if defined (__WIN32__) && (HAVE_OPENGL)
		glewInit();
#endif
		if (!OGL_CheckExtension("GL_ARB_vertex_shader") || !OGL_CheckExtension("GL_ARB_fragment_shader") || !OGL_CheckExtension("GL_ARB_shader_objects") || !OGL_CheckExtension("GL_ARB_shading_language_100"))
		{
			logWarning("OpenGL (Shader) renderer is not available");
			fprintf(stderr, "WARNING: Failed to initialize OpenGL renderer\n");
			fprintf(stderr, "WARNING: Retrying with Software renderer\n");
			screen_mode.acceleration = graphics_preferences->screen_mode.acceleration = _no_acceleration;
			main_screen = SDL_CreateWindow(get_application_name().c_str(),
										   SDL_WINDOWPOS_CENTERED,
										   SDL_WINDOWPOS_CENTERED,
										   sdl_width, sdl_height,
										   flags);
		}
		else
		{
			passed_shader = true;
		}
	}
//#endif

	if (main_screen == NULL) {
		fprintf(stderr, "Can't open video display (%s)\n", SDL_GetError());
#ifdef HAVE_OPENGL
		fprintf(stderr, "WARNING: Failed to initialize OpenGL with 24 bit colour\n");
		fprintf(stderr, "WARNING: Retrying with 16 bit colour\n");
		logWarning("Trying OpenGL 16-bit mode");
		
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
 		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);

		main_screen = SDL_CreateWindow(get_application_name().c_str(),
									   SDL_WINDOWPOS_CENTERED,
									   SDL_WINDOWPOS_CENTERED,
									   sdl_width, sdl_height,
									   flags);
#endif
	}
	if (main_screen == NULL && (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)) {
		fprintf(stderr, "Can't open video display (%s)\n", SDL_GetError());
		fprintf(stderr, "WARNING: Trying in windowed mode");
		logWarning("Trying windowed mode");
		uint32 tempflags = flags & SDL_WINDOW_FULLSCREEN_DESKTOP;
		main_screen = SDL_CreateWindow(get_application_name().c_str(),
									   SDL_WINDOWPOS_CENTERED,
									   SDL_WINDOWPOS_CENTERED,
									   vmode_width, vmode_height,
									   tempflags);
		if (main_screen) {
			screen_mode.fullscreen = graphics_preferences->screen_mode.fullscreen = false;
		}
	}
	if (main_screen == NULL && (flags & SDL_WINDOW_OPENGL)) {
		fprintf(stderr, "Can't open video display (%s)\n", SDL_GetError());
		fprintf(stderr, "WARNING: Trying in software mode");
		logWarning("Trying software mode");
		uint32 tempflags = (flags & ~SDL_WINDOW_OPENGL) | SDL_SWSURFACE;
		main_screen = SDL_CreateWindow(get_application_name().c_str(),
									   SDL_WINDOWPOS_CENTERED,
									   SDL_WINDOWPOS_CENTERED,
									   sdl_width, sdl_height,
									   tempflags);
		if (main_screen) {
			screen_mode.acceleration = graphics_preferences->screen_mode.acceleration = _no_acceleration;
		}
	}
	if (main_screen == NULL && (flags & (SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_OPENGL))) {
		fprintf(stderr, "Can't open video display (%s)\n", SDL_GetError());
		fprintf(stderr, "WARNING: Trying in software windowed mode");
		logWarning("Trying software windowed mode");
		uint32 tempflags = (flags & ~(SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN_DESKTOP)) | SDL_SWSURFACE;
		main_screen = SDL_CreateWindow(get_application_name().c_str(),
									   SDL_WINDOWPOS_CENTERED,
									   SDL_WINDOWPOS_CENTERED,
									   vmode_width, vmode_height,
									   tempflags);
		if (main_screen) {
			screen_mode.acceleration = graphics_preferences->screen_mode.acceleration = _no_acceleration;
			screen_mode.fullscreen = graphics_preferences->screen_mode.fullscreen = false;
		}
	}
	if (main_screen == NULL) {
		fprintf(stderr, "Can't open video display (%s)\n", SDL_GetError());
		fprintf(stderr, "ERROR: Unable to find working display mode");
		logWarning("Unable to find working display mode; exiting");
		vhalt("Cannot find a working video mode.");
	}
#ifdef HAVE_OPENGL
	if (!context_created && !nogl && screen_mode.acceleration != _no_acceleration) {
		SDL_GL_CreateContext(main_screen);
		context_created = true;
	}
#endif
	} // end if need_window
	if (nogl || screen_mode.acceleration == _no_acceleration) {
		if (!main_render) {
			main_render = SDL_CreateRenderer(main_screen, -1, 0);
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
			SDL_RenderSetLogicalSize(main_render, vmode_width, vmode_height);
			main_texture = SDL_CreateTexture(main_render, pixel_format_32.format, SDL_TEXTUREACCESS_STREAMING, vmode_width, vmode_height);
		} else if (!main_texture) {
			main_texture = SDL_CreateTexture(main_render, pixel_format_32.format, SDL_TEXTUREACCESS_STREAMING, vmode_width, vmode_height);
		}
	}
	if (!main_surface) {
		main_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, vmode_width, vmode_height, 32, pixel_format_32.Rmask, pixel_format_32.Gmask, pixel_format_32.Bmask, 0);
	}
#ifdef MUST_RELOAD_VIEW_CONTEXT
	if (!nogl && screen_mode.acceleration != _no_acceleration) 
		ReloadViewContext();
#endif
	if (depth == 8) {
	        SDL_Color colors[256];
		build_sdl_color_table(interface_color_table, colors);
		SDL_SetPaletteColors(main_surface->format->palette, colors, 0, 256);
	}
	if (HUD_Buffer) {
		SDL_FreeSurface(HUD_Buffer);
		HUD_Buffer = NULL;
	}
	if (Term_Buffer) {
		SDL_FreeSurface(Term_Buffer);
		Term_Buffer = NULL;
	}

    screen_rectangle *term_rect = get_interface_rectangle(_terminal_screen_rect);
	Term_Buffer = SDL_CreateRGBSurface(SDL_SWSURFACE, RECTANGLE_WIDTH(term_rect), RECTANGLE_HEIGHT(term_rect), 32, pixel_format_32.Rmask, pixel_format_32.Gmask, pixel_format_32.Bmask, pixel_format_32.Amask);

#ifdef HAVE_OPENGL
	if (!nogl && screen_mode.acceleration != _no_acceleration) {
		static bool gl_info_printed = false;
		if (!gl_info_printed)
		{
			printf("GL_VENDOR: %s\n", glGetString(GL_VENDOR));
			printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
			printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
//		const char *gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
//		printf("GL_EXTENSIONS: %s\n", gl_extensions);
			gl_info_printed = true;
		}
		int pixw = MainScreenPixelWidth();
		int pixh = MainScreenPixelHeight();
		glScissor(0, 0, pixw, pixh);
		glViewport(0, 0, pixw, pixh);
		
		OGL_ClearScreen();
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
#ifdef __WIN32__
		clear_screen();
#endif
	}
#endif
	
	if (in_game && (screen_mode.hud && (prev_width != main_surface->w || prev_height != main_surface->h)) || force_resize_hud)
	{
		L_Call_HUDResize();
	}
}

bool get_auto_resolution_size(short *w, short *h, struct screen_mode_data *mode)
{
	if (screen_mode.auto_resolution)
	{
		short width = Screen::instance()->ModeWidth(0);
		short height = Screen::instance()->ModeHeight(0);
		// in windowed mode, use a window one step down from fullscreen size
		if (!screen_mode.fullscreen &&
			((width > 640) || (height > 480)) &&
			(Screen::instance()->GetModes().size() > 1))
		{
			width = Screen::instance()->ModeWidth(1);
			height = Screen::instance()->ModeHeight(1);
		}
		if (w)
			*w = width;
		if (h)
			*h = height;
		if (mode)
		{
			mode->width = width;
			mode->height = height;
		}
		return true;
	}
	return false;
}
	
void change_screen_mode(struct screen_mode_data *mode, bool redraw, bool resize_hud)
{
	// Get the screen mode here
	screen_mode = *mode;

	// "Redraw" change now and clear the screen
	if (redraw) {
		short w = std::max(mode->width, static_cast<short>(640));
		short h = std::max(mode->height, static_cast<short>(480));
		if (!in_game)
		{
			w = 640;
			h = 480;
		}
		else
			get_auto_resolution_size(&w, &h, mode);
		change_screen_mode(w, h, mode->bit_depth, false, !in_game, resize_hud);
		clear_screen();
		recenter_mouse();
	}

	fps_counter.reset();
}

void change_screen_mode(short screentype)
{
	struct screen_mode_data *mode = &screen_mode;
	
	short w = std::max(mode->width, static_cast<short>(640));
	short h = std::max(mode->height, static_cast<short>(480));
	if (screentype == _screentype_menu)
	{
		w = 640;
		h = 480;
	}
	else
		get_auto_resolution_size(&w, &h, mode);
	
	bool force_menu_size = (screentype == _screentype_menu || screentype == _screentype_chapter);
	change_screen_mode(w, h, mode->bit_depth, false, force_menu_size);
	clear_screen();
	recenter_mouse();

	fps_counter.reset();
}

void toggle_fullscreen(bool fs)
{
	if (fs != screen_mode.fullscreen) {
		screen_mode.fullscreen = fs;
		if (in_game)
			change_screen_mode(&screen_mode, true);
		else {
		  change_screen_mode(&screen_mode, true);
		  clear_screen();
		}
	}
}

void toggle_fullscreen()
{
  toggle_fullscreen(! screen_mode.fullscreen);
  if (!in_game) {
    update_game_window();
  } 
}


/*
 *  Render game screen
 */

static bool clear_next_screen = false;
static void darken_world_window(void);

void update_world_view_camera()
{
	/*
	 * Add a restrained speed cue without disturbing scenario FOV effects.
	 * View_AdjustFOV() performs the interpolation, so entering and leaving a
	 * sprint never causes an abrupt zoom.
	 */
	static float movement_fov_bonus= 0.0f;
	static float bullet_time_fov_offset= 0.0f;
	static float pistol_zoom_fov_offset= 0.0f;
	const float movement_fov_target=
		(input_preferences->sprintathon_enabled &&
		 current_player->sprinting) ? 5.0f : 0.0f;
	const float bullet_time_fov_target=
		sprintathon_bullet_time_active() ? -7.0f : 0.0f;
	const float pistol_zoom_fov_target=
		/* About twice the optical magnification of the previous 48-degree view. */
		sprintathon_single_pistol_zoom_active() ? -55.0f : 0.0f;
	movement_fov_bonus +=
		(movement_fov_target-movement_fov_bonus)*0.06f;
	bullet_time_fov_offset +=
		(bullet_time_fov_target-bullet_time_fov_offset)*0.08f;
	pistol_zoom_fov_offset +=
		(pistol_zoom_fov_target-pistol_zoom_fov_offset)*
		(pistol_zoom_fov_target ? 0.18f : 0.12f);
	if (fabsf(movement_fov_target-movement_fov_bonus)<0.01f)
		movement_fov_bonus= movement_fov_target;
	if (fabsf(bullet_time_fov_target-bullet_time_fov_offset)<0.01f)
		bullet_time_fov_offset= bullet_time_fov_target;
	if (fabsf(pistol_zoom_fov_target-pistol_zoom_fov_offset)<0.01f)
		pistol_zoom_fov_offset= pistol_zoom_fov_target;

	if (!current_player->extravision_duration &&
		!world_view->tunnel_vision_active)
	{
		world_view->target_field_of_view=
			NORMAL_FIELD_OF_VIEW+movement_fov_bonus+
			bullet_time_fov_offset+pistol_zoom_fov_offset;
	}

	int16 scoped_recoil_yaw= 0;
	int16 scoped_recoil_pitch= 0;
	switch (current_player->scoped_pistol_recoil_ticks)
	{
		case 8: scoped_recoil_pitch= 4; break;
		case 7: scoped_recoil_yaw= 1; scoped_recoil_pitch= 8; break;
		case 6: scoped_recoil_yaw= -1; scoped_recoil_pitch= 11; break;
		case 5: scoped_recoil_yaw= 1; scoped_recoil_pitch= 9; break;
		case 4: scoped_recoil_pitch= 6; break;
		case 3: scoped_recoil_yaw= -1; scoped_recoil_pitch= 3; break;
		case 2: scoped_recoil_pitch= 1; break;
		case 1: scoped_recoil_pitch= 0; break;
		default: break;
	}
	scoped_recoil_yaw= (FULL_CIRCLE*scoped_recoil_yaw)/360;
	scoped_recoil_pitch= (FULL_CIRCLE*scoped_recoil_pitch)/360;

	world_view->yaw = current_player->facing+scoped_recoil_yaw;
	world_view->pitch = current_player->elevation+
		current_player->sprintathon_camera_pitch+
		current_player->backflip_camera_pitch+scoped_recoil_pitch;
	world_view->roll = current_player->sprintathon_camera_roll+
		current_player->cartwheel_camera_roll;
	world_view->maximum_depth_intensity = current_player->weapon_intensity;

	world_view->origin = current_player->camera_location;
	if (graphics_preferences->screen_mode.bobbing_type != BobbingType::camera_and_weapon)
		world_view->origin.z -= current_player->step_height;
	world_view->origin_polygon_index = current_player->camera_polygon_index;

	// Script-based camera control
	auto use_cameras = UseLuaCameras();

	world_view->virtual_yaw = world_view->yaw * FIXED_ONE;
	world_view->virtual_pitch = world_view->pitch * FIXED_ONE;

	if (!use_cameras)
	{
		world_view->show_weapons_in_hand =
			!ChaseCam_GetPosition(world_view->origin,
								  world_view->origin_polygon_index,
								  world_view->yaw, world_view->pitch);

		if (current_player_index == local_player_index)
		{
			world_view->virtual_yaw += virtual_aim_delta().yaw;
			world_view->virtual_pitch += virtual_aim_delta().pitch;
			// Rotate the visual camera forward without moving the aim direction.
			if (current_player->slide_roll_ticks_remaining > 0)
			{
				constexpr float roll_duration = 30.0f;
				const float progress = A1_PIN(
					(roll_duration - current_player->slide_roll_ticks_remaining +
					 current_player->slide_roll_fraction / 100.0f) / roll_duration,
					0.0f, 1.0f);
				// Ease into and out of the somersault. A small final nod dips
				// downward and rises smoothly back to the original viewpoint.
				const float eased = progress * progress * (3.0f - 2.0f * progress);
				const float tail = A1_PIN((progress - 0.78f) / 0.22f, 0.0f, 1.0f);
				const float dip = (FULL_CIRCLE * 7.0f / 360.0f) *
					sinf(tail * 3.14159265f);
				world_view->virtual_pitch -= static_cast<_fixed>(
					(eased * FULL_CIRCLE + dip) * FIXED_ONE);
			}
		}
	}
}

// Screenshot mode is local and never changes the player or saved game.
namespace {
bool screenshot_camera_enabled = false;
world_point3d screenshot_origin = {};
short screenshot_polygon = NONE;
float screenshot_x = 0, screenshot_y = 0, screenshot_z = 0;
float screenshot_yaw = 0, screenshot_pitch = 0;
uint64_t screenshot_last_time = 0;
}

bool screenshot_mode_active() { return screenshot_camera_enabled; }

void screenshot_mode_begin()
{
	update_world_view_camera();
	screenshot_origin = world_view->origin;
	screenshot_polygon = world_view->origin_polygon_index;
	screenshot_x = screenshot_origin.x;
	screenshot_y = screenshot_origin.y;
	screenshot_z = screenshot_origin.z;
	screenshot_yaw = world_view->yaw;
	screenshot_pitch = world_view->pitch > HALF_CIRCLE ?
		world_view->pitch - FULL_CIRCLE : world_view->pitch;
	screenshot_last_time = machine_tick_count();
	screenshot_camera_enabled = true;
}

void screenshot_mode_end()
{
	screenshot_camera_enabled = false;
	screenshot_last_time = 0;
}

void screenshot_mode_mouse_look(int dx, int dy)
{
	if (!screenshot_camera_enabled) return;
	const float radians_per_pixel = FULL_CIRCLE / 2400.0f;
	screenshot_yaw += dx * radians_per_pixel;
	screenshot_pitch -= dy * radians_per_pixel;
	screenshot_pitch = std::max(-FULL_CIRCLE * 5.0f / 24.0f,
		std::min(FULL_CIRCLE * 5.0f / 24.0f, screenshot_pitch));
}

static void update_screenshot_camera()
{
	const uint64_t now = machine_tick_count();
	const float dt = screenshot_last_time ?
		std::min(0.05f, static_cast<float>(now - screenshot_last_time) /
			MACHINE_TICKS_PER_SECOND) : 0.0f;
	screenshot_last_time = now;
	const Uint8* keys = SDL_GetKeyboardState(nullptr);
	float forward = float(keys[SDL_SCANCODE_W]) - float(keys[SDL_SCANCODE_S]);
	float strafe = float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]);
	float climb = float(keys[SDL_SCANCODE_E]) - float(keys[SDL_SCANCODE_Q]);
	const float magnitude = sqrtf(forward * forward + strafe * strafe + climb * climb);
	if (magnitude > 1.0f) {
		forward /= magnitude;
		strafe /= magnitude;
		climb /= magnitude;
	}
	const float speed = WORLD_ONE * (keys[SDL_SCANCODE_LSHIFT] ||
		keys[SDL_SCANCODE_RSHIFT] ? 6.0f : 2.0f) * dt;
	const float yaw = screenshot_yaw * (6.28318530718f / FULL_CIRCLE);
	const float next_x = screenshot_x + speed * (cosf(yaw) * forward - sinf(yaw) * strafe);
	const float next_y = screenshot_y + speed * (sinf(yaw) * forward + cosf(yaw) * strafe);
	world_point2d destination = {static_cast<world_distance>(next_x),
		static_cast<world_distance>(next_y)};
	const short polygon = world_point_to_polygon_index(&destination);
	if (polygon != NONE) {
		screenshot_x = next_x;
		screenshot_y = next_y;
		screenshot_polygon = polygon;
	}
	const polygon_data* area = get_polygon_data(screenshot_polygon);
	const float lower = area->floor_height + WORLD_ONE / 16.0f;
	const float upper = area->ceiling_height - WORLD_ONE / 16.0f;
	screenshot_z = upper < lower ? (area->floor_height + area->ceiling_height) / 2.0f :
		std::max(lower, std::min(upper, screenshot_z + speed * climb));
	screenshot_origin.x = static_cast<world_distance>(screenshot_x);
	screenshot_origin.y = static_cast<world_distance>(screenshot_y);
	screenshot_origin.z = static_cast<world_distance>(screenshot_z);
	world_view->origin = screenshot_origin;
	world_view->origin_polygon_index = screenshot_polygon;
	world_view->yaw = NORMALIZE_ANGLE(static_cast<angle>(lroundf(screenshot_yaw)));
	world_view->pitch = NORMALIZE_ANGLE(static_cast<angle>(lroundf(screenshot_pitch)));
	world_view->roll = 0;
	world_view->virtual_yaw = static_cast<_fixed>(screenshot_yaw * FIXED_ONE);
	world_view->virtual_pitch = static_cast<_fixed>(screenshot_pitch * FIXED_ONE);
	world_view->show_weapons_in_hand = false;
}

extern bool is_network_pregame;

void render_screen(short ticks_elapsed)
{
	// Make whatever changes are necessary to the world_view structure based on whichever player is frontmost
	world_view->ticks_elapsed = ticks_elapsed;
	world_view->tick_count = dynamic_world->tick_count;
	world_view->shading_mode = current_player->infravision_duration ? _shading_infravision : _shading_normal;

	update_world_view_camera();

	auto heartbeat_fraction = get_heartbeat_fraction();
	world_view->heartbeat_fraction = heartbeat_fraction;
	update_interpolated_world(heartbeat_fraction);

	bool SwitchedModes = false;
	
	// Suppress the overhead map if desired
	if (PLAYER_HAS_MAP_OPEN(current_player) && View_MapActive()) {
		if (!world_view->overhead_map_active) {
			set_overhead_map_status(true);
			SwitchedModes = true;
		}
	} else {
		if (world_view->overhead_map_active) {
			set_overhead_map_status(false);
			SwitchedModes = true;
		}
	}

	if(player_in_terminal_mode(current_player_index)) {
		if (!world_view->terminal_mode_active) {
			set_terminal_status(true);
			SwitchedModes = true;
		}
	} else {
		if (world_view->terminal_mode_active) {
			set_terminal_status(false);
			SwitchedModes = true;
		}
	}

	// Set rendering-window bounds for the current sort of display to render
	screen_mode_data *mode = &screen_mode;

	SDL_Rect HUD_DestRect = Screen::instance()->hud_rect();
	SDL_Rect ViewRect, MapRect, TermRect;

	bool ViewChangedSize = false;
	bool MapChangedSize = false;

	// Switching fullscreen mode requires much the same reinitialization as switching the screen size
	if (mode->fullscreen != PrevFullscreen) {
		PrevFullscreen = mode->fullscreen;
		ViewChangedSize = true;
		MapChangedSize = true;
	}

	// Each kind of display needs its own size
	ViewRect = Screen::instance()->view_rect();
	MapRect = Screen::instance()->map_rect();
	TermRect = Screen::instance()->term_rect();
	
	static SDL_Rect PrevViewRect = { 0, 0, 0, 0 };
	if (memcmp(&PrevViewRect, &ViewRect, sizeof(SDL_Rect)))
	{
		ViewChangedSize = true;
		PrevViewRect = ViewRect;
	}

	static SDL_Rect PrevMapRect = { 0, 0, 0, 0 };
	if (memcmp(&PrevMapRect, &MapRect, sizeof(SDL_Rect)))
	{
		MapChangedSize = true;
		PrevMapRect = MapRect;
	}
	
	static bool PrevTransparent = false;
	bool MapIsTranslucent = map_is_translucent();
	if (PrevTransparent != MapIsTranslucent)
	{
		MapChangedSize = true;
		PrevTransparent = MapIsTranslucent;
	}
	
	static bool PrevHighRes = true;
	bool HighResolution = mode->high_resolution || is_network_pregame;
	if (PrevHighRes != HighResolution)
	{
		ViewChangedSize = true;
		PrevHighRes = HighResolution;
	}
	
	static short PrevDepth = 0;
	if (PrevDepth != mode->bit_depth)
	{
		ViewChangedSize = true;
		PrevDepth = mode->bit_depth;
	}

	static bool PrevDrawEveryOtherLine = false;
	bool DrawEveryOtherLine = mode->draw_every_other_line;
	if (PrevDrawEveryOtherLine != DrawEveryOtherLine) {
		ViewChangedSize = true;
		PrevDrawEveryOtherLine = DrawEveryOtherLine;
	}

	SDL_Rect BufferRect = {0, 0, ViewRect.w, ViewRect.h};
	// Now the buffer rectangle; be sure to shrink it as appropriate
	if (!HighResolution && screen_mode.acceleration == _no_acceleration) {
		BufferRect.w >>= 1;
		BufferRect.h >>= 1;
	}

	// Set up view data appropriately
	world_view->screen_width = BufferRect.w;
	world_view->screen_height = BufferRect.h;
	world_view->standard_screen_width = 2 * BufferRect.h;
	initialize_view_data(world_view);

	bool update_full_screen = false;
	if (ViewChangedSize || MapChangedSize || SwitchedModes) {
		clear_screen_margin();
		if (!OGL_IsActive() && DrawEveryOtherLine)
			clear_screen();
		update_full_screen = true;
		if (Screen::instance()->hud() && !Screen::instance()->lua_hud() && !is_network_pregame)
			draw_interface();

		// Reallocate the drawing buffer
		if (ViewChangedSize)
			reallocate_world_pixels(BufferRect.w, BufferRect.h);
		if (MapChangedSize)
			reallocate_map_pixels(MapRect.w, MapRect.h);

		dirty_terminal_view(current_player_index);
	} 
	else if (screen_mode.acceleration != _no_acceleration && clear_next_screen)
	{
		clear_screen(false);
		update_full_screen = true;
		if (Screen::instance()->hud() && !Screen::instance()->lua_hud() && !is_network_pregame)
			draw_interface();

		clear_next_screen = false;
	}

	interpolate_world_view(heartbeat_fraction);
	if (screenshot_mode_active()) update_screenshot_camera();

#ifdef HAVE_OPENGL
	// Is map to be drawn with OpenGL?
	if (OGL_IsActive() && world_view->overhead_map_active)
		OGL_MapActive = true;
	else
		OGL_MapActive = false;

	// Set OpenGL viewport to world view
	Rect sr = MakeRect(0, 0, Screen::instance()->height(), Screen::instance()->width());
	Rect vr = MakeRect(ViewRect);
	Screen::instance()->bound_screen_to_rect(ViewRect);
	OGL_SetWindow(sr, vr, true);
	
#endif

    // clear drawing from previous frame
    // (GL must do this before render_view)
    if (screen_mode.acceleration != _no_acceleration)
        clear_screen_margin();
    
	// Update software_render_dest
	if (OGL_IsActive())
		software_render_dest.clear();
	else if (software_render_dest.empty() || ViewChangedSize)
		software_render_dest = bitmap_definition_of_sdl_surface(world_pixels);
	
	// Render world view
	render_view(world_view, software_render_dest.get());

    // clear Lua drawing from previous frame
    // (SDL is slower if we do this before render_view)
    if (screen_mode.acceleration == _no_acceleration &&
		(MapIsTranslucent || Screen::instance()->lua_hud()))
        clear_screen_margin();

	if (game_is_networked && is_network_pregame)
	{
		clear_screen(false);

#ifdef HAVE_OPENGL
		if (OGL_IsActive())
		{
			Screen::instance()->bound_screen();
			OGL_SetWindow(sr, sr, true);
			DisplayNetLoadingScreen(MainScreenSurface());
			OGL_SwapBuffers();
			return;
		}
#endif
		SDL_Rect rect = { (Screen::instance()->window_rect().w - ViewRect.w) / 2, (Screen::instance()->window_rect().h - ViewRect.h) / 2, 0, 0 };
		SDL_FillRect(world_pixels, NULL, SDL_MapRGB(world_pixels->format, 0, 0, 0));
		DisplayNetLoadingScreen(world_pixels);
		update_screen(rect, rect, true, false);
		MainScreenUpdateRect(0, 0, 0, 0);
		return;
	}
    
	// Render crosshairs
	if (!screenshot_mode_active() && !world_view->overhead_map_active && !world_view->terminal_mode_active)
	  if (NetAllowCrosshair())
	    if (Crosshairs_IsActive())
#ifdef HAVE_OPENGL
			if (!OGL_RenderCrosshairs())
#endif
				Crosshairs_Render(world_pixels);

	SDL_Surface *disp_pixels = world_pixels;
	if (world_view->overhead_map_active)
		disp_pixels = Map_Buffer;
	
	// Display FPS and position
	if (!world_view->terminal_mode_active) {
	  extern bool chat_input_mode;
	  if (!chat_input_mode){
		update_fps_display(disp_pixels);
	  }
	  DisplaySprintathonLevelTimer(disp_pixels);
	  DisplayPosition(disp_pixels);
	  DisplayScores(disp_pixels);
	}
	DisplayMessages(disp_pixels);
	DisplayInputLine(disp_pixels);
	
#ifdef HAVE_OPENGL
	// Set OpenGL viewport to whole window (so HUD will be in the right position)
	Screen::instance()->bound_screen();
	OGL_SetWindow(sr, sr, true);
#endif
	

	// If the main view is not being rendered in software but OpenGL is active,
	// then blit the software rendering to the screen
	if (screen_mode.acceleration != _no_acceleration) {
#ifdef HAVE_OPENGL
		if (Screen::instance()->hud()) {
			if (Screen::instance()->lua_hud())
				Lua_DrawHUD(ticks_elapsed);
			else {
				Rect dr = MakeRect(HUD_DestRect);
				OGL_DrawHUD(dr, ticks_elapsed);
			}
		}
		
		if (world_view->terminal_mode_active) {
			// Copy 2D rendering to screen

			if (Term_RenderRequest) {
				SDL_SetSurfaceBlendMode(Term_Buffer, SDL_BLENDMODE_NONE);
				Term_Blitter.Load(*Term_Buffer);
				Term_RenderRequest = false;
			}
			Term_Blitter.nearFilter = TxtrTypeInfoList[OGL_Txtr_HUD].NearFilter;
			Term_Blitter.Draw(TermRect);
		}

#endif
	} else {
		// Update world window
		if (!world_view->terminal_mode_active &&
			(!world_view->overhead_map_active || MapIsTranslucent))
			update_screen(BufferRect, ViewRect, HighResolution, DrawEveryOtherLine);
		
		// Update map
		if (world_view->overhead_map_active) {
			SDL_Rect src_rect = { 0, 0, MapRect.w, MapRect.h };
			DrawSurface(Map_Buffer, MapRect, src_rect);
		}
		
		// Update HUD
		if (Screen::instance()->lua_hud())
		{
			Lua_DrawHUD(ticks_elapsed);
		}
		else if (HUD_RenderRequest) {
			SDL_Rect src_rect = { 0, 320, 640, 160 };
			DrawSurface(HUD_Buffer, HUD_DestRect, src_rect);
			HUD_RenderRequest = false;
		}

		// Update terminal
		if (world_view->terminal_mode_active) {
			if (Term_RenderRequest || Screen::instance()->lua_hud()) {
				SDL_Rect src_rect = { 0, 0, Term_Buffer->w, Term_Buffer->h };
				DrawSurface(Term_Buffer, TermRect, src_rect);
				Term_RenderRequest = false;
			}
		}

		if (!get_keyboard_controller_status() && !screenshot_mode_active())
		{
			darken_world_window();
		}

		if (update_full_screen || Screen::instance()->lua_hud())
		{
			MainScreenUpdateRect(0, 0, 0, 0);
		}
		else if ((!world_view->overhead_map_active || MapIsTranslucent) &&
				 !world_view->terminal_mode_active)
		{
			MainScreenUpdateRects(1, &ViewRect);
		}
	}

#ifdef HAVE_OPENGL
	// Swap OpenGL double-buffers
	if (screen_mode.acceleration != _no_acceleration)
	{
		if (!get_keyboard_controller_status() && !screenshot_mode_active())
		{
			darken_world_window();
		}

		draw_sprintathon_bullet_time_effect();

		OGL_SwapBuffers();
	}
#endif
	
	Movie::instance()->AddFrame(Movie::FRAME_NORMAL);
}

/*
 *  Blit world view to screen
 */

template <class T>
static inline void quadruple_surface(
	const T *src,
	int src_pitch,
	T *dst, int dst_pitch,
	const SDL_Rect &dst_rect,
	bool every_other_line)
{
	int width = dst_rect.w / 2;
	int height = dst_rect.h / 2;
	dst += dst_rect.y * dst_pitch / sizeof(T) + dst_rect.x;
	T *dst2 = dst + dst_pitch / sizeof(T);

	uint32 black_pixel = SDL_MapRGB(main_surface->format, 0, 0, 0);
	bool overlay_active = world_view->overhead_map_active
		&& map_is_translucent();
	
	while (height-- > 0) {
		if (every_other_line) {
			if (overlay_active) {
				// overlay map needs us to clear all the scanlines, so we have
				// to put black in the "skipped" lines
				for (int x=0; x<width; x++) {
					dst[x * 2] = dst[x * 2 + 1] = src[x];
					dst2[x * 2] = dst2[x * 2 + 1] = black_pixel;
				}
			} else {
				for (int x=0; x<width; x++) {
					dst[x * 2] = dst[x * 2 + 1] = src[x];
				}
			}
		} else {
			for (int x=0; x<width; x++) {
				T p = src[x];
				dst[x * 2] = dst[x * 2 + 1] = p;
				dst2[x * 2] = dst2[x * 2 + 1] = p;
			}
		}

		src += src_pitch / sizeof(T);
		dst += dst_pitch * 2 / sizeof(T);
		dst2 += dst_pitch * 2 / sizeof(T);
	}
}

static void apply_gamma(SDL_Surface *src, SDL_Surface *dst)
{
	if (SDL_MUSTLOCK(dst)) {
	    if (SDL_LockSurface(dst) < 0) return;
	}
	uint32 px, dst_px;
	uint8 src_r, src_g, src_b;
	uint8 dst_r, dst_g, dst_b;
	
	uint32 srm = src->format->Rmask, sgm = src->format->Gmask, sbm = src->format->Bmask;
	uint32 drm = dst->format->Rmask, dgm = dst->format->Gmask, dbm = dst->format->Bmask;
	uint32 srs = src->format->Rshift, sgs = src->format->Gshift, sbs = src->format->Bshift;
	uint32 drs = dst->format->Rshift, dgs = dst->format->Gshift, dbs = dst->format->Bshift;
	uint32 srl = src->format->Rloss, sgl = src->format->Gloss, sbl = src->format->Bloss;
	uint32 drl = dst->format->Rloss, dgl = dst->format->Gloss, dbl = dst->format->Bloss;
	
	int sbpp = src->format->BytesPerPixel;
	int dbpp = dst->format->BytesPerPixel;
	uint8 *sptr = static_cast<uint8*>(src->pixels);
	uint8 *dptr = static_cast<uint8*>(dst->pixels);
	size_t numpixels = src->w * src->h;
	for (size_t i = 0; i < numpixels; ++i) {
		switch (sbpp) {
			case 2:
				px = reinterpret_cast<uint16*>(sptr)[i];
				break;
			case 4:
				px = reinterpret_cast<uint32*>(sptr)[i];
				break;
			default:
				return;
		}
	
		src_r = ((px & srm) >> srs) << srl;
		src_g = ((px & sgm) >> sgs) << sgl;
		src_b = ((px & sbm) >> sbs) << sbl;
		dst_r = current_gamma_r[src_r] >> 8;
		dst_g = current_gamma_g[src_g] >> 8;
		dst_b = current_gamma_b[src_b] >> 8;
		dst_px = (((dst_r >> drl) << drs) & drm) |
				 (((dst_g >> dgl) << dgs) & dgm) |
				 (((dst_b >> dbl) << dbs) & dbm);
			
		switch (dbpp) {
			case 2:
				reinterpret_cast<uint16*>(dptr)[i] = dst_px;
				break;
			case 4:
				reinterpret_cast<uint32*>(dptr)[i] = dst_px;
				break;
			default:
				return;
		}
	}
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

static inline bool pixel_formats_equal(SDL_PixelFormat* a, SDL_PixelFormat* b)
{
	return (a->BytesPerPixel == b->BytesPerPixel &&
		a->Rmask == b->Rmask &&
		a->Gmask == b->Gmask &&
		a->Bmask == b->Bmask);
}

static void update_screen(SDL_Rect &source, SDL_Rect &destination, bool hi_rez, bool every_other_line)
{
	SDL_Surface *s = world_pixels;
	if (!using_default_gamma && bit_depth > 8) {
		apply_gamma(world_pixels, world_pixels_corrected);
		s = world_pixels_corrected;
	}
		
	if (hi_rez) 
	{
		SDL_BlitSurface(s, NULL, main_surface, &destination);
	} 
	else 
	{
		SDL_Surface* intermediary = 0;
		if (SDL_MUSTLOCK(main_surface)) 
		{
			if (SDL_LockSurface(main_surface) < 0) return;
		}

		if (!pixel_formats_equal(s->format, main_surface->format))
		{
			intermediary = SDL_ConvertSurface(s, main_surface->format, s->flags);
			s = intermediary;
		}

		switch (s->format->BytesPerPixel) 
		{
		case 1:
			quadruple_surface((pixel8 *)s->pixels, s->pitch, (pixel8 *)main_surface->pixels, main_surface->pitch, destination, every_other_line);
			break;
		case 2:
			quadruple_surface((pixel16 *)s->pixels, s->pitch, (pixel16 *)main_surface->pixels, main_surface->pitch, destination, every_other_line);
			break;
		case 4:
			quadruple_surface((pixel32 *)s->pixels, s->pitch, (pixel32 *)main_surface->pixels, main_surface->pitch, destination, every_other_line);
			break;
		}
		
		if (SDL_MUSTLOCK(main_surface)) {
			SDL_UnlockSurface(main_surface);
		}

		if (intermediary) 
		{
			SDL_FreeSurface(intermediary);
		}
	}
//	SDL_UpdateRects(main_surface, 1, &destination);
}


/*
 *  Update game display if it was overdrawn
 */

void update_screen_window(void)
{
	draw_interface();
	assert_world_color_table(interface_color_table, world_color_table);
}


/*
 *  Color table handling
 */

static void build_sdl_color_table(const color_table *color_table, SDL_Color *colors)
{
	const rgb_color *src = color_table->colors;
	SDL_Color *dst = colors;
	for (int i=0; i<color_table->color_count; i++) {
		dst->r = src->red >> 8;
		dst->g = src->green >> 8;
		dst->b = src->blue >> 8;
		dst->a = 0xff;
		src++; dst++;
	}
}

void initialize_gamma(void)
{
	if (!default_gamma_inited) {
		default_gamma_inited = true;
		for (int i = 0; i < 256; ++i) {
			default_gamma_r[i] = default_gamma_g[i] = default_gamma_b[i] = i << 8;
		}
		memcpy(current_gamma_r, default_gamma_r, sizeof(current_gamma_r));
		memcpy(current_gamma_g, default_gamma_g, sizeof(current_gamma_g));
		memcpy(current_gamma_b, default_gamma_b, sizeof(current_gamma_b));
	}
}

void build_direct_color_table(struct color_table *color_table, short bit_depth)
{
	if (!shell_options.nogamma && !default_gamma_inited)
		initialize_gamma();
	color_table->color_count = 256;
	rgb_color *color = color_table->colors;
	bool force_software = Movie::instance()->IsRecording();
	for (int i=0; i<256; i++, color++)
	{
		color->red = force_software ? i << 8 : default_gamma_r[i];
		color->green = force_software ? i << 8 : default_gamma_g[i];
		color->blue = force_software ? i << 8 : default_gamma_b[i];
	}
}

void change_interface_clut(struct color_table *color_table)
{
	memcpy(interface_color_table, color_table, sizeof(struct color_table));
}

void change_screen_clut(struct color_table *color_table)
{
	if (bit_depth == 8) {
		memcpy(uncorrected_color_table, color_table, sizeof(struct color_table));
		memcpy(interface_color_table, color_table, sizeof(struct color_table));
	} else {
		build_direct_color_table(uncorrected_color_table, bit_depth);
		memcpy(interface_color_table, uncorrected_color_table, sizeof(struct color_table));
	}

	gamma_correct_color_table(uncorrected_color_table, world_color_table, screen_mode.gamma_level);
	memcpy(visible_color_table, world_color_table, sizeof(struct color_table));

	assert_world_color_table(interface_color_table, world_color_table);
}

void animate_screen_clut(struct color_table *color_table, bool full_screen)
{
	for (int i=0; i<color_table->color_count; i++) {
		current_gamma_r[i] = color_table->colors[i].red;
		current_gamma_g[i] = color_table->colors[i].green;
		current_gamma_b[i] = color_table->colors[i].blue;
	}
	using_default_gamma = !memcmp(color_table, uncorrected_color_table, sizeof(struct color_table));
	
	if (interface_bit_depth == 8) {
		SDL_Color colors[256];
		build_sdl_color_table(color_table, colors);
		if (world_pixels)
			SDL_SetPaletteColors(world_pixels->format->palette, colors, 0, 256);
		if (HUD_Buffer)
			SDL_SetPaletteColors(HUD_Buffer->format->palette, colors, 0, 256);
	}
}

void assert_world_color_table(struct color_table *interface_color_table, struct color_table *world_color_table)
{
	if (interface_bit_depth == 8) {
		SDL_Color colors[256];
		build_sdl_color_table(interface_color_table, colors);
		if (world_pixels)
			SDL_SetPaletteColors(world_pixels->format->palette, colors, 0, 256);
		if (HUD_Buffer)
			SDL_SetPaletteColors(HUD_Buffer->format->palette, colors, 0, 256);
	}
	if (world_color_table)
		animate_screen_clut(world_color_table, false);
}


/*
 *  Render terminal
 */

void render_computer_interface(struct view_data *view)
{
	_set_port_to_term();
	_render_computer_interface();
	_restore_port();
}


/*
 *  Render overhead map
 */

void render_overhead_map(struct view_data *view)
{
#ifdef HAVE_OPENGL
	if (OGL_IsActive()) {
		// Set OpenGL viewport to world view
		Rect sr = MakeRect(0, 0, Screen::instance()->height(), Screen::instance()->width());
		SDL_Rect MapRect = Screen::instance()->map_rect();
		Rect mr = MakeRect(MapRect);
		Screen::instance()->bound_screen_to_rect(MapRect);
		OGL_SetWindow(sr, mr, true);
	}
#endif
	struct overhead_map_data overhead_data;
	SDL_FillRect(Map_Buffer, NULL, SDL_MapRGB(Map_Buffer->format, 0, 0, 0));

	SDL_Rect maprect = Screen::instance()->map_rect();
	overhead_data.half_width = maprect.w >> 1;
	overhead_data.half_height = maprect.h >> 1;
	overhead_data.width = maprect.w;
	overhead_data.height = maprect.h;
	overhead_data.top = overhead_data.left = 0;

	overhead_data.scale = view->overhead_map_scale;
	overhead_data.mode = _rendering_game_map;
	overhead_data.origin.x = view->origin.x;
	overhead_data.origin.y = view->origin.y;

	_set_port_to_map();
	_render_overhead_map(&overhead_data);
	_restore_port();
}


/*
 *  Get world view destination frame for given screen size
 */

void calculate_destination_frame(short size, bool high_resolution, Rect *frame)
{
	frame->left = frame->top = 0;

	// Calculate destination frame
	switch (size) {
		case _full_screen:
			frame->right = DESIRED_SCREEN_WIDTH;
			frame->bottom = DESIRED_SCREEN_HEIGHT;
			break;
		case _100_percent:
			frame->right = DEFAULT_WORLD_WIDTH;
			frame->bottom = DEFAULT_WORLD_HEIGHT;
			break;
		case _75_percent:
			frame->right = 3 * DEFAULT_WORLD_WIDTH / 4;
			frame->bottom = 3 * DEFAULT_WORLD_HEIGHT / 4;
			break;
		case _50_percent:
			frame->right = DEFAULT_WORLD_WIDTH / 2;
			frame->bottom = DEFAULT_WORLD_HEIGHT / 2;
			break;
	}
	
	if (size != _full_screen) {
		int dx = (DEFAULT_WORLD_WIDTH - frame->right) / 2;
		int dy = (DEFAULT_WORLD_HEIGHT - frame->bottom) / 2;
		frame->top += dy;
		frame->left += dx;
		frame->bottom += dy;
		frame->right += dx;
	}
}


/*
 *  Draw dithered black pattern over world window
 */

template <class T>
static inline void draw_pattern_rect(T *p, int pitch, uint32 pixel, const SDL_Rect &r)
{
	p += r.y * pitch / sizeof(T) + r.x;
	for (int y=0; y<r.h; y++) {
		for (int x=y&1; x<r.w; x+=2)
			p[x] = pixel;
		p += pitch / sizeof(T);
	}
}

static void darken_world_window(void)
{
	// Get world window bounds
	SDL_Rect r = Screen::instance()->window_rect();

#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL()) {

		// Save current state
		glPushAttrib(GL_ALL_ATTRIB_BITS);

		// Disable everything but alpha blending
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
		glEnable(GL_BLEND);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_FOG);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_STENCIL_TEST);

		// Direct projection
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0.0, GLdouble(main_surface->w), GLdouble(main_surface->h), 0.0, 0.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

		// Draw 50% black rectangle
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.0, 0.0, 0.0, 0.5);
		OGL_RenderRect(r);

		// Restore projection and state
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glPopAttrib();

//		MainScreenSwap();
		return;
	}
#endif

	// Get black pixel value
	uint32 pixel = SDL_MapRGB(main_surface->format, 0, 0, 0);

	// Lock surface
	if (SDL_MUSTLOCK(main_surface))
		if (SDL_LockSurface(main_surface) < 0)
			return;

	// Draw pattern
	switch (main_surface->format->BytesPerPixel) {
		case 1:
			draw_pattern_rect((pixel8 *)main_surface->pixels, main_surface->pitch, pixel, r);
			break;
		case 2:
			draw_pattern_rect((pixel16 *)main_surface->pixels, main_surface->pitch, pixel, r);
			break;
		case 4:
			draw_pattern_rect((pixel32 *)main_surface->pixels, main_surface->pitch, pixel, r);
			break;
	}

	// Unlock surface
	if (SDL_MUSTLOCK(main_surface))
		SDL_UnlockSurface(main_surface);

	MainScreenUpdateRects(1, &r);
}

#ifdef HAVE_OPENGL
static GLhandleARB sprintathon_radial_blur_program()
{
	static GLhandleARB program= 0;
	static bool attempted= false;
	if (attempted)
		return program;
	attempted= true;

	static const GLcharARB *vertex_source=
		"#version 120\n"
		"varying vec2 blur_uv;\n"
		"void main() {\n"
		"  gl_Position = ftransform();\n"
		"  blur_uv = gl_MultiTexCoord0.xy;\n"
		"}\n";
	static const GLcharARB *fragment_source=
		"#version 120\n"
		"uniform sampler2D frame_texture;\n"
		"uniform float effect_amount;\n"
		"uniform float heavy_amount;\n"
		"uniform float scope_amount;\n"
		"uniform float aspect_ratio;\n"
		"varying vec2 blur_uv;\n"
		"void main() {\n"
		"  vec2 radial = blur_uv - vec2(0.5);\n"
		"  vec2 shaped = radial * vec2(aspect_ratio, 1.0);\n"
		"  float radius = length(shaped);\n"
		"  float edge = smoothstep(mix(0.12, 0.035, heavy_amount), 0.58, radius);\n"
		"  float scope_edge = smoothstep(0.34, 0.43, radius);\n"
		"  vec4 original = texture2D(frame_texture, blur_uv);\n"
		"  vec4 blurred = original;\n"
		"  for (int i = 1; i <= 16; ++i) {\n"
		"    float blur_amount = max(effect_amount * mix(1.0, 2.8, heavy_amount), scope_amount * 2.2);\n"
		"    float distance = float(i) * 0.0036 * blur_amount;\n"
		"    vec2 offset = radial * distance;\n"
		"    blurred += texture2D(frame_texture, blur_uv - offset);\n"
		"  }\n"
		"  blurred /= 17.0;\n"
		"  float bullet_strength = edge * effect_amount * mix(1.2, 2.15, heavy_amount);\n"
		"  float scope_strength = scope_edge * scope_amount * 1.35;\n"
		"  float strength = min(1.0, max(bullet_strength, scope_strength));\n"
		"  gl_FragColor = mix(original, blurred, strength);\n"
		"}\n";

	GLint compiled= GL_FALSE;
	GLhandleARB vertex= glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
	glShaderSourceARB(vertex, 1, &vertex_source, nullptr);
	glCompileShaderARB(vertex);
	glGetObjectParameterivARB(vertex, GL_OBJECT_COMPILE_STATUS_ARB, &compiled);
	if (!compiled)
	{
		glDeleteObjectARB(vertex);
		return 0;
	}

	GLhandleARB fragment= glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
	glShaderSourceARB(fragment, 1, &fragment_source, nullptr);
	glCompileShaderARB(fragment);
	glGetObjectParameterivARB(fragment, GL_OBJECT_COMPILE_STATUS_ARB, &compiled);
	if (!compiled)
	{
		glDeleteObjectARB(vertex);
		glDeleteObjectARB(fragment);
		return 0;
	}

	program= glCreateProgramObjectARB();
	glAttachObjectARB(program, vertex);
	glAttachObjectARB(program, fragment);
	glLinkProgramARB(program);
	glDeleteObjectARB(vertex);
	glDeleteObjectARB(fragment);
	GLint linked= GL_FALSE;
	glGetObjectParameterivARB(program, GL_OBJECT_LINK_STATUS_ARB, &linked);
	if (!linked)
	{
		glDeleteObjectARB(program);
		program= 0;
	}
	return program;
}

static void draw_sprintathon_bullet_time_effect()
{
	static float amount= 0.f;
	static float scope_amount= 0.f;
	static float transition_start_amount= 0.f;
	static float scope_transition_start_amount= 0.f;
	static float previous_target= 0.f;
	static float previous_scope_target= 0.f;
	static uint32 transition_start_tick= 0;
	static uint32 scope_transition_start_tick= 0;
	const uint32 now= machine_tick_count();
	const float target= sprintathon_bullet_time_active() ? 1.f : 0.f;
	const float scope_target=
		sprintathon_single_pistol_zoom_active() ? 1.f : 0.f;
	if (target!=previous_target)
	{
		transition_start_amount= amount;
		transition_start_tick= now;
		previous_target= target;
	}
	if (scope_target!=previous_scope_target)
	{
		scope_transition_start_amount= scope_amount;
		scope_transition_start_tick= now;
		previous_scope_target= scope_target;
	}
	const float progress= std::min(
		static_cast<float>(now-transition_start_tick)/1000.f, 1.f);
	amount= transition_start_amount+
		(target-transition_start_amount)*progress;
	const float scope_progress= std::min(
		static_cast<float>(now-scope_transition_start_tick)/180.f, 1.f);
	const float eased_scope_progress=
		scope_progress*scope_progress*(3.f-2.f*scope_progress);
	scope_amount= scope_transition_start_amount+
		(scope_target-scope_transition_start_amount)*eased_scope_progress;
	if (amount<0.001f && target==0.f &&
		scope_amount<0.001f && scope_target==0.f)
	{
		amount= 0.f;
		scope_amount= 0.f;
		return;
	}

	/* Work in physical framebuffer pixels. Scenario HUDs can change the logical
	 * surface and leave it smaller than the OpenGL drawable. */
	const GLsizei pixel_width= MainScreenPixelWidth();
	const GLsizei pixel_height= MainScreenPixelHeight();
	if (pixel_width<=0 || pixel_height<=0)
		return;
	static GLuint captured_frame= 0;
	static GLsizei texture_width= 0, texture_height= 0;
	const GLfloat right= static_cast<GLfloat>(pixel_width);
	const GLfloat bottom= static_cast<GLfloat>(pixel_height);
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	/* HUD implementations and scenario plugins may leave a small sub-viewport
	 * active after their final draw.  A full-screen post-process drawn through
	 * that viewport appears enlarged and anchored in one corner.  Use the whole
	 * drawable here; GL_ALL_ATTRIB_BITS restores the scenario viewport below. */
	glViewport(0, 0, pixel_width, pixel_height);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_FOG);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_BLEND);
	glShadeModel(GL_SMOOTH);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, GLdouble(pixel_width), GLdouble(pixel_height), 0.0,
		0.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	/* Capture the finished world/HUD once, immediately before buffer swap. */
	glEnable(GL_TEXTURE_2D);
	if (!captured_frame)
		glGenTextures(1, &captured_frame);
	glBindTexture(GL_TEXTURE_2D, captured_frame);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (texture_width!=pixel_width || texture_height!=pixel_height)
	{
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
			0, 0, pixel_width, pixel_height, 0);
		texture_width= pixel_width;
		texture_height= pixel_height;
	}
	else
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			0, 0, pixel_width, pixel_height);

	/* Average framebuffer samples in the fragment shader.  Unlike layered
	 * fixed-function quads, this produces continuous blur rather than ghosts. */
	const GLhandleARB blur_program= sprintathon_radial_blur_program();
	if (blur_program)
	{
		const float bullet_blur_amount =
			input_preferences->sprintathon_bullet_time_blur ? amount : 0.f;
		const float heavy_blur_amount =
			(input_preferences->sprintathon_bullet_time_blur &&
			 input_preferences->sprintathon_bullet_time_heavy_blur) ? amount : 0.f;
		const float scope_blur_amount =
			input_preferences->sprintathon_pistol_scope_blur ? scope_amount : 0.f;
		glUseProgramObjectARB(blur_program);
		glUniform1iARB(
			glGetUniformLocationARB(blur_program, "frame_texture"), 0);
		glUniform1fARB(
			glGetUniformLocationARB(blur_program, "effect_amount"),
			bullet_blur_amount);
		glUniform1fARB(
			glGetUniformLocationARB(blur_program, "heavy_amount"),
			heavy_blur_amount);
		glUniform1fARB(
			glGetUniformLocationARB(blur_program, "scope_amount"),
			scope_blur_amount);
		glUniform1fARB(
			glGetUniformLocationARB(blur_program, "aspect_ratio"),
			static_cast<GLfloat>(pixel_width)/pixel_height);
		glColor4f(1.f, 1.f, 1.f, 1.f);
		glBegin(GL_QUADS);
		glTexCoord2f(0.f, 1.f); glVertex2f(0.f, 0.f);
		glTexCoord2f(1.f, 1.f); glVertex2f(right, 0.f);
		glTexCoord2f(1.f, 0.f); glVertex2f(right, bottom);
		glTexCoord2f(0.f, 0.f); glVertex2f(0.f, bottom);
		glEnd();
		glUseProgramObjectARB(0);
	}

	/* Restore a restrained warm time-state grade as a plain translucent wash.
	 * It is deliberately separate from the captured texture, so it cannot
	 * create the repeated-frame artifact the old contrast pass produced. */
	glDisable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(0.90f, 0.24f, 0.035f, 0.065f*amount);
	glBegin(GL_QUADS);
	glVertex2f(0.f, 0.f);
	glVertex2f(right, 0.f);
	glVertex2f(right, bottom);
	glVertex2f(0.f, bottom);
	glEnd();

	/* Keep the reticle and metal rim sharp while the peripheral scene blurs. */
	if (scope_amount>0.001f)
	{
		static OGL_Blitter pistol_scope;
		static bool load_attempted= false;
		if (!load_attempted)
		{
			load_attempted= true;
			FileSpecifier file("gfx/pistol_scope.png");
			if (!file.Exists() &&
				!file.SetNameWithPath("Sprintathon/pistol_scope.png"))
			{
				file= FileSpecifier(
					get_data_path(kPathDefaultData)+
					"/Sprintathon/pistol_scope.png");
			}
			if (file.Exists())
			{
				ImageDescriptor image;
				if (image.LoadFromFile(file, ImageLoader_Colors, 0))
					pistol_scope.Load(image);
			}
		}
		if (pistol_scope.Loaded())
		{
			/*
			 * Let the physical scope settle just behind the camera.  Camera
			 * angular velocity provides the look lag, while the player's local
			 * movement adds a much smaller body sway.  Keeping this renderer-
			 * local avoids changing aim, simulation or replay state.
			 */
			static float scope_sway_x= 0.f;
			static float scope_sway_y= 0.f;
			const float horizontal_look_velocity= static_cast<float>(
				FIXED_INTEGERAL_PART(
					current_player->variables.angular_velocity));
			const float vertical_look_velocity= static_cast<float>(
				FIXED_INTEGERAL_PART(
					current_player->variables.vertical_angular_velocity));
			const float sideways_velocity= static_cast<float>(
				current_player->variables.perpendicular_velocity)/FIXED_ONE;
			const float forward_velocity= static_cast<float>(
				current_player->variables.velocity)/FIXED_ONE;
			const float maximum_scope_sway_x= pixel_width*0.024f;
			const float maximum_scope_sway_y= pixel_height*0.018f;
			const float target_scope_sway_x= A1_PIN(
				-horizontal_look_velocity*pixel_width/520.f-
					sideways_velocity*pixel_width*0.13f,
				-maximum_scope_sway_x, maximum_scope_sway_x);
			const float target_scope_sway_y= A1_PIN(
				vertical_look_velocity*pixel_height/430.f+
					fabsf(forward_velocity)*pixel_height*0.045f,
				-maximum_scope_sway_y, maximum_scope_sway_y);
			constexpr float scope_follow_speed= 0.065f;
			scope_sway_x+=
				(target_scope_sway_x-scope_sway_x)*scope_follow_speed;
			scope_sway_y+=
				(target_scope_sway_y-scope_sway_y)*scope_follow_speed;

			const float diameter=
				std::min<float>(pixel_width, pixel_height)*0.86f;
			const Image_Rect destination(
				(pixel_width-diameter)*0.5f+scope_sway_x,
				(pixel_height-diameter)*0.5f+scope_sway_y,
				diameter, diameter);
			pistol_scope.tint_color_r= 1.f;
			pistol_scope.tint_color_g= 1.f;
			pistol_scope.tint_color_b= 1.f;
			pistol_scope.tint_color_a= scope_amount;
			pistol_scope.rotation= 0.f;
			pistol_scope.Draw(destination);
		}
	}


	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPopAttrib();
}
#endif

/*
 *  Validate world window
 */

void validate_world_window(void)
{
	RequestDrawingTerm();
}


/*
 *  Draw the HUD or terminal (non-OpenGL)
 */

void DrawSurface(SDL_Surface *s, SDL_Rect &dest_rect, SDL_Rect &src_rect)
{
	if (s) {
		SDL_Rect new_src_rect = {src_rect.x, src_rect.y, src_rect.w, src_rect.h};
		SDL_Surface *surface = s;
		if (dest_rect.w != src_rect.w || dest_rect.h != src_rect.h)
		{
			double x_scale = dest_rect.w / (double) src_rect.w;
			double y_scale = dest_rect.h / (double) src_rect.h;
			surface = rescale_surface(s, static_cast<int>(s->w * x_scale), static_cast<int>(s->h * y_scale));
			new_src_rect.x = static_cast<Sint16>(new_src_rect.x * x_scale);
			new_src_rect.y = static_cast<Sint16>(new_src_rect.y * y_scale);
			new_src_rect.w = static_cast<Uint16>(new_src_rect.w * x_scale);
			new_src_rect.h = static_cast<Uint16>(new_src_rect.h * y_scale);
		}
		SDL_BlitSurface(surface, &new_src_rect, main_surface, &dest_rect);
		if (!Screen::instance()->lua_hud() || (get_game_state() != _game_in_progress))
			MainScreenUpdateRects(1, &dest_rect);
		
		if (surface != s)
			SDL_FreeSurface(surface);
	}
}

void draw_intro_screen(void)
{
	if (fade_blacked_screen())
		return;
	
	SDL_Rect src_rect = { 0, 0, Intro_Buffer->w, Intro_Buffer->h };
	SDL_Rect dst_rect = { 0, 0, src_rect.w, src_rect.h};
	
#ifdef HAVE_OPENGL
	if (OGL_IsActive()) {
		if (intro_buffer_changed) {
			SDL_SetSurfaceBlendMode(Intro_Buffer, SDL_BLENDMODE_NONE);
			Intro_Blitter.Load(*Intro_Buffer);
			intro_buffer_changed = false;
		}
		OGL_Blitter::BoundScreen();
		OGL_ClearScreen();
		Intro_Blitter.Draw(dst_rect);
		
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		OGL_DoFades(dst_rect.x, dst_rect.y, dst_rect.x + dst_rect.w, dst_rect.y + dst_rect.h);		
		OGL_SwapBuffers();
	} else
#endif
	{
		SDL_Surface *s = Intro_Buffer;
		if (!using_default_gamma) {
			apply_gamma(Intro_Buffer, Intro_Buffer_corrected);
			SDL_SetSurfaceBlendMode(Intro_Buffer_corrected, SDL_BLENDMODE_NONE);
			s = Intro_Buffer_corrected;
		}
		DrawSurface(s, dst_rect, src_rect);
		intro_buffer_changed = false;
	}
}

/*
 *  Clear screen
 */

void clear_screen(bool update)
{
#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL()) {
		OGL_ClearScreen();
		if (update) {
			MainScreenSwap();
			OGL_ClearScreen();
		}
	} else 
#endif
	{
		SDL_FillRect(main_surface, NULL, SDL_MapRGB(main_surface->format, 0, 0, 0));
		if (update) MainScreenUpdateRect(0, 0, 0, 0);
	}

	clear_next_screen = true;
}

void clear_screen_margin()
{
#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL()) {
		OGL_ClearScreen();
        return;
	}
#endif
    SDL_Rect r, wr, dr;
    wr = Screen::instance()->window_rect();
    if (world_view->terminal_mode_active)
        dr = Screen::instance()->term_rect();
    else if (world_view->overhead_map_active && !map_is_translucent())
        dr = Screen::instance()->map_rect();
    else
        dr = Screen::instance()->view_rect();

	dr.x -= wr.x;
	dr.y -= wr.y;
	if (Screen::instance()->hud() && !Screen::instance()->lua_hud())
		wr.h -= Screen::instance()->hud_rect().h;
	
    if (dr.x > 0)
    {
        r.x = wr.x;
        r.y = wr.y;
        r.w = dr.x;
        r.h = wr.h;
        SDL_FillRect(main_surface, &r, SDL_MapRGB(main_surface->format, 0, 0, 0));
    }
    if ((dr.x + dr.w) < wr.w)
    {
        r.x = wr.x + dr.x + dr.w;
        r.y = wr.y;
        r.w = wr.w - (dr.x + dr.w);
        r.h = wr.h;
        SDL_FillRect(main_surface, &r, SDL_MapRGB(main_surface->format, 0, 0, 0));
    }
    if (dr.y > 0)
    {
        r.x = wr.x + dr.x;
        r.y = wr.y;
        r.w = dr.x + dr.w;
        r.h = dr.y;
        SDL_FillRect(main_surface, &r, SDL_MapRGB(main_surface->format, 0, 0, 0));
    }
    if ((dr.y + dr.h) < wr.h)
    {
        r.x = wr.x + dr.x;
        r.y = wr.y + dr.y + dr.h;
        r.w = dr.x + dr.w;
        r.h = wr.h - (dr.y + dr.h);
        SDL_FillRect(main_surface, &r, SDL_MapRGB(main_surface->format, 0, 0, 0));
    }
}

bool MainScreenVisible()
{
	return (main_screen != NULL);
}
int MainScreenLogicalWidth()
{
	return (main_surface ? main_surface->w : 0);
}
int MainScreenLogicalHeight()
{
	return (main_surface ? main_surface->h : 0);
}
int MainScreenWindowWidth()
{
	int w = 0;
	SDL_GetWindowSize(main_screen, &w, NULL);
	return w;
}
int MainScreenWindowHeight()
{
	int h = 0;
	SDL_GetWindowSize(main_screen, NULL, &h);
	return h;
}
int MainScreenPixelWidth()
{
	int w = 0;
	int dummy = 0;				// SDL 2.24/Win crashes if you pass nullptr
#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL())
		SDL_GL_GetDrawableSize(main_screen, &w, &dummy);
	else
#endif
		SDL_GetRendererOutputSize(main_render, &w, &dummy);
	return w;
}
int MainScreenPixelHeight()
{
	int h = 0;
	int dummy = 0;				// SDL 2.24/Win crashes if you pass nullptr
#ifdef HAVE_OPENGL
	if (MainScreenIsOpenGL())
		SDL_GL_GetDrawableSize(main_screen, &dummy, &h);
	else
#endif
		SDL_GetRendererOutputSize(main_render, &dummy, &h);
	return h;
}
float MainScreenPixelScale()
{
	return MainScreenPixelWidth() / static_cast<float>(MainScreenWindowWidth());
}
bool MainScreenIsOpenGL()
{
	return (main_screen && !main_render);
}
void MainScreenSwap()
{
	SDL_GL_SwapWindow(main_screen);
}
void MainScreenCenterMouse()
{
	int w, h;
	SDL_GetWindowSize(main_screen, &w, &h);
	SDL_WarpMouseInWindow(main_screen, w/2, h/2);
}
SDL_Surface *MainScreenSurface()
{
	return main_surface;
}
SDL_Window* MainScreenWindow()
{
	return main_screen;
}
void MainScreenUpdateRect(int x, int y, int w, int h)
{
	SDL_Rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	MainScreenUpdateRects(1, &r);
}
void MainScreenUpdateRects(size_t count, const SDL_Rect *rects)
{
	SDL_UpdateTexture(main_texture, NULL, main_surface->pixels, main_surface->pitch);
	SDL_RenderClear(main_render);
	SDL_RenderCopy(main_render, main_texture, NULL, NULL);
//	for (size_t i = 0; i < count; ++i) {
//		SDL_RenderCopy(main_render, main_texture, &rects[i], &rects[i]);
//	}
	SDL_RenderPresent(main_render);
}
