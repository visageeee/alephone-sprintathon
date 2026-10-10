// Sprintathon visual texturing mode. GPL-3.0-or-later.
// Included once by screen.cpp, beside the paused free-camera implementation.
#ifndef SPRINTATHON_SURFACE_EDITOR_H
#define SPRINTATHON_SURFACE_EDITOR_H

#include "SurfaceEditorGeometry.h"
#include "SurfaceEditorObjects.h"
#include "Shape_Blitter.h"
#include "game_wad.h"
#include "game_errors.h"
#include "FileHandler.h"
#include "scenery.h"
#include "platforms.h"
#include "lightsource.h"
#include "OGL_Setup.h"
#include "sdl_dialogs.h"
#include "sdl_widgets.h"
#include <cstdlib>

// Metadata comes from the engine definitions, including scenario MML overrides.
bool surface_editor_panel_definition(short type, short& kind, short& collection);
short surface_editor_match_panel(short type, shape_descriptor texture);
int surface_editor_panel_variant(short type);
short surface_editor_breakable_panel(shape_descriptor texture, short preferred);
#include <map>
#include <memory>
#include <cstdio>
#include <set>
#include <string>

namespace surface_editor {
using surface_editor_geometry::Part;
using surface_editor_geometry::Hit;
bool active = false, palette_cursor = false;
constexpr int columns = 2, rows = 8, cell = 72, page_size = columns*rows;
constexpr int panel_width = 160, panel_height = 708, tiles_y = 68;
struct Texture { shape_descriptor shape; bool landscape; };
struct Scenery { short type, collection, frame; bool hanging; };
struct OffsetChange { Hit hit; int old_x, old_y, new_x, new_y, base_x = 0, base_y = 0; };
struct HeightState {
    std::vector<std::pair<short, line_data>> lines;
    std::vector<std::pair<short, side_data>> sides;
    int side_count = 0;
};
struct Change {
    Hit hit; Texture before, after; int16 old_mode, new_mode;
    bool transparent_geometry = false;
    bool panel = false;
    side_data old_panel{}, new_panel{};
    bool scenery = false;
    map_object placement = {};
    short object_index = NONE;
    int id = -1;
    uint16 sequence = 0;
    bool preserve_sequence = false;
    bool erased = false, persisted = true;
    bool texture_enabled = true, lighting_enabled = false;
    int16 old_light = 0, new_light = 0;
    std::vector<OffsetChange> offsets;
    bool height = false;
    int old_height = 0, new_height = 0;
    HeightState old_geometry, new_geometry;
};
std::vector<Texture> textures;
std::vector<Scenery> scenery;
std::vector<int> light_choices, saved_ids;
std::map<int, short> live_scenery;
int next_id = 0, selected_light = 0;
int custom_light = NONE; // Existing map light; NONE selects the shading swatches.
constexpr int light_steps = 10, light_cell = 57, light_width = 32, header_height = 32;
bool left_collapsed = false, right_collapsed = false;
bool apply_texture = true, apply_lighting = false, align_adjacent = false;
bool edit_panels = false;
bool transparent_surface = false, remove_texture = false;
bool dragging = false;
int height_start_y = 0;
// One transfer mode per surface: scrolling and wobble are mutually exclusive.
enum Motion { keep_motion, still_motion, scroll_x, scroll_y, reverse_x, reverse_y, wobble_motion, motion_count };
int texture_motion = keep_motion;
bool fast_motion = false;
const char* motion_names[] = {"Keep", "Still", "Scroll X", "Scroll Y", "Scroll -X", "Scroll -Y", "Wobble"};
constexpr int controls_height = 297;
void message(const std::string& text);
Change drag_change{};
double drag_u = 0, drag_v = 0;
void update_drag();
void finish_drag();
std::vector<Change> undo, redo;
std::vector<std::unique_ptr<Shape_Blitter>> thumbnails, scenery_thumbnails;
int selected = 0, page = 0, selected_scenery = 0, scenery_page = 0;
int cursor_x = 0, cursor_y = 0;
SDL_Rect pick_viewport = {};
bool scenery_brush = false;
bool panel_dirty = true;
FileSpecifier save_target, source_map;
int source_level = NONE, save_level = NONE;
bool save_target_ready = false, original_fog = false;
uint64_t animation_started = 0;
void save(bool save_as = false);
bool fog_enabled() { return TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Fog); }
std::string status;
SDL_Surface* panels[8] = {};
#ifdef HAVE_OPENGL
OGL_Blitter panel_blitters[8];
#endif

SDL_Rect panel_rect(bool right = false)
{
    const int h = std::max(1, std::min(Screen::instance()->height()-(right ? 99 : controls_height)-header_height, 1239));
    const int w = std::max(128, h*panel_width/panel_height);
    return {right ? Screen::instance()->width()-w : std::max(20, h*light_width/panel_height), header_height, w, h};
}
SDL_Rect light_rect()
{
    const auto r = panel_rect();
    return {0, r.y, r.x, r.h};
}
SDL_Rect checkbox_rect()
{
    const auto r = panel_rect();
    return {0, Screen::instance()->height()-controls_height, r.x+r.w, controls_height};
}
SDL_Rect actions_rect()
{
    const auto r = panel_rect(true);
    return {r.x, right_collapsed ? header_height : r.y+r.h, r.w, 99};
}
SDL_Rect footer_rect()
{
    const auto l = panel_rect(), r = panel_rect(true);
    return {l.x+l.w+8, Screen::instance()->height()-28,
        std::max(1, r.x-l.x-l.w-16), 28};
}
bool contains(SDL_Rect r, int x, int y)
{ return x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h; }
SDL_Rect header_rect(bool right = false)
{
    const auto r = panel_rect(right);
    return {right ? r.x : 0, 0, right ? r.w : r.x+r.w, header_height};
}
bool over_ui(int x, int y)
{
    return contains(actions_rect(), x, y) || contains(header_rect(), x, y) || contains(header_rect(true), x, y) ||
        (!left_collapsed && (contains(panel_rect(), x, y) || contains(light_rect(), x, y) || contains(checkbox_rect(), x, y))) ||
        (!right_collapsed && contains(panel_rect(true), x, y)) ||
        (!status.empty() && contains(footer_rect(), x, y));
}

void motion(int x, int y)
{
    // SDL already remaps software-renderer events to its logical resolution.
    if (OGL_IsActive()) {
        int w, h; SDL_GetWindowSize(MainScreenWindow(), &w, &h);
        if (w && h) { x = x*Screen::instance()->width()/w; y = y*Screen::instance()->height()/h; }
    }
    cursor_x = x; cursor_y = y;
    if (dragging) update_drag();
}

surface_editor_geometry::Ray picking_ray()
{
    const auto view = Screen::instance()->view_rect();
    double nx = palette_cursor ? 2.0*(cursor_x-view.x)/std::max(1, view.w)-1.0 : 0;
    double ny = palette_cursor ? 1.0-2.0*(cursor_y-view.y)/std::max(1, view.h) : 0;
    if (palette_cursor && OGL_IsActive() && pick_viewport.w > 0 && pick_viewport.h > 0) {
        const double px = double(cursor_x)*MainScreenPixelWidth()/Screen::instance()->width();
        const double py = MainScreenPixelHeight()-double(cursor_y)*MainScreenPixelHeight()/Screen::instance()->height();
        nx = 2*(px-pick_viewport.x)/pick_viewport.w-1;
        ny = 2*(py-pick_viewport.y)/pick_viewport.h-1;
    }
    const double yaw = screenshot_yaw*(6.283185307179586/FULL_CIRCLE);
    double pitch = screenshot_pitch*(6.283185307179586/FULL_CIRCLE);
    double right, up;
    if (OGL_IsActive()) {
        // Match Rasterizer_Shader's frustum, including the FOV preference.
        const double aspect = world_view->screen_width/double(std::max(1, int(world_view->screen_height)));
        const double tangent = std::tan(world_view->field_of_view*3.141592653589793/360.0);
        const double xtan = View_FOV_FixHorizontalNotVertical() ? tangent : tangent*aspect/2;
        const double ytan = View_FOV_FixHorizontalNotVertical() ? tangent/aspect : tangent/2;
        right = nx*xtan*world_view->real_world_to_screen_x/std::max(1, int(world_view->world_to_screen_x));
        up = ny*ytan*world_view->real_world_to_screen_y/std::max(1, int(world_view->world_to_screen_y));
    } else {
        right = nx*world_view->half_screen_width/std::max(1, int(world_view->world_to_screen_x));
        up = ny*world_view->half_screen_height/std::max(1, int(world_view->world_to_screen_y));
        pitch = std::atan(double(world_view->dtanpitch)/std::max(1, int(world_view->world_to_screen_y)));
    }
    return surface_editor_geometry::camera_ray(screenshot_origin.x, screenshot_origin.y, screenshot_origin.z,
        yaw, pitch, right, up, !OGL_IsActive() || world_view->mimic_sw_perspective);
}

struct Surface { shape_descriptor* shape = nullptr; int16* mode = nullptr; int16* light = nullptr; };
Surface surface(Hit hit)
{
    if (hit.index < 0) return {};
    if (hit.part == Part::floor || hit.part == Part::ceiling) {
        if (hit.index >= dynamic_world->polygon_count) return {};
        auto* p = get_polygon_data(hit.index);
        return hit.part == Part::floor ? Surface{&p->floor_texture, &p->floor_transfer_mode, &p->floor_lightsource_index} :
            Surface{&p->ceiling_texture, &p->ceiling_transfer_mode, &p->ceiling_lightsource_index};
    }
    if (hit.part == Part::none || hit.index < 0 || hit.index >= dynamic_world->side_count) return {};
    auto* s = get_side_data(hit.index);
    if (hit.part == Part::secondary) return {&s->secondary_texture.texture, &s->secondary_transfer_mode, &s->secondary_lightsource_index};
    if (hit.part == Part::transparent) return {&s->transparent_texture.texture, &s->transparent_transfer_mode, &s->transparent_lightsource_index};
    return {&s->primary_texture.texture, &s->primary_transfer_mode, &s->primary_lightsource_index};
}

