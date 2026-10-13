#ifndef SPRINTATHON_RENDER_RESOLUTION_H
#define SPRINTATHON_RENDER_RESOLUTION_H

#include <algorithm>
#include "preferences.h"
#include "screen.h"

// Keep logical view dimensions unchanged: HUD, projection and editor picking
// use those coordinates. Only the shader framebuffer and its samples shrink.
inline int SprintathonRenderPixels(int logical_pixels)
{
    const int percent = std::max(50, std::min(100,
        graphics_preferences->sprintathon_render_scale));
    return std::max(1, static_cast<int>(logical_pixels * MainScreenPixelScale() *
        (percent / 100.0f)));
}

#endif
