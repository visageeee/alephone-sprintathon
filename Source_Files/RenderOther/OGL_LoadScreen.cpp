/*

	Copyright (C) 2006 and beyond by Bungie Studios, Inc.
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
	
	OpenGL load screens
	written by Gregory Smith, 2006
*/

#include "OGL_LoadScreen.h"
#include "screen.h"

#ifdef HAVE_OPENGL
#include "OGL_Render.h"
#include "SprintathonLoadingTerminal.h"

extern bool OGL_SwapBuffers();

OGL_LoadScreen *OGL_LoadScreen::instance()
{
	static OGL_LoadScreen *instance_ = nullptr;
	if (!instance_) 
		instance_ = new OGL_LoadScreen();
	
	return instance_;
}

extern bool OGL_ClearScreen();

bool OGL_LoadScreen::Start()
{
    SprintathonLoadingTerminal::start();
    use=true;percent=0;status.clear();
    Progress(0);
    return true;
}

void OGL_LoadScreen::SetStatus(const std::string& text)
{
    status=text;
    SprintathonLoadingTerminal::status(text);
}

void OGL_LoadScreen::Stop()
{
    // One short completion beat, after all real loading work has finished.
    // Keep the event pump alive and sleep between frames instead of busy-waiting.
    if(use){
        SprintathonLoadingTerminal::finish();
        const Uint32 finished=SDL_GetTicks();
        do{
            SDL_PumpEvents();
            SprintathonLoadingTerminal::draw();
            OGL_SwapBuffers();
            SDL_Delay(33);
        }while(Uint32(SDL_GetTicks()-finished)<1400);
    }
	OGL_ClearScreen();
	OGL_SwapBuffers();
	Clear();
}

void OGL_LoadScreen::Progress(const int progress)
{
    SprintathonLoadingTerminal::progress(progress);
    SprintathonLoadingTerminal::draw();
    OGL_SwapBuffers();
}

void OGL_LoadScreen::Set(std::string Path, bool Stretch, bool Scale)
{
	OGL_LoadScreen::Set(Path, Stretch, Scale, 0, 0, 0, 0);
	useProgress = false;
}

void OGL_LoadScreen::Set(std::string Path, bool Stretch, bool Scale, short X, short Y, short W, short H)
{
	path = Path;
	x = X;
	y = Y;
	w = W;
	h = H;
	stretch = Stretch;
	scale = Scale;

	image.Clear();
	use = true;
	useProgress = true;
	percent = 0;
}

void OGL_LoadScreen::Clear()
{
	OGL_ClearScreen();
	OGL_SwapBuffers();
	OGL_ClearScreen();

	use = false;
	useProgress = false;
	path.clear();
	status.clear();
	image.Clear();
	blitter.Unload();
}

#endif