Hit target(const surface_editor_geometry::Ray& ray, std::vector<std::pair<int, double>>* visited = nullptr)
{
    using namespace surface_editor_geometry;
    if (screenshot_polygon < 0 || screenshot_polygon >= dynamic_world->polygon_count) return {};
    auto room = [](int index) {
        const auto* p = get_polygon_data(index);
        Room r{double(p->floor_height), double(p->ceiling_height), {}};
        for (int i = 0; i < p->vertex_count; ++i) {
            auto a = get_endpoint_data(p->endpoint_indexes[i])->vertex;
            auto b = get_endpoint_data(p->endpoint_indexes[(i+1)%p->vertex_count])->vertex;
            const int side_index = p->side_indexes[i];
            const auto* s = side_index == NONE ? nullptr : get_side_data(side_index);
            r.edges.push_back({double(a.x), double(a.y), double(b.x), double(b.y),
                p->adjacent_polygon_indexes[i], side_index, s && s->type == _split_side,
                s && s->transparent_texture.texture != UNONE,
                s && s->type == _full_side && s->primary_texture.texture != UNONE, p->line_indexes[i]});
        }
        return r;
    };
    const bool texture_tool = !visited && !scenery_brush && !edit_panels;
    const bool through = texture_tool ? !transparent_surface : (SDL_GetModState() & KMOD_ALT) != 0;
    return trace(ray, screenshot_polygon, room, through, visited,
        texture_tool && transparent_surface);
}

struct Offset { world_distance* x; world_distance* y; };
Offset offset(Hit h)
{
    if (h.part == Part::floor || h.part == Part::ceiling) {
        auto* p = get_polygon_data(h.index);
        auto& o = h.part == Part::floor ? p->floor_origin : p->ceiling_origin;
        return {&o.x, &o.y};
    }
    auto* side = get_side_data(h.index);
    auto& o = h.part == Part::secondary ? side->secondary_texture :
        h.part == Part::transparent ? side->transparent_texture : side->primary_texture;
    return {&o.x0, &o.y0};
}
struct TextureFrame {
    int a = NONE, b = NONE;
    double x = 0, y = 0, dx = 0, dy = 0, length = 0, top = 0;
};
TextureFrame texture_frame(Hit h)
{
    TextureFrame f;
    auto* p = get_polygon_data(h.polygon);
    if (h.part == Part::floor || h.part == Part::ceiling) {
        f.top = h.part == Part::floor ? p->floor_height : p->ceiling_height;
        return f;
    }
    for (int i = 0; i < p->vertex_count; ++i) if (p->side_indexes[i] == h.index) {
        f.a = p->endpoint_indexes[i]; f.b = p->endpoint_indexes[(i+1)%p->vertex_count];
        const auto a = get_endpoint_data(f.a)->vertex, b = get_endpoint_data(f.b)->vertex;
        auto* line = get_line_data(p->line_indexes[i]);
        f.x = a.x; f.y = a.y; f.dx = b.x-a.x; f.dy = b.y-a.y;
        f.length = line->length;
        f.top = h.part == Part::transparent ? line->lowest_adjacent_ceiling :
            (h.part == Part::secondary || get_side_data(h.index)->type == _low_side) ?
            std::max(line->highest_adjacent_floor, p->floor_height) : p->ceiling_height;
        break;
    }
    return f;
}
bool drag_coordinates(Hit h, double& u, double& v)
{
    const auto r = picking_ray();
    const auto f = texture_frame(h);
    return surface_editor_geometry::texture_drag_coordinates(r,
        h.part == Part::floor || h.part == Part::ceiling,
        f.x, f.y, f.dx, f.dy, f.length, f.top, 128*WORLD_ONE, u, v);
}

int wrapped_offset(int value)
{
    // All supported texture scales divide four world units.
    constexpr int period = 4*WORLD_ONE;
    return (value%period+period)%period;
}
void set_offsets(const Change& c, bool forward)
{
    for (const auto& edit : c.offsets) {
        auto o = offset(edit.hit);
        *o.x = forward ? edit.new_x : edit.old_x;
        *o.y = forward ? edit.new_y : edit.old_y;
    }
}
// Snapshot only the boundary of the edited polygon. New sides are appended by
// new_side(), and removed again by undo, keeping map references reversible.
HeightState capture_geometry(int polygon)
{
    HeightState state;
    state.side_count = int(SideList.size());
    const auto* p = get_polygon_data(polygon);
    for (int i = 0; i < p->vertex_count; ++i) {
        const short index = p->line_indexes[i];
        const auto& line = *get_line_data(index);
        state.lines.push_back({index, line});
        for (short side : {line.clockwise_polygon_side_index, line.counterclockwise_polygon_side_index})
            if (side != NONE) state.sides.push_back({side, *get_side_data(side)});
    }
    return state;
}
void refresh_height_geometry(int polygon)
{
    const auto* p = get_polygon_data(polygon);
    std::set<short> owners;
    for (int i = 0; i < p->vertex_count; ++i) {
        auto* line = get_line_data(p->line_indexes[i]);
        for (short owner : {line->clockwise_polygon_owner, line->counterclockwise_polygon_owner})
            if (owner != NONE) owners.insert(owner);
        recalculate_redundant_line_data(p->line_indexes[i]);
    }
    // Render flags are normally allocated only at level load. Height edits
    // can append sides beyond that original allocation.
    if (RenderFlagList.size() < size_t(RENDER_FLAGS_BUFFER_SIZE))
        RenderFlagList.resize(RENDER_FLAGS_BUFFER_SIZE);
    for (short owner : owners) recalculate_redundant_polygon_data(owner);
    for (int i = 0; i < p->vertex_count; ++i)
        recalculate_redundant_endpoint_data(p->endpoint_indexes[i]);
    init_interpolated_world();
}
void restore_geometry(const HeightState& state, int polygon)
{
    SideList.resize(state.side_count);
    dynamic_world->side_count = state.side_count;
    for (const auto& entry : state.lines) *get_line_data(entry.first) = entry.second;
    for (const auto& entry : state.sides) *get_side_data(entry.first) = entry.second;
    refresh_height_geometry(polygon);
}
// Paused editor door previews never change platform activation or save state.
std::map<short, std::pair<world_distance, world_distance>> door_positions;
std::map<short, uint16> door_line_flags;
void repair_door_side(short index);
void preview_door_height(short polygon, world_distance floor, world_distance ceiling)
{
    auto* p = get_polygon_data(polygon);
    p->floor_height = floor; p->ceiling_height = ceiling;
    refresh_height_geometry(polygon);
    // Platform movement normally updates these portal flags too. The paused
    // editor must do it explicitly, without advancing the gameplay simulation.
    for (int i = 0; i < p->vertex_count; ++i) {
        auto* line = get_line_data(p->line_indexes[i]);
        if (line->clockwise_polygon_owner == NONE || line->counterclockwise_polygon_owner == NONE) continue;
        const bool open = line->highest_adjacent_floor < line->lowest_adjacent_ceiling;
        SET_LINE_TRANSPARENCY(line, open);
        SET_LINE_SOLIDITY(line, !open);
        for (short side_index : {line->clockwise_polygon_side_index, line->counterclockwise_polygon_side_index})
            if (side_index != NONE) repair_door_side(side_index);
    }
    for (int i = 0; i < p->vertex_count; ++i)
        recalculate_redundant_endpoint_data(p->endpoint_indexes[i]);
    init_interpolated_world();
}
void restore_door_positions()
{
    for (const auto& entry : door_positions)
        preview_door_height(entry.first, entry.second.first, entry.second.second);
    for (const auto& entry : door_line_flags) get_line_data(entry.first)->flags = entry.second;
    for (const auto& entry : door_positions) {
        const auto* p = get_polygon_data(entry.first);
        for (int i = 0; i < p->vertex_count; ++i)
            recalculate_redundant_endpoint_data(p->endpoint_indexes[i]);
    }
    init_interpolated_world();
}
struct SavedDoorPositions {
    std::map<short, std::pair<world_distance, world_distance>> preview;
    SavedDoorPositions() {
        for (const auto& entry : door_positions) {
            const auto* p = get_polygon_data(entry.first);
            preview[entry.first] = {p->floor_height, p->ceiling_height};
        }
        restore_door_positions();
    }
    ~SavedDoorPositions() {
        for (const auto& entry : preview)
            preview_door_height(entry.first, entry.second.first, entry.second.second);
    }
};
void use_door()
{
    finish_drag();
    // Trace visible surfaces, independent of the current paint channel.
    std::vector<std::pair<int, double>> visited;
    const Hit hit = target(picking_ray(), &visited);
    short polygon = NONE;
    auto consider = [&](short index) {
        if (polygon == NONE && index != NONE && get_polygon_data(index)->type == _polygon_is_platform)
            polygon = index;
    };
    // Include the camera's room so an open door can be closed from inside.
    consider(screenshot_polygon);
    short line_index = hit.line;
    if (line_index < 0 && hit.index >= 0 &&
        (hit.part == Part::primary || hit.part == Part::secondary || hit.part == Part::transparent))
        line_index = get_side_data(hit.index)->line_index;
    if (line_index >= 0) {
        const auto* line = get_line_data(line_index);
        consider(line->clockwise_polygon_owner); consider(line->counterclockwise_polygon_owner);
    }
    if (polygon == NONE) {
        for (const auto& step : visited) {
            consider(step.first);
            if (polygon != NONE) break;
        }
    }
    if (polygon == NONE) { message("Point at a door or its opening, then press E."); return; }
    auto* p = get_polygon_data(polygon);
    const auto* platform = get_platform_data(p->permutation);
    if (!door_positions.count(polygon)) {
        door_positions[polygon] = {p->floor_height, p->ceiling_height};
        for (int i = 0; i < p->vertex_count; ++i) {
            const short index = p->line_indexes[i];
            if (!door_line_flags.count(index)) door_line_flags[index] = get_line_data(index)->flags;
        }
    }
    const bool open = p->floor_height == platform->minimum_floor_height &&
        p->ceiling_height == platform->maximum_ceiling_height;
    preview_door_height(polygon,
        open ? platform->maximum_floor_height : platform->minimum_floor_height,
        open ? platform->minimum_ceiling_height : platform->maximum_ceiling_height);
    message(open ? "Door closed (preview)." : "Door opened (preview).");
}

