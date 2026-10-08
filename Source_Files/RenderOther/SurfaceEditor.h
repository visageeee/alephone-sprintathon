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
#include "lightsource.h"
#include "OGL_Setup.h"
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
struct Change {
    Hit hit; Texture before, after; int16 old_mode, new_mode;
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
};
std::vector<Texture> textures;
std::vector<Scenery> scenery;
std::vector<int> light_choices, saved_ids;
std::map<int, short> live_scenery;
int next_id = 0, selected_light = 0;
constexpr int light_steps = 10, light_cell = 57, light_width = 32, header_height = 32;
bool left_collapsed = false, right_collapsed = false;
bool apply_texture = true, apply_lighting = false, align_adjacent = false;
bool dragging = false;
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
void save(bool save_as = false);
bool fog_enabled() { return TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Fog); }
std::string status;
SDL_Surface* panels[8] = {};
#ifdef HAVE_OPENGL
OGL_Blitter panel_blitters[8];
#endif

SDL_Rect panel_rect(bool right = false)
{
    const int h = std::max(1, std::min(Screen::instance()->height()-99-header_height, 1239));
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
    return {0, Screen::instance()->height()-99, r.x+r.w, 99};
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
    if (hit.part == Part::none || hit.index >= dynamic_world->side_count) return {};
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
                s && s->type == _full_side});
        }
        return r;
    };
    return trace(ray, screenshot_polygon, room, (SDL_GetModState() & KMOD_ALT) != 0, visited);
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
void finish_drag()
{
    if (!dragging) return;
    dragging = false;
    bool changed = false;
    for (const auto& e : drag_change.offsets)
        changed = changed || e.old_x != e.new_x || e.old_y != e.new_y;
    if (changed) { undo.push_back(drag_change); redo.clear(); }
    drag_change.offsets.clear();
    init_interpolated_world();
}
void update_drag()
{
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

void paint(bool sample)
{
    const Hit hit = target(picking_ray());
    const auto dst = surface(hit);
    if (!dst.shape) { message("No surface."); return; }
    if (sample) {
        for (size_t i = 0; i < textures.size(); ++i)
            if (textures[i].shape == *dst.shape && textures[i].landscape == (*dst.mode == _xfer_landscape)) {
                selected = int(i); page = selected/page_size; break;
            }
        const double brightness = double(get_light_intensity(*dst.light))/FIXED_ONE;
        selected_light = std::max(0, std::min(light_steps-1, int(std::lround((1-brightness)*(light_steps-1)))));
        scenery_brush = false; message("Sampled."); return;
    }
    const bool tex = apply_texture && !textures.empty();
    const bool illumination = apply_lighting && !light_choices.empty();
    if (!tex && !illumination) { message("No paint channel selected."); return; }
    const Texture next = tex ? textures[selected] : Texture{*dst.shape, *dst.mode == _xfer_landscape};
    const int16 mode = tex ? (next.landscape ? _xfer_landscape :
        (*dst.mode == _xfer_landscape ? _xfer_normal : *dst.mode)) : *dst.mode;
    const int16 light = illumination ? swatch_light(selected_light) : *dst.light;
    if (illumination && light == NONE) { message("Map light limit reached."); return; }
    if (*dst.shape == next.shape && *dst.mode == mode && *dst.light == light) return;
    Change c{hit, {*dst.shape, *dst.mode == _xfer_landscape}, next, *dst.mode, mode};
    c.old_light = *dst.light; c.new_light = light;
    c.texture_enabled = tex; c.lighting_enabled = illumination;
    undo.push_back(c); redo.clear();
    apply(hit, next, mode, light, tex, illumination);
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

void history(bool forward)
{
    finish_drag();
    auto& from = forward ? redo : undo;
    auto& to = forward ? undo : redo;
    if (from.empty()) { message(forward ? "Nothing to redo." : "Nothing to undo."); return; }
    Change c = from.back();
    if (!c.offsets.empty()) { set_offsets(c, forward); init_interpolated_world(); }
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
    textures.clear(); thumbnails.clear(); undo.clear(); redo.clear();
    scenery.clear(); scenery_thumbnails.clear();
    scenery.push_back({-1, 0, 0, false}); // eraser is always first
    index_scenery();
    light_choices.assign(light_steps, NONE);
    selected_light = 0;
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
                if (cursor_y-checkbox_rect().y < 33) apply_texture = !apply_texture;
                else if (cursor_y-checkbox_rect().y < 66) apply_lighting = !apply_lighting;
                else align_adjacent = !align_adjacent;
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
                    selected_light = index; scenery_brush = false; message("");
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
                message("");
            }
            return;
        }
        if ((!status.empty() && contains(footer_rect(), cursor_x, cursor_y)) ||
            !contains(Screen::instance()->view_rect(), cursor_x, cursor_y)) return;
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
        const int h = i == 7 ? 99 : i == 2 ? footer.h : i == 4 ? 99 : i >= 5 ? header_height : panel_height;
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
                    const bool chosen = slot == selected_light;
                    SDL_Rect r{3, tiles_y+slot*light_cell, light_width-6, light_cell-2};
                    SDL_FillRect(canvas, &r, SDL_MapRGB(canvas->format, chosen ? 255 : 55, chosen ? 182 : 59, chosen ? 74 : 64));
                    const int shade = surface_editor_objects::light_level(slot, 255);
                    SDL_Rect inner{r.x+3, r.y+3, r.w-6, r.h-6};
                    SDL_FillRect(canvas, &inner, SDL_MapRGB(canvas->format, shade, shade, shade));
                }
            } else if (side == 4) {
                for (int row = 0; row < 3; ++row) {
                    const bool checked = row == 0 ? apply_texture : row == 1 ? apply_lighting : align_adjacent;
                    SDL_Rect box{10, row*33+8, 17, 17};
                    SDL_FillRect(canvas, &box, SDL_MapRGB(canvas->format, 175, 180, 185));
                    SDL_Rect inside{box.x+2, box.y+2, 13, 13};
                    SDL_FillRect(canvas, &inside, SDL_MapRGB(canvas->format, checked ? 255 : 20, checked ? 182 : 23, checked ? 74 : 27));
                    draw_text(canvas, row == 0 ? "Texture" : row == 1 ? "Lighting" : "Align adjacent", 36, row*33+22,
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
    else snprintf(label, sizeof(label), "%s %d", name, hit.index);
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
#endif