bool set_height(Hit hit, int height)
{
    if (door_positions.count(hit.index)) { message("Leave visual mode before changing a previewed door height."); return false; }
    auto* p = get_polygon_data(hit.index);
    const int floor = hit.part == Part::floor ? height : p->floor_height;
    const int ceiling = hit.part == Part::ceiling ? height : p->ceiling_height;
    // Engine collision code stores the gap in a signed world_distance.
    if (floor >= ceiling || ceiling-floor > 32767) return false;
    if (p->first_object != NONE && !surface_editor_objects::removable(p->first_object,
        p->first_object, MAXIMUM_OBJECTS_PER_MAP,
        [](int i) { return SLOT_IS_USED(&objects[i]); },
        [](int i) { return objects[i].next_object; })) {
        message("Cannot change height: invalid polygon object list."); return false;
    }
    if (!change_polygon_height(hit.index, floor, ceiling, nullptr)) {
        message("Height blocked by an occupant."); return false;
    }
    // Saved scenery heights are offsets from their floor/ceiling. Keep the live
    // objects in agreement, including hanging scenery and nonzero offsets.
    for (size_t i = 0; i < SavedObjectList.size(); ++i) {
        const auto& record = SavedObjectList[i];
        if (record.type != _saved_object || record.polygon_index != hit.index) continue;
        const auto live = live_scenery.find(saved_ids[i]);
        if (live == live_scenery.end()) continue;
        auto* object = get_object_data(live->second);
        const int z = record.location.z + ((record.flags & _map_object_hanging_from_ceiling) ? ceiling : floor);
        object->location.z = std::max(-32768, std::min(32767, z));
    }
    return true;
}
// Full-height sides do not clip to a moving door's opening. Repair only
// full sides facing a platform, using the engine's standard travel extents.
void repair_door_side(short index)
{
    auto* side = get_side_data(index);
    if (side->type != _full_side) return;
    const short neighbor = find_adjacent_polygon(side->polygon_index, side->line_index);
    if (neighbor == NONE) return;
    if (get_polygon_data(neighbor)->type != _polygon_is_platform &&
        get_polygon_data(side->polygon_index)->type != _polygon_is_platform) return;
    recalculate_side_type(index);
    // The generic classifier returns full for flush openings too. On a
    // two-sided platform boundary that must be a zero-height lower surface,
    // otherwise painting the shaft face fills the entire open doorway.
    if (side->type == _full_side) side->type = _low_side;
    // A full side had one texture. A split door needs the same initial
    // material below as above; retain any explicitly painted lower face.
    if (side->type == _split_side && side->secondary_texture.texture == UNONE) {
        side->secondary_texture = side->primary_texture;
        side->secondary_transfer_mode = side->primary_transfer_mode;
        side->secondary_lightsource_index = side->primary_lightsource_index;
    }
}

void expose_height_sides(Hit hit)
{
    const auto* p = get_polygon_data(hit.index);
    for (int i = 0; i < p->vertex_count; ++i) {
        const short line_index = p->line_indexes[i];
        auto* line = get_line_data(line_index);
        for (int direction = 0; direction < 2; ++direction) {
            const short owner = direction ? line->counterclockwise_polygon_owner : line->clockwise_polygon_owner;
            const short neighbor = direction ? line->clockwise_polygon_owner : line->counterclockwise_polygon_owner;
            if (owner == NONE) continue;
            const auto* room = get_polygon_data(owner);
            const auto* other = neighbor == NONE ? nullptr : get_polygon_data(neighbor);
            const bool exposed = !other || other->floor_height > room->floor_height || other->ceiling_height < room->ceiling_height;
            short side = direction ? line->counterclockwise_polygon_side_index : line->clockwise_polygon_side_index;
            if (side == NONE && exposed) {
                side = new_side(owner, line_index);
                auto* s = get_side_data(side);
                s->primary_texture.texture = s->secondary_texture.texture =
                    hit.part == Part::floor ? p->floor_texture : p->ceiling_texture;
                s->primary_lightsource_index = s->secondary_lightsource_index =
                    hit.part == Part::floor ? p->floor_lightsource_index : p->ceiling_lightsource_index;
            }
            if (side != NONE) {
                auto* s = get_side_data(side);
                const short old_type = s->type;
                recalculate_side_type(side);
                // The engine's generic guess returns full for flush portals.
                // A zero-height low side keeps these portals visibly open.
                if (other && !exposed && (old_type != _full_side || s->primary_texture.texture == UNONE))
                    s->type = _low_side;
                else if (old_type == _full_side && s->primary_texture.texture != UNONE)
                    s->type = _full_side; // preserve intentional full walls
                repair_door_side(side);
                if (old_type == _low_side && s->type == _split_side) {
                    s->secondary_texture = s->primary_texture;
                    s->secondary_transfer_mode = s->primary_transfer_mode;
                    s->secondary_lightsource_index = s->primary_lightsource_index;
                } else if (old_type == _split_side && s->type == _low_side) {
                    s->primary_texture = s->secondary_texture;
                    s->primary_transfer_mode = s->secondary_transfer_mode;
                    s->primary_lightsource_index = s->secondary_lightsource_index;
                }
                const auto shape = hit.part == Part::floor ? p->floor_texture : p->ceiling_texture;
                const auto light = hit.part == Part::floor ? p->floor_lightsource_index : p->ceiling_lightsource_index;
                if (exposed && s->primary_texture.texture == UNONE) {
                    s->primary_texture.texture = shape;
                    s->primary_lightsource_index = light;
                    s->primary_transfer_mode = _xfer_normal;
                }
                if (s->type == _split_side && s->secondary_texture.texture == UNONE) {
                    s->secondary_texture.texture = shape;
                    s->secondary_lightsource_index = light;
                    s->secondary_transfer_mode = _xfer_normal;
                }
            }
        }
    }
    refresh_height_geometry(hit.index);
}
void start_height_drag()
{
    const Hit hit = target(picking_ray());
    if (hit.part != Part::floor && hit.part != Part::ceiling) {
        message("Grab a floor or ceiling to change its height."); return;
    }
    const auto* p = get_polygon_data(hit.index);
    if (p->type == _polygon_is_platform) {
        message("Platform heights are controlled by their movement settings."); return;
    }
    // Reserve the worst-case number of new boundary sides before changing height.
    if (SideList.size()+2*p->vertex_count > 32767) { message("Map side limit reached."); return; }
    drag_change = Change{}; drag_change.hit = hit; drag_change.height = true;
    drag_change.old_height = drag_change.new_height = hit.part == Part::floor ? p->floor_height : p->ceiling_height;
    drag_change.old_geometry = capture_geometry(hit.index);
    height_start_y = cursor_y;
    dragging = true;
    message("");
}
void finish_drag()
{
    if (!dragging) return;
    dragging = false;
    bool changed = drag_change.height && drag_change.old_height != drag_change.new_height;
    if (drag_change.height) {
        if (changed) drag_change.new_geometry = capture_geometry(drag_change.hit.index);
        else restore_geometry(drag_change.old_geometry, drag_change.hit.index);
    }
    for (const auto& e : drag_change.offsets)
        changed = changed || e.old_x != e.new_x || e.old_y != e.new_y;
    if (changed) { undo.push_back(drag_change); redo.clear(); }
    drag_change = Change{};
    init_interpolated_world();
}
void update_drag()
{
    if (drag_change.height) {
        const auto* p = get_polygon_data(drag_change.hit.index);
        const bool floor = drag_change.hit.part == Part::floor;
        const int low = std::max(-32768, floor ? p->ceiling_height-32767 : p->floor_height+1);
        const int high = std::min(32767, floor ? p->ceiling_height-1 : p->floor_height+32767);
        const int height = surface_editor_geometry::drag_height(drag_change.old_height,
            height_start_y-cursor_y, WORLD_ONE, low, high);
        if (height == drag_change.new_height || !set_height(drag_change.hit, height)) return;
        drag_change.new_height = height;
        // Always derive side types/textures from the pre-drag snapshot so a
        // split -> low -> split excursion cannot replace the upper texture.
        for (const auto& entry : drag_change.old_geometry.sides)
            *get_side_data(entry.first) = entry.second;
        expose_height_sides(drag_change.hit);
        char text[80];
        snprintf(text, sizeof(text), "%s: %.3f", floor ? "Floor" : "Ceiling", double(height)/WORLD_ONE);
        message(text);
        return;
    }
    double u, v;
    if (!drag_coordinates(drag_change.hit, u, v)) return;
    const int dx = int(std::lround(u-drag_u)), dy = int(std::lround(v-drag_v));
    for (auto& e : drag_change.offsets) {
        e.new_x = wrapped_offset(e.base_x+dx);
        e.new_y = wrapped_offset(e.base_y+dy);
    }
    set_offsets(drag_change, true);
}
void start_drag()
{
    const Hit h = target(picking_ray());
    const auto src = surface(h);
    if (!src.shape || *src.shape == UNONE || *src.mode == _xfer_landscape) return;
    if (!drag_coordinates(h, drag_u, drag_v)) return;
    drag_change = Change{}; drag_change.hit = h;
    auto capture = [&](Hit hit) {
        auto o = offset(hit);
        drag_change.offsets.push_back({hit,*o.x,*o.y,*o.x,*o.y});
    };
    capture(h);
    if (align_adjacent) {
        // Flood connected matching surfaces. Floors/ceilings share a world origin;
        // walls continue their U coordinate across directed shared endpoints.
        std::vector<Hit> candidates;
        for (int pi = 0; pi < dynamic_world->polygon_count; ++pi) {
            auto* p = get_polygon_data(pi);
            if (h.part == Part::floor || h.part == Part::ceiling) candidates.push_back({h.part,pi,pi,0});
            else for (int j = 0; j < p->vertex_count; ++j)
                if (p->side_indexes[j] != NONE) candidates.push_back({h.part,p->side_indexes[j],pi,0});
        }
        for (size_t i = 0; i < drag_change.offsets.size(); ++i) {
            const auto parent = drag_change.offsets[i];
            const auto frame = texture_frame(parent.hit);
            for (auto candidate : candidates) {
                bool seen = false;
                for (const auto& e : drag_change.offsets) if (e.hit.index == candidate.index) seen = true;
                if (seen) continue;
                auto dst = surface(candidate);
                if (!dst.shape || *dst.shape != *src.shape || *dst.mode != *src.mode) continue;
                const auto f = texture_frame(candidate);
                int x = parent.new_x, y = parent.new_y;
                if (h.part == Part::floor || h.part == Part::ceiling) {
                    auto* p = get_polygon_data(parent.hit.polygon);
                    bool adjacent = false;
                    for (int j = 0; j < p->vertex_count; ++j)
                        adjacent = adjacent || p->adjacent_polygon_indexes[j] == candidate.polygon;
                    if (!adjacent || f.top != frame.top) continue;
                } else {
                    if (f.a == NONE) continue;
                    if (candidate.polygon != parent.hit.polygon) {
                        auto* p = get_polygon_data(parent.hit.polygon);
                        bool adjacent = false;
                        for (int j = 0; j < p->vertex_count; ++j)
                            adjacent = adjacent || p->adjacent_polygon_indexes[j] == candidate.polygon;
                        // Cross connected polygons, but not reverse-facing walls.
                        if (!adjacent || frame.dx*f.dx+frame.dy*f.dy < 0) continue;
                    }
                    if (frame.b == f.a) x += int(frame.length);
                    else if (frame.a == f.b) x -= int(f.length);
                    else continue;
                    y += int(frame.top-f.top);
                }
                capture(candidate);
                drag_change.offsets.back().new_x = wrapped_offset(x);
                drag_change.offsets.back().new_y = wrapped_offset(y);
            }
        }
    }
    for (auto& e : drag_change.offsets) { e.base_x = e.new_x; e.base_y = e.new_y; }
    dragging = true;
}

void message(const std::string& text) { status = text; panel_dirty = true; }

void apply(Hit hit, Texture texture, int16 mode, int16 light, bool texture_enabled, bool lighting_enabled)
{
    const auto dst = surface(hit);
    if (!dst.shape) return;
    if (lighting_enabled) *dst.light = light;
    if (texture_enabled) { *dst.shape = texture.shape; *dst.mode = mode; }
    if (texture_enabled && (hit.part == Part::primary || hit.part == Part::secondary)) {
        repair_door_side(hit.index);
        init_interpolated_world();
    }
    if (texture_enabled && hit.part == Part::transparent) {
        recalculate_redundant_line_data(get_side_data(hit.index)->line_index);
        init_interpolated_world();
    }
    // Keep the projectile/landscape classification consistent with the renderer.
    if (texture_enabled && hit.part == Part::primary) {
        const auto* side = get_side_data(hit.index);
        auto* line = get_line_data(side->line_index);
        bool landscape = false;
        for (short index : {line->clockwise_polygon_side_index, line->counterclockwise_polygon_side_index})
            if (index != NONE && get_side_data(index)->primary_transfer_mode == _xfer_landscape)
                landscape = true;
        SET_LINE_LANDSCAPE_STATUS(line, landscape);
    }
    panel_dirty = true;
}

// Create steady editor lights lazily; never alter a map's shared light.
int swatch_light(int step)
{
    if (light_choices[step] != NONE) return light_choices[step];
    static_light_data definition{};
    definition.type = _normal_light;
    SET_LIGHT_IS_INITIALLY_ACTIVE(&definition, true);
    SET_LIGHT_IS_STATELESS(&definition, true);
    const _fixed brightness = surface_editor_objects::light_level(step, FIXED_ONE);
    for (auto* spec : {&definition.primary_active, &definition.secondary_active, &definition.becoming_active,
                      &definition.primary_inactive, &definition.secondary_inactive, &definition.becoming_inactive}) {
        spec->function = _constant_lighting_function;
        spec->period = TICKS_PER_SECOND;
        spec->intensity = brightness;
    }
    // Reuse only fully constant, untagged definitions at the exact brightness.
    for (size_t i = 0; i < LightList.size(); ++i) {
        const auto& data = LightList[i];
        const auto& d = data.static_data;
        if (!SLOT_IS_USED(&data) || d.tag || data.intensity != brightness) continue;
        bool matches = true;
        for (const auto* spec : {&d.primary_active, &d.secondary_active, &d.becoming_active,
                                &d.primary_inactive, &d.secondary_inactive, &d.becoming_inactive})
            matches = matches && spec->function == _constant_lighting_function &&
                spec->intensity == brightness && spec->delta_intensity == 0;
        if (matches) return light_choices[step] = int(i);
    }
    const bool grow = std::none_of(LightList.begin(), LightList.end(),
        [](const light_data& data) { return SLOT_IS_FREE(&data); });
    if (grow && LightList.size() >= 32767) return NONE;
    if (grow) LightList.emplace_back();
    const short index = new_light(&definition);
    if (index == NONE) { if (grow) LightList.pop_back(); return NONE; }
    dynamic_world->light_count = static_cast<int16>(LightList.size());
    return light_choices[step] = index;
}

int16 paint_motion(int16 previous)
{
    switch (texture_motion) {
        case still_motion: return _xfer_normal;
        case scroll_x: return fast_motion ? _xfer_fast_horizontal_slide : _xfer_horizontal_slide;
        case scroll_y: return fast_motion ? _xfer_fast_vertical_slide : _xfer_vertical_slide;
        case reverse_x: return fast_motion ? _xfer_reverse_fast_horizontal_slide : _xfer_reverse_horizontal_slide;
        case reverse_y: return fast_motion ? _xfer_reverse_fast_vertical_slide : _xfer_reverse_vertical_slide;
        case wobble_motion: return fast_motion ? _xfer_fast_wobble : _xfer_wobble;
        default: return previous == _xfer_landscape ? _xfer_normal : previous;
    }
}
void sample_motion(int16 mode)
{
    texture_motion = keep_motion; fast_motion = false;
    for (int motion = still_motion; motion < motion_count; ++motion) {
        texture_motion = motion;
        for (bool fast : {false, true}) {
            fast_motion = fast;
            if (paint_motion(_xfer_normal) == mode) return;
        }
    }
    texture_motion = keep_motion; fast_motion = false;
}
void paint(bool sample)
{
    Hit hit = target(picking_ray());
    if (transparent_surface && hit.part != Part::transparent) {
        message("Aim at an opening between polygons for transparent decor."); return;
    }
    HeightState before_geometry;
    bool created_side = false;
    if ((hit.part == Part::primary || hit.part == Part::secondary || hit.part == Part::transparent) &&
        hit.index == NONE && !sample &&
        !remove_texture && apply_texture && !textures.empty()) {
        if (hit.line < 0 || SideList.size() >= 32767) {
            message("Cannot create a wall surface here."); return;
        }
        if (hit.part == Part::transparent && textures[selected].landscape) { message("Choose a wall texture for transparent decor."); return; }
        before_geometry = capture_geometry(hit.polygon);
        hit.index = new_side(hit.polygon, hit.line);
        auto* side = get_side_data(hit.index);
        side->primary_lightsource_index = side->secondary_lightsource_index =
            side->transparent_lightsource_index = get_polygon_data(hit.polygon)->floor_lightsource_index;
        if (hit.part != Part::transparent && side->type == _split_side) {
            const auto ray = picking_ray();
            const short neighbor = find_adjacent_polygon(hit.polygon, hit.line);
            if (neighbor != NONE && ray.z + hit.distance * ray.dz <= get_polygon_data(neighbor)->floor_height)
                hit.part = Part::secondary;
        }
        refresh_height_geometry(hit.polygon);
        created_side = true;
    }
    const auto dst = surface(hit);
    if (!dst.shape) { message("No textured surface here."); return; }
    if (sample) {
        for (size_t i = 0; i < textures.size(); ++i)
            if (textures[i].shape == *dst.shape && textures[i].landscape == (*dst.mode == _xfer_landscape)) {
                selected = int(i); page = selected/page_size; break;
            }
        sample_motion(*dst.mode);
        custom_light = *dst.light;
        const double brightness = double(get_light_intensity(*dst.light))/FIXED_ONE;
        selected_light = std::max(0, std::min(light_steps-1, int(std::lround((1-brightness)*(light_steps-1)))));
        remove_texture = false; scenery_brush = false; message("Sampled."); return;
    }
    if (!sample && remove_texture && hit.part != Part::transparent) {
        message("Remove texture is for transparent decorations only."); return;
    }
    const bool tex = remove_texture || (apply_texture && !textures.empty());
    const bool illumination = !remove_texture && apply_lighting && (custom_light != NONE || !light_choices.empty());
    if (!tex && !illumination) { message("No paint channel selected."); return; }
    const Texture next = remove_texture ? Texture{UNONE, false} :
        tex ? textures[selected] : Texture{*dst.shape, *dst.mode == _xfer_landscape};
    if (hit.part == Part::transparent && next.landscape) {
        message("Choose a wall texture for transparent decor."); return;
    }
    const int16 mode = tex ? (next.landscape ? _xfer_landscape :
        paint_motion(*dst.mode)) : *dst.mode;
    const int16 light = illumination ? (custom_light != NONE ? custom_light : swatch_light(selected_light)) : *dst.light;
    if (illumination && (light < 0 || size_t(light) >= LightList.size() || !get_light_data(light))) {
        if (created_side) restore_geometry(before_geometry, hit.polygon);
        message("Map light limit reached."); return;
    }
    if (*dst.shape == next.shape && *dst.mode == mode && *dst.light == light) return;
    Change c{hit, {*dst.shape, *dst.mode == _xfer_landscape}, next, *dst.mode, mode};
    c.old_light = *dst.light; c.new_light = light;
    c.texture_enabled = tex; c.lighting_enabled = illumination;
    apply(hit, next, mode, light, tex, illumination);
    if (created_side) {
        c.transparent_geometry = true;
        c.old_geometry = before_geometry;
        c.new_geometry = capture_geometry(hit.polygon);
    }
    undo.push_back(c); redo.clear();
    message("");
}

bool add_scenery(Change& c)
{
    if (SavedObjectList.size() >= 32767) { message("Map object limit reached."); return false; }
    object_location location{};
    location.p = c.placement.location;
    location.polygon_index = c.placement.polygon_index;
    location.yaw = c.placement.facing;
    location.flags = c.placement.flags;
    c.object_index = new_scenery(&location, c.placement.index);
    if (c.object_index == NONE) { message("No free runtime object slot for scenery."); return false; }
    randomize_scenery_shape(c.object_index);
    if (c.preserve_sequence) get_object_data(c.object_index)->sequence = c.sequence;
    if (c.id < 0) c.id = next_id++;
    live_scenery[c.id] = c.object_index;
    if (c.persisted) {
        SavedObjectList.push_back(c.placement);
        saved_ids.push_back(c.id);
        dynamic_world->initial_objects_count = static_cast<int16>(SavedObjectList.size());
    }
    // Placement and redo must publish the new slot and polygon links together.
    // Otherwise leaving an interpolated frame can restore the pre-placement
    // snapshot while the editor/animation lists still reference the new object.
    init_interpolated_world();
    return true;
}

void index_scenery()
{
    saved_ids.clear(); live_scenery.clear(); next_id = 0;
    for (size_t i = 0; i < SavedObjectList.size(); ++i) saved_ids.push_back(next_id++);
    std::set<int> matched;
    for (int i = 0; i < MAXIMUM_OBJECTS_PER_MAP; ++i) {
        const auto* o = &objects[i];
        if (!SLOT_IS_USED(o) || GET_OBJECT_OWNER(o) != _object_is_scenery) continue;
        int id = -1;
        for (size_t j = 0; j < SavedObjectList.size(); ++j) {
            const auto& record = SavedObjectList[j];
            if (record.type != _saved_object || record.index != o->permutation || record.polygon_index != o->polygon ||
                record.location.x != o->location.x || record.location.y != o->location.y || matched.count(saved_ids[j])) continue;
            const auto* polygon = get_polygon_data(o->polygon);
            const int z = record.location.z + ((record.flags & _map_object_hanging_from_ceiling) ? polygon->ceiling_height : polygon->floor_height);
            if (std::abs(z-o->location.z) > 1) continue;
            id = saved_ids[j]; matched.insert(id); break;
        }
        if (id < 0) id = next_id++; // script-created scenery, no saved-map entry
        live_scenery[id] = i;
    }
}

bool remove_scenery(Change& c)
{
    const auto live = live_scenery.find(c.id);
    if (live == live_scenery.end()) { message("Scenery no longer exists."); return false; }
    const int index = live->second;
    if (index < 0 || index >= MAXIMUM_OBJECTS_PER_MAP || !SLOT_IS_USED(&objects[index]) ||
        GET_OBJECT_OWNER(&objects[index]) != _object_is_scenery) {
        message("Scenery no longer exists."); return false;
    }
    const auto* object = &objects[index];
    if (object->polygon < 0 || object->polygon >= dynamic_world->polygon_count ||
        !surface_editor_objects::removable(get_polygon_data(object->polygon)->first_object,
            index, MAXIMUM_OBJECTS_PER_MAP,
            [](int i) { return SLOT_IS_USED(&objects[i]); },
            [](int i) { return objects[i].next_object; })) {
        message("Cannot erase scenery: invalid polygon object list."); return false;
    }
    const int parasite = object->parasitic_object;
    if (parasite != NONE && (parasite < 0 || parasite >= MAXIMUM_OBJECTS_PER_MAP ||
        parasite == index || !SLOT_IS_USED(&objects[parasite]))) {
        message("Cannot erase scenery: invalid attached object."); return false;
    }
    auto entry = std::find(saved_ids.begin(), saved_ids.end(), c.id);
    if (c.persisted && entry == saved_ids.end()) { message("Saved scenery entry missing."); return false; }
    if (entry != saved_ids.end()) {
        const int removed = int(entry-saved_ids.begin());
        // These are the map's cached references to saved sound-source objects.
        // Compacting scenery records must remap them, without touching geometry
        // lists which share the same MapIndexList storage.
        std::vector<int> starts;
        for (int p = 0; p < dynamic_world->polygon_count; ++p)
            starts.push_back(get_polygon_data(p)->sound_source_indexes);
        surface_editor_objects::remap_sound_sources(MapIndexList, starts, removed);
        SavedObjectList.erase(SavedObjectList.begin()+removed);
        saved_ids.erase(entry);
        dynamic_world->initial_objects_count = static_cast<int16>(SavedObjectList.size());
    }
    const auto* o = get_object_data(live->second);
    c.sequence = o->sequence; c.preserve_sequence = true;
    deanimate_scenery(live->second);
    remove_map_object(live->second);
    live_scenery.erase(live);
    init_interpolated_world();
    return true;
}

int scenery_target()
{
    const auto ray = picking_ray();
    std::vector<std::pair<int, double>> visited;
    const auto obstruction = target(ray, &visited);
    double nearest = obstruction.part == Part::none ? 1e30 : obstruction.distance;
    int selected_id = -1;
    const double yaw = screenshot_yaw*(6.283185307179586/FULL_CIRCLE);
    const double pitch = OGL_IsActive() && world_view->billboard_xy && !world_view->mimic_sw_perspective ?
        screenshot_pitch*(6.283185307179586/FULL_CIRCLE) : 0;
    for (const auto& item : live_scenery) {
        if (item.second < 0 || item.second >= MAXIMUM_OBJECTS_PER_MAP) continue;
        const auto* o = &objects[item.second];
        if (!SLOT_IS_USED(o) || GET_OBJECT_OWNER(o) != _object_is_scenery || OBJECT_IS_INVISIBLE(o)) continue;
        shape_and_transfer_mode shape{};
        get_object_shape_and_transfer_mode(&screenshot_origin, item.second, &shape);
        if (shape.collection_code == NONE || shape.low_level_shape_index == NONE) continue;
        const auto* info = extended_get_shape_information(shape.collection_code, shape.low_level_shape_index);
        if (!info) continue;
        const double t = surface_editor_geometry::sprite_hit(ray, o->location.x, o->location.y, o->location.z,
            yaw, pitch, info->world_left, info->world_right, info->world_bottom, info->world_top);
        if (t < 0 || t > nearest) continue;
        bool reachable = false;
        for (size_t j = 0; j < visited.size(); ++j)
            if (visited[j].first == o->polygon && t >= visited[j].second &&
                (j+1 == visited.size() || t <= visited[j+1].second)) { reachable = true; break; }
        if (reachable) { selected_id = item.first; nearest = t; }
    }
    return selected_id;
}

void erase_scenery()
{
    const int id = scenery_target();
    if (id < 0) { message("No scenery."); return; }
    Change c{}; c.scenery = true; c.erased = true; c.id = id;
    const auto entry = std::find(saved_ids.begin(), saved_ids.end(), id);
    c.persisted = entry != saved_ids.end();
    if (c.persisted) c.placement = SavedObjectList[entry-saved_ids.begin()];
    else {
        const auto* o = get_object_data(live_scenery[id]);
        c.placement.type = _saved_object; c.placement.index = o->permutation;
        c.placement.polygon_index = o->polygon; c.placement.facing = o->facing;
        c.placement.location = o->location;
        c.placement.location.z -= get_polygon_data(o->polygon)->floor_height;
    }
    if (!remove_scenery(c)) return;
    undo.push_back(c); redo.clear();
    message(c.persisted ? "" : "Removed live scenery (no saved map entry).");
}

void place_scenery()
{
    if (scenery.empty()) { message("No loaded scenery types available."); return; }
    const auto ray = picking_ray();
    const Hit hit = target(ray);
    const auto choice = scenery[selected_scenery];
    if (choice.type < 0) { erase_scenery(); return; }
    if (hit.part != (choice.hanging ? Part::ceiling : Part::floor)) {
        message(choice.hanging ? "Ceiling required." : "Floor required.");
        return;
    }
    const auto* polygon = get_polygon_data(hit.polygon);
    world_distance radius, height;
    get_scenery_dimensions(choice.type, &radius, &height);
    if (std::abs(int(height)) > int(polygon->ceiling_height)-polygon->floor_height) {
        message("This sprite is too tall for the selected space."); return;
    }
    Change c{};
    c.scenery = true;
    c.placement.type = _saved_object;
    c.placement.index = choice.type;
    c.placement.polygon_index = hit.polygon;
    c.placement.facing = NORMALIZE_ANGLE(static_cast<angle>(lroundf(screenshot_yaw)));
    // Saved-map Z is relative to the floor/ceiling, just like new_scenery.
    c.placement.flags = choice.hanging ? _map_object_hanging_from_ceiling : 0;
    const double x = ray.x + ray.dx*hit.distance, y = ray.y + ray.dy*hit.distance;
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) {
        message("Placement outside the map coordinate range."); return;
    }
    c.placement.location = {static_cast<world_distance>(std::lround(x)),
        static_cast<world_distance>(std::lround(y)), 0};
    // Snap points on exact polygon edges a tiny distance toward the interior.
    for (int i = 0; i < 2; ++i) {
        auto& p = c.placement.location;
        p.x += (polygon->center.x > p.x) - (polygon->center.x < p.x);
        p.y += (polygon->center.y > p.y) - (polygon->center.y < p.y);
    }
    if (!add_scenery(c)) return;
    undo.push_back(c); redo.clear();
    message("");
}


void edit_control_panel()
{
    finish_drag();
    const Hit hit = target(picking_ray());
    if ((hit.part != Part::primary && hit.part != Part::secondary && hit.part != Part::transparent) ||
        hit.index < 0 || hit.index >= dynamic_world->side_count) {
        message("Select a wall to edit its panel."); return;
    }
    const side_data before = *get_side_data(hit.index);
    static const char* classes[] = {"Oxygen", "Shield", "Double shield", "Triple shield",
        "Light switch", "Platform switch", "Tag switch", "Save terminal", "Computer terminal"};
    std::vector<std::string> names;
    short kind, collection;
    for (short i = 0; surface_editor_panel_definition(i, kind, collection); ++i)
        names.push_back(std::to_string(i)+": "+(surface_editor_panel_variant(i) == 1 ? "Chip slot" :
                surface_editor_panel_variant(i) == 2 ? "Breakable tag switch" :
                kind >= 0 && kind < 9 ? classes[kind] : "Panel")+
            " (C"+std::to_string(collection)+")");
    if (names.empty()) { message("No panel definitions available."); return; }
    std::vector<const char*> labels;
    for (const auto& name : names) labels.push_back(name.c_str());
    labels.push_back(nullptr);
    dialog d;
    vertical_placer* layout = new vertical_placer;
    layout->dual_add(new w_title("CONTROL PANEL"), d);
    table_placer* table = new table_placer(2, get_theme_space(ITEM_WIDGET));
    w_toggle* enabled = new w_toggle(SIDE_IS_CONTROL_PANEL(&before) != 0);
    w_select* type = new w_select(std::max(0, std::min(int(names.size())-1, int(before.control_panel_type))), labels.data());
    const std::string target_text = std::to_string(before.control_panel_permutation);
    w_text_entry* target_w = new w_text_entry(6, target_text.c_str());
    auto row = [&](const char* title, widget* w) { table->dual_add(w->label(title), d); table->dual_add(w, d); };
    row("Enabled", enabled); row("Type", type); row("Target ID (light / platform polygon / tag / terminal)", target_w);
    const uint16 masks[] = {_control_panel_status, _side_is_repair_switch, _side_is_destructive_switch,
        _side_is_lighted_switch, _side_switch_can_be_destroyed, _side_switch_can_only_be_hit_by_projectiles};
    const char* flag_names[] = {"Initially on", "Required for exit", "Consumes item", "Requires light",
        "Destroyable", "Projectile activation only"};
    w_toggle* flags[6];
    for (int i=0;i<6;++i) { flags[i]=new w_toggle((before.flags & masks[i]) != 0); row(flag_names[i], flags[i]); }
    layout->add(table, true);
    horizontal_placer* buttons = new horizontal_placer;
    buttons->dual_add(new w_button("ACCEPT", dialog_ok, &d), d);
    buttons->dual_add(new w_button("CANCEL", dialog_cancel, &d), d);
    layout->add(buttons, true); d.set_widget_placer(layout);
    SDL_SetRelativeMouseMode(SDL_FALSE); SDL_ShowCursor(SDL_ENABLE);
    if (d.run() == 0) {
        char* end = nullptr;
        const char* text = target_w->get_text();
        const long target_id = std::strtol(text, &end, 10);
        surface_editor_panel_definition(type->get_selection(), kind, collection);
        bool valid = *text && end && !*end && target_id >= -1 && target_id <= 32767;
        if (kind == _panel_is_light_switch)
            valid = valid && target_id >= 0 && size_t(target_id) < LightList.size() && get_light_data(target_id) != nullptr;
        if (kind == _panel_is_platform_switch) {
            valid = valid && target_id >= 0 && target_id < dynamic_world->polygon_count;
            if (valid) {
                const auto* polygon = get_polygon_data(static_cast<short>(target_id));
                valid = polygon->type == _polygon_is_platform &&
                    polygon->permutation >= 0 &&
                    polygon->permutation < dynamic_world->platform_count;
            }
        }
        if (kind == _panel_is_computer_terminal)
            valid = valid && target_id >= 0;
        // Keep panel placement usable with the standard Aleph One reach check.
        const auto* panel_line = get_line_data(before.line_index);
        const auto is_platform = [](short polygon_index) {
            return polygon_index != NONE &&
                get_polygon_data(polygon_index)->type == _polygon_is_platform;
        };
        const bool platform_boundary = is_platform(panel_line->clockwise_polygon_owner) ||
            is_platform(panel_line->counterclockwise_polygon_owner);
        const bool breakable_tag = kind == _panel_is_tag_switch &&
            (flags[4]->get_selection() || surface_editor_panel_variant(type->get_selection()) == 2);
        const short matched_type = breakable_tag ?
            surface_editor_breakable_panel(before.primary_texture.texture, type->get_selection()) :
            surface_editor_match_panel(type->get_selection(), before.primary_texture.texture);
        if (enabled->get_selection() && hit.part != Part::primary)
            message("Control panels must be placed on the primary wall surface.");
        else if (enabled->get_selection() && platform_boundary)
            message("Use a fixed wall away from the platform boundary. Target the platform polygon ID.");
        else if (enabled->get_selection() && matched_type == NONE)
            message(breakable_tag ? "No breakable tag switch matches this texture. Choose a compatible switch texture." :
                "No matching panel definition for this texture and function. Paint a compatible switch texture first.");
        else if (enabled->get_selection() && !valid) message("Invalid target ID. No changes made.");
        else {
            Change c{}; c.hit=hit; c.panel=true; c.old_panel=before; c.new_panel=before;
            SET_SIDE_CONTROL_PANEL((&c.new_panel), enabled->get_selection());
            if (enabled->get_selection()) {
                c.new_panel.control_panel_type=matched_type;
                c.new_panel.control_panel_permutation=static_cast<int16>(target_id);
                if (breakable_tag) flags[4]->set_selection(true);
                for (int i=0;i<6;++i) {
                    if (flags[i]->get_selection()) c.new_panel.flags |= masks[i];
                    else c.new_panel.flags &= ~masks[i];
                }
            }
            *get_side_data(hit.index)=c.new_panel;
            undo.push_back(c); redo.clear(); init_interpolated_world();
            message("Panel settings updated. Wall texture preserved.");
        }
    }
    SDL_SetRelativeMouseMode(palette_cursor ? SDL_FALSE : SDL_TRUE);
    SDL_ShowCursor(palette_cursor ? SDL_ENABLE : SDL_DISABLE);
    screenshot_last_time=machine_tick_count(); panel_dirty=true;
}

void history(bool forward)
{
    finish_drag();
    auto& from = forward ? redo : undo;
    auto& to = forward ? undo : redo;
    if (from.empty()) { message(forward ? "Nothing to redo." : "Nothing to undo."); return; }
    Change c = from.back();
    if (c.transparent_geometry) {
        restore_geometry(forward ? c.new_geometry : c.old_geometry, c.hit.polygon);
    } else if (c.panel) {
        if (c.hit.index < 0 || c.hit.index >= dynamic_world->side_count) return;
        *get_side_data(c.hit.index) = forward ? c.new_panel : c.old_panel;
        init_interpolated_world();
    } else if (c.height) {
        if (!set_height(c.hit, forward ? c.new_height : c.old_height)) return;
        restore_geometry(forward ? c.new_geometry : c.old_geometry, c.hit.index);
    } else if (!c.offsets.empty()) { set_offsets(c, forward); init_interpolated_world(); }
    else if (c.scenery) {
        const bool adding = forward != c.erased;
        if (adding ? !add_scenery(c) : !remove_scenery(c)) return;
    } else apply(c.hit, forward ? c.after : c.before, forward ? c.new_mode : c.old_mode,
        forward ? c.new_light : c.old_light, c.texture_enabled, c.lighting_enabled);
    from.pop_back();
    to.push_back(c);
    message("");
}

void save(bool save_as)
{
    finish_drag();
    SavedDoorPositions saved_door_positions;
    clear_game_error();
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    if (save_as) {
        FileSpecifier file;
        file.SetToLocalDataDir();
        if (file.WriteDialog(_typecode_scenario, "Save edited level as", "Edited level.sceA")) {
            // Choosing the current destination must preserve its other levels.
            const bool existing_target = save_target_ready && file == save_target;
            const bool source = file == source_map;
            const bool ok = existing_target || source ?
                save_edited_level(file, existing_target ? save_level : source_level) : export_level(file);
            if (ok) {
                save_target = file; save_level = existing_target ? save_level : source ? source_level : 0;
                save_target_ready = true; message("Saved: " + file.GetName());
            } else message("Save failed. Edits remain in memory.");
        }
    } else {
        if (save_target_ready && save_edited_level(save_target, save_level))
            message("Saved: " + save_target.GetName());
        else message("Cannot save this source. Use Save As.");
    }
    SDL_SetRelativeMouseMode(palette_cursor ? SDL_FALSE : SDL_TRUE);
    SDL_ShowCursor(palette_cursor ? SDL_ENABLE : SDL_DISABLE);
    screenshot_last_time = machine_tick_count();
    panel_dirty = true;
}

void begin()
{
    original_fog = fog_enabled();
    if (!save_target_ready || source_map != get_map_file() || source_level != dynamic_world->current_level_number) {
        source_map = get_map_file(); source_level = dynamic_world->current_level_number;
        save_target = source_map; save_level = source_level; save_target_ready = true;
    }
    // Object edits must not be overwritten by the next interpolation restore.
    exit_interpolated_world();
    init_interpolated_world();
    for (short i = 0; i < dynamic_world->side_count; ++i) repair_door_side(i);
    init_interpolated_world();
    animation_started = machine_tick_count();
    textures.clear(); thumbnails.clear(); undo.clear(); redo.clear();
    scenery.clear(); scenery_thumbnails.clear();
    scenery.push_back({-1, 0, 0, false}); // eraser is always first
    index_scenery();
    light_choices.assign(light_steps, NONE);
    selected_light = 0; custom_light = NONE;
    selected = page = selected_scenery = scenery_page = 0;
    scenery_brush = false;
    pick_viewport = {};
    // Enumerate all frames from every texture collection used by this level,
    // including custom/imported collections and their CLUTs.
    std::set<std::pair<int, bool>> collections;
    auto collect = [&](shape_descriptor shape, int16 mode) {
        if (shape != UNONE && is_collection_present(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(shape))))
            collections.insert({GET_DESCRIPTOR_COLLECTION(shape), mode == _xfer_landscape});
    };
    for (int i = 0; i < dynamic_world->polygon_count; ++i) {
        const auto* p = get_polygon_data(i);
        collect(p->floor_texture, p->floor_transfer_mode);
        collect(p->ceiling_texture, p->ceiling_transfer_mode);
    }
    for (int i = 0; i < dynamic_world->side_count; ++i) {
        const auto* s = get_side_data(i);
        collect(s->primary_texture.texture, s->primary_transfer_mode);
        collect(s->secondary_texture.texture, s->secondary_transfer_mode);
        collect(s->transparent_texture.texture, s->transparent_transfer_mode);
    }
    for (const auto& collection : collections) {
        const int count = std::min(int(get_number_of_collection_frames(GET_COLLECTION(collection.first))), MAXIMUM_SHAPES_PER_COLLECTION);
        for (int i = 0; i < count; ++i) {
            const auto shape = static_cast<shape_descriptor>(BUILD_DESCRIPTOR(collection.first, i));
            if (shape == UNONE) continue;
            textures.push_back({shape, collection.second});
        }
    }
    thumbnails.resize(textures.size());
    for (short type = 0; type < scenery_type_count(); ++type) {
        short collection, frame; bool hanging;
        if (get_scenery_preview(type, collection, frame, hanging))
            scenery.push_back({type, collection, frame, hanging});
    }
    scenery_thumbnails.resize(scenery.size());
    screenshot_mode_begin();
    screenshot_roll = 0;
    active = true;
    palette_cursor = true;
    cursor_x = Screen::instance()->width()/2;
    cursor_y = Screen::instance()->height()/2;
    message("");
}

void end()
{
    finish_drag();
    restore_door_positions();
    door_positions.clear();
    door_line_flags.clear();
    SET_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Fog, original_fog);
    active = false;
    palette_cursor = false;
    thumbnails.clear();
    scenery_thumbnails.clear();
    for (int i = 0; i < 8; ++i) {
        if (panels[i]) { SDL_FreeSurface(panels[i]); panels[i] = nullptr; }
#ifdef HAVE_OPENGL
        panel_blitters[i].Unload();
#endif
    }
    init_interpolated_world();
    screenshot_mode_end();
}

void cursor()
{
    finish_drag();
    palette_cursor = !palette_cursor;
    SDL_SetRelativeMouseMode(palette_cursor ? SDL_FALSE : SDL_TRUE);
    SDL_ShowCursor(palette_cursor ? SDL_ENABLE : SDL_DISABLE);
    message("");
}

void scroll(int direction)
{
    if (!direction) return;
    if (palette_cursor && (contains(header_rect(), cursor_x, cursor_y) ||
        contains(header_rect(true), cursor_x, cursor_y))) return;
    if (palette_cursor && !left_collapsed && contains(light_rect(), cursor_x, cursor_y)) return;
    bool right = scenery_brush;
    if (palette_cursor) {
        if (!left_collapsed && contains(panel_rect(), cursor_x, cursor_y)) right = false;
        else if (!right_collapsed && contains(panel_rect(true), cursor_x, cursor_y)) right = true;
        else return;
    }
    const int count = right ? int(scenery.size()) : int(textures.size());
    if (!count) return;
    int& page_number = right ? scenery_page : page;
    int& selection = right ? selected_scenery : selected;
    const int delta = direction > 0 ? -1 : 1;
    if (palette_cursor) {
        const int pages = (count+page_size-1)/page_size;
        page_number = (page_number+delta+pages)%pages;
    } else {
        selection = (selection+delta+count)%count;
        page_number = selection/page_size;
    }
    panel_dirty = true;
}

void choose_map_light()
{
    finish_drag();
    dialog d;
    vertical_placer* layout = new vertical_placer;
    layout->dual_add(new w_title("MAP LIGHT"), d);
    const std::string initial = std::to_string(custom_light == NONE ? 0 : custom_light);
    w_text_entry* id = new w_text_entry(6, initial.c_str());
    id->set_enter_pressed_callback(dialog_try_ok);
    const std::string range_label = "Light ID (0-" +
        std::to_string(int(LightList.size())-1) + ")";
    layout->dual_add(id->label(range_label.c_str()), d);
    layout->dual_add(id, d);
    horizontal_placer* buttons = new horizontal_placer;
    buttons->dual_add(new w_button("ACCEPT", dialog_ok, &d), d);
    buttons->dual_add(new w_button("CANCEL", dialog_cancel, &d), d);
    layout->add(buttons, true); d.set_widget_placer(layout);
    SDL_SetRelativeMouseMode(SDL_FALSE); SDL_ShowCursor(SDL_ENABLE);
    if (d.run() == 0) {
        char* end = nullptr;
        const char* text = id->get_text();
        const long value = std::strtol(text, &end, 10);
        if (*text && end && !*end && value >= 0 && value <= 32767 && size_t(value) < LightList.size() && get_light_data(value) != nullptr) {
            custom_light = static_cast<int>(value);
            apply_lighting = true; apply_texture = false;
            remove_texture = false; edit_panels = false; scenery_brush = false;
            message("Light " + std::to_string(custom_light) + " selected. Click a surface to apply.");
        } else message("Invalid light ID. Enter an existing map light number, not its tag.");
    }
    SDL_SetRelativeMouseMode(palette_cursor ? SDL_FALSE : SDL_TRUE);
    SDL_ShowCursor(palette_cursor ? SDL_ENABLE : SDL_DISABLE);
    screenshot_last_time = machine_tick_count(); panel_dirty = true;
}

void click(int button, int x, int y)
{
    if (palette_cursor) {
        motion(x, y);
        if (contains(actions_rect(), cursor_x, cursor_y)) {
            if (button == SDL_BUTTON_LEFT) {
                const int row = (cursor_y-actions_rect().y)/33;
                if (row < 2) save(row == 1);
                else { SET_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Fog, !fog_enabled()); panel_dirty = true; }
            }
            return;
        }
        for (int side = 0; side < 2; ++side) {
            if (!contains(header_rect(side != 0), cursor_x, cursor_y)) continue;
            if (button == SDL_BUTTON_LEFT) {
                bool& collapsed = side ? right_collapsed : left_collapsed;
                collapsed = !collapsed; panel_dirty = true;
            }
            return;
        }
        if (!left_collapsed && contains(checkbox_rect(), cursor_x, cursor_y)) {
            if (button == SDL_BUTTON_LEFT) {
                const int row = (cursor_y-checkbox_rect().y)/33;
                if (row == 0) texture_motion = (texture_motion+1)%motion_count;
                else if (row == 1) fast_motion = !fast_motion;
                else if (row == 2) apply_texture = !apply_texture;
                else if (row == 3) apply_lighting = !apply_lighting;
                else if (row == 4) align_adjacent = !align_adjacent;
                else if (row == 5) edit_panels = !edit_panels;
                else if (row == 6) { transparent_surface = !transparent_surface; remove_texture = false; edit_panels = false; }
                else if (row == 7) { remove_texture = !remove_texture; transparent_surface = true; edit_panels = false; }
                else if (row == 8) { choose_map_light(); return; }
                scenery_brush = false; message("");
            }
            return;
        }
        if (!left_collapsed && contains(light_rect(), cursor_x, cursor_y)) {
            if (button == SDL_BUTTON_LEFT) {
                const auto r = light_rect();
                const int py = (cursor_y-r.y)*panel_height/r.h-tiles_y;
                const int index = py/light_cell;
                if (py >= 0 && py < light_steps*light_cell && index < light_steps) {
                    selected_light = index; custom_light = NONE; scenery_brush = false; message("");
                }
            }
            return;
        }
        for (int side = 0; side < 2; ++side) {
            if (side ? right_collapsed : left_collapsed) continue;
            const auto r = panel_rect(side != 0);
            if (!contains(r, cursor_x, cursor_y)) continue;
            if (button != SDL_BUTTON_LEFT) return;
            const int px = (cursor_x-r.x)*panel_width/r.w-8;
            const int py = (cursor_y-r.y)*panel_height/r.h-tiles_y;
            if (px < 0 || px >= columns*cell || py < 0 || py >= rows*cell) return;
            const int index = (side ? scenery_page : page)*page_size+py/cell*columns+px/cell;
            const int count = side ? int(scenery.size()) : int(textures.size());
            if (index < count) {
                scenery_brush = side != 0;
                (side ? selected_scenery : selected) = index;
                if (!side) remove_texture = false;
                message("");
            }
            return;
        }
        if ((!status.empty() && contains(footer_rect(), cursor_x, cursor_y)) ||
            !contains(Screen::instance()->view_rect(), cursor_x, cursor_y)) return;
    }
    if (edit_panels && !scenery_brush && button == SDL_BUTTON_LEFT) { edit_control_panel(); return; }
    if (button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_SHIFT)) {
        if (palette_cursor) start_height_drag();
        return;
    }
    if (button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_CTRL)) {
        if (palette_cursor) start_drag();
        return;
    }
    if (button == SDL_BUTTON_RIGHT) paint(true);
    else if (button == SDL_BUTTON_LEFT) {
        if (scenery_brush) place_scenery();
        else paint(false);
    }
}

void draw_panel()
{
    const auto footer = footer_rect();
    for (int i = 0; i < 8; ++i) {
        const int w = i == 7 ? actions_rect().w : i == 2 ? footer.w : i == 3 ? light_width : i == 4 ? checkbox_rect().w : i >= 5 ? header_rect(i == 6).w : panel_width;
        const int h = i == 7 ? 99 : i == 2 ? footer.h : i == 4 ? controls_height : i >= 5 ? header_height : panel_height;
        if (panels[i] && (panels[i]->w != w || panels[i]->h != h)) {
            SDL_FreeSurface(panels[i]); panels[i] = nullptr;
        }
        if (!panels[i]) {
            panels[i] = SDL_CreateRGBSurface(0, w, h, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0);
            panel_dirty = true;
        }
        if (!panels[i]) return;
    }
#ifdef HAVE_OPENGL
    if (OGL_IsActive()) {
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST);
        glDisable(GL_FOG); glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, MainScreenPixelWidth(), MainScreenPixelHeight());
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, Screen::instance()->width(), Screen::instance()->height(), 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    }
#endif
    if (panel_dirty) {
        auto& font = GetOnScreenFont();
        for (int side = 0; side < 8; ++side) {
            SDL_Surface* canvas = panels[side];
            SDL_FillRect(canvas, nullptr, SDL_MapRGB(canvas->format, 20, 23, 27));
            auto label = [&](int y, std::string text) {
                while (!text.empty() && font.TextWidth(text.c_str()) > canvas->w-16) text.pop_back();
                draw_text(canvas, text.c_str(), 8, y, SDL_MapRGB(canvas->format, 225, 229, 232), font.Info, font.Style);
            };
            if (side == 7) {
                for (int row = 0; row < 3; ++row) {
                    SDL_Rect button{4, row*33+3, canvas->w-8, 27};
                    SDL_FillRect(canvas, &button, SDL_MapRGB(canvas->format, 43, 49, 57));
                    label(row*33+22, row == 0 ? "Save" : row == 1 ? "Save As" : fog_enabled() ? "Fog: On" : "Fog: Off");
                }
            } else if (side == 2) {
                label(18, status);
            } else if (side >= 5) {
                SDL_FillRect(canvas, nullptr, SDL_MapRGB(canvas->format, 43, 49, 57));
                label(22, side == 5 ? "Light/Texture" : "Items");
            } else if (side == 3) {
                for (int slot = 0; slot < light_steps; ++slot) {
                    const bool chosen = custom_light == NONE && slot == selected_light;
                    SDL_Rect r{3, tiles_y+slot*light_cell, light_width-6, light_cell-2};
                    SDL_FillRect(canvas, &r, SDL_MapRGB(canvas->format, chosen ? 255 : 55, chosen ? 182 : 59, chosen ? 74 : 64));
                    const int shade = surface_editor_objects::light_level(slot, 255);
                    SDL_Rect inner{r.x+3, r.y+3, r.w-6, r.h-6};
                    SDL_FillRect(canvas, &inner, SDL_MapRGB(canvas->format, shade, shade, shade));
                }
            } else if (side == 4) {
                for (int row = 0; row < 2; ++row) {
                    SDL_Rect button{4, row*33+3, canvas->w-8, 27};
                    SDL_FillRect(canvas, &button, SDL_MapRGB(canvas->format, 43,49,57));
                }
                label(22, std::string("Motion: ")+motion_names[texture_motion]);
                label(55, fast_motion ? "Speed: Fast" : "Speed: Normal");
                for (int row = 6; row < 9; ++row) {
                    SDL_Rect button{4, row*33+3, canvas->w-8, 27};
                    SDL_FillRect(canvas, &button, SDL_MapRGB(canvas->format,
                        row == 7 && remove_texture ? 110 : 43, 49, 57));
                }
                label(220, transparent_surface ? "Surface: Transparent" : "Surface: Solid");
                label(253, remove_texture ? "Remove texture: On" : "Remove texture");
                label(286, custom_light == NONE ? "Map light: Choose ID" : "Map light: " + std::to_string(custom_light));
                for (int row = 0; row < 4; ++row) {
                    const bool checked = row == 0 ? apply_texture : row == 1 ? apply_lighting : row == 2 ? align_adjacent : edit_panels;
                    SDL_Rect box{10, (row+2)*33+8, 17, 17};
                    SDL_FillRect(canvas, &box, SDL_MapRGB(canvas->format, 175, 180, 185));
                    SDL_Rect inside{box.x+2, box.y+2, 13, 13};
                    SDL_FillRect(canvas, &inside, SDL_MapRGB(canvas->format, checked ? 255 : 20, checked ? 182 : 23, checked ? 74 : 27));
                    draw_text(canvas, row == 0 ? "Texture" : row == 1 ? "Lighting" : row == 2 ? "Align adjacent" : "Edit panels", 36, (row+2)*33+22,
                        SDL_MapRGB(canvas->format, 225,229,232), font.Info, font.Style);
                }
            } else {
                const bool is_active = scenery_brush == (side == 1);
                const int selection = side ? selected_scenery : selected;
                const int count = side ? int(scenery.size()) : int(textures.size());
                const int current_page = side ? scenery_page : page;

                label(40, "Page "+std::to_string(current_page+1)+"/"+std::to_string(std::max(1,(count+page_size-1)/page_size)));
                if (count) {
                    if (side) label(60, scenery[selection].type < 0 ? "Eraser" : "Type "+std::to_string(scenery[selection].type)+(scenery[selection].hanging ? " ceiling" : " floor"));
                    else label(60, "C"+std::to_string(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(textures[selection].shape)))+
                        " / "+std::to_string(GET_DESCRIPTOR_SHAPE(textures[selection].shape)));
                } else label(60, "None loaded");
                for (int slot = 0; slot < page_size; ++slot) {
                    const int index = current_page*page_size+slot;
                    if (index >= count) break;
                    SDL_Rect r{8+slot%columns*cell, tiles_y+slot/columns*cell, cell-2, cell-2};
                    const bool highlight = index == selection && is_active;
                    SDL_FillRect(canvas, &r, SDL_MapRGB(canvas->format, highlight ? 255 : 55, highlight ? 182 : 59, highlight ? 74 : 64));
                    if (side == 1 && scenery[index].type < 0) {
                        SDL_Rect icon{r.x+17, r.y+14, 35, 19};
                        SDL_FillRect(canvas, &icon, SDL_MapRGB(canvas->format, 220,145,155));
                        draw_text(canvas, "ERASE", r.x+9, r.y+54, SDL_MapRGB(canvas->format, 245,245,245), font.Info, font.Style);
                    }
                }

            }
#ifdef HAVE_OPENGL
            if (OGL_IsActive()) panel_blitters[side].Load(*canvas);
#endif
        }
        panel_dirty = false;
    }
    for (int side = 0; side < 8; ++side) {
        if (side == 2 && status.empty()) continue;
        if ((side == 0 || side == 3 || side == 4) && left_collapsed) continue;
        if (side == 1 && right_collapsed) continue;
        auto r = side == 7 ? actions_rect() : side >= 5 ? header_rect(side == 6) : side == 2 ? footer : side == 3 ? light_rect() : side == 4 ? checkbox_rect() : panel_rect(side != 0);
#ifdef HAVE_OPENGL
        if (OGL_IsActive()) panel_blitters[side].Draw(r);
        else
#endif
        { SDL_Rect src{0, 0, panels[side]->w, panels[side]->h}; DrawSurface(panels[side], r, src); }
        if (side >= 2) continue;
        const int count = side ? int(scenery.size()) : int(textures.size());
        const int current_page = side ? scenery_page : page;
        auto& previews = side ? scenery_thumbnails : thumbnails;
        for (int slot = 0; slot < page_size; ++slot) {
            const int index = current_page*page_size+slot;
            if (index >= count) break;
            if (side == 1 && scenery[index].type < 0) continue;
            auto& b = previews[index];
            if (!b) {
                const short collection = side ? scenery[index].collection : GET_DESCRIPTOR_COLLECTION(textures[index].shape);
                const short frame = side ? scenery[index].frame : GET_DESCRIPTOR_SHAPE(textures[index].shape);
                const short kind = side ? Shape_Texture_Sprite :
                    (textures[index].landscape ? Shape_Texture_Landscape : Shape_Texture_Wall);
                b.reset(new Shape_Blitter(GET_COLLECTION(collection), frame, kind, GET_COLLECTION_CLUT(collection)));
            }
            if (!b->Width() || !b->Height()) continue;
            const float scale = float(r.w)/panel_width;
            const float scale_y = float(r.h)/panel_height;
            const float size = (cell-8)*std::min(scale, scale_y);
            const float ratio = b->UnscaledWidth()/float(b->UnscaledHeight());
            const float w = ratio >= 1 ? size : size*ratio, h = ratio >= 1 ? size/ratio : size;
            b->Rescale(w, h);
            SDL_Rect dst{r.x+int((8+slot%columns*cell+4)*scale+(size-w)/2),
                r.y+int((tiles_y+slot/columns*cell+4)*scale_y+(size-h)/2), int(w), int(h)};
#ifdef HAVE_OPENGL
            if (OGL_IsActive()) b->OGL_Draw(dst);
            else
#endif
            b->SDL_Draw(MainScreenSurface(), dst);
        }
    }
#ifdef HAVE_OPENGL
    if (OGL_IsActive()) {
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glPopAttrib();
    }
#endif
}

void draw_target(SDL_Surface* pixels)
{
    if (OGL_IsActive()) pick_viewport = Screen::instance()->OpenGLViewPort();
    if (palette_cursor && (over_ui(cursor_x, cursor_y) || !contains(Screen::instance()->view_rect(), cursor_x, cursor_y))) return;
    const Hit hit = target(picking_ray());
    const auto view = Screen::instance()->view_rect();
    const int x = palette_cursor ? (cursor_x-view.x)*pixels->w/std::max(1, view.w) : pixels->w/2;
    const int y = palette_cursor ? (cursor_y-view.y)*pixels->h/std::max(1, view.h) : pixels->h/2;
    const bool erasing = scenery_brush && !scenery.empty() && scenery[selected_scenery].type < 0;
    const bool valid = erasing ? scenery_target() >= 0 : scenery_brush ?
        (!scenery.empty() && hit.part == (scenery[selected_scenery].hanging ? Part::ceiling : Part::floor)) : hit.part != Part::none;
    DisplayTextDest = pixels;
    DisplayTextFont = GetOnScreenFont().Info;
    DisplayTextStyle = GetOnScreenFont().Style;
    DisplayText(x-4, y+4, "+", valid ? 255 : 160, valid ? 190 : 160, valid ? 80 : 160);
    const char* name = erasing ? (valid ? "Erase scenery" : "No scenery") : hit.part == Part::floor ? "Floor" : hit.part == Part::ceiling ? "Ceiling" :
        hit.part == Part::primary ? "Wall" : hit.part == Part::secondary ? "Lower wall" :
        hit.part == Part::transparent ? "Transparent wall" : "No surface";
    char label[120];
    if (erasing) snprintf(label, sizeof(label), "%s", name);
    else if (hit.index < 0 && hit.line >= 0)
        snprintf(label, sizeof(label), "%s (line %d)", hit.part == Part::transparent ? "Empty opening" : "Untextured wall", hit.line);
    else snprintf(label, sizeof(label), "%s%s %d", remove_texture ? "Remove: " : "", name, hit.index);
    DisplayText(x+14, y+4, label);
}

}

bool surface_editor_active() { return surface_editor::active; }
bool surface_editor_palette_cursor() { return surface_editor::active && surface_editor::palette_cursor; }
void surface_editor_begin() { surface_editor::begin(); }
void surface_editor_end() { surface_editor::end(); }
void surface_editor_toggle_cursor() { surface_editor::cursor(); }
void surface_editor_motion(int x, int y) { surface_editor::motion(x, y); }
void surface_editor_release() { surface_editor::finish_drag(); }
void surface_editor_scroll(int direction) { surface_editor::scroll(direction); }
void surface_editor_click(int button, int x, int y) { surface_editor::click(button, x, y); }
void surface_editor_undo(bool redo) { surface_editor::history(redo); }
void surface_editor_save(bool save_as) { surface_editor::save(save_as); }
void surface_editor_use_door() { surface_editor::use_door(); }

#endif
