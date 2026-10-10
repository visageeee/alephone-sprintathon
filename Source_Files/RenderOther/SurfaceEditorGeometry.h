// Geometry-only surface picking for Sprintathon's visual editor. GPL-3.0-or-later.
#ifndef SPRINTATHON_SURFACE_EDITOR_GEOMETRY_H
#define SPRINTATHON_SURFACE_EDITOR_GEOMETRY_H

#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>
#include <utility>

namespace surface_editor_geometry {
enum class Part { none, floor, ceiling, primary, secondary, transparent };
struct Hit {
    Part part = Part::none;
    int index = -1; // polygon for horizontal surfaces, side for walls
    int polygon = -1;
    double distance = 0;
    int line = -1; // boundary for an empty transparent surface
};
// Ten pixels per 0.1 world unit. Round the total delta once (WORLD_ONE is
// binary fixed point), so ten steps equal exactly one world unit. Clamp in
// step space, retaining the original height's offset from the 0.1 grid.
inline int drag_height(int original, int pixels_up, int world_one, int low, int high)
{
    const int min_step = int(std::ceil(double(low-original)*10/world_one));
    const int max_step = int(std::floor(double(high-original)*10/world_one));
    const int steps = std::max(min_step, std::min(max_step, pixels_up/10));
    return original+int(std::lround(double(steps)*world_one/10));
}
struct Ray { double x, y, z, dx, dy, dz; };

// Project onto the original surface plane even when the pointer leaves its
// polygon. Coordinates follow the engine's texture-origin sign conventions.
inline bool texture_drag_coordinates(const Ray& r, bool horizontal,
    double x, double y, double dx, double dy, double length, double top,
    double limit, double& u, double& v)
{
    double t;
    if (horizontal) {
        if (std::abs(r.dz) < 1e-8) return false;
        t = (top-r.z)/r.dz;
        u = -(r.x+t*r.dx); v = -(r.y+t*r.dy);
    } else {
        const double divisor = r.dx*dy-r.dy*dx;
        if (std::abs(divisor) < 1e-8 || length <= 0) return false;
        t = ((x-r.x)*dy-(y-r.y)*dx)/divisor;
        u = -((r.x+t*r.dx-x)*dx+(r.y+t*r.dy-y)*dy)/length;
        v = r.z+t*r.dz;
    }
    return t > 0 && t < limit && std::isfinite(u) && std::isfinite(v);
}

// right/up are offsets on a unit-distance camera plane, not Euler angles.
inline Ray camera_ray(double x, double y, double z, double yaw, double pitch,
    double right, double up, bool sheared)
{
    const double forward = sheared ? 1.0 : std::cos(pitch) - up*std::sin(pitch);
    const double vertical = sheared ? up + std::tan(pitch) : std::sin(pitch) + up*std::cos(pitch);
    return {x, y, z, forward*std::cos(yaw) - right*std::sin(yaw),
        forward*std::sin(yaw) + right*std::cos(yaw), vertical};
}

// Intersect the displayed sprite bounds, including non-solid/zero-radius decor.
inline double sprite_hit(const Ray& ray, double x, double y, double z,
    double yaw, double pitch, double left, double right, double bottom, double top)
{
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const double denominator = ray.dx*cp*cy+ray.dy*cp*sy+ray.dz*sp;
    if (std::abs(denominator) < 1e-10) return -1;
    const double t = ((x-ray.x)*cp*cy+(y-ray.y)*cp*sy+(z-ray.z)*sp)/denominator;
    if (t < 0) return -1;
    const double px = ray.x+t*ray.dx-x, py = ray.y+t*ray.dy-y, pz = ray.z+t*ray.dz-z;
    const double horizontal = -px*sy+py*cy;
    const double vertical = -px*sp*cy-py*sp*sy+pz*cp;
    return horizontal >= left && horizontal <= right && vertical >= bottom && vertical <= top ? t : -1;
}
struct Edge {
    double x, y, end_x, end_y;
    int neighbor = -1, side = -1;
    bool split = false, transparent = false, full = false;
    int line = -1;
};
struct Room {
    double floor, ceiling;
    std::vector<Edge> edges;
};

// Traverse connected convex polygons instead of intersecting every polygon in
// the map. This also distinguishes overlapping Marathon spaces correctly.
template<class GetRoom>
Hit trace(const Ray& ray, int polygon, GetRoom room_at, bool through_transparent,
    std::vector<std::pair<int, double>>* visited = nullptr, bool empty_transparent = false)
{
    double entered = -1e-7;
    for (int step = 0; polygon >= 0 && step < 1024; ++step) {
        if (visited) visited->push_back({polygon, entered});
        const Room room = room_at(polygon);
        double wall_t = std::numeric_limits<double>::infinity();
        const Edge* wall = nullptr;
        for (const auto& edge : room.edges) {
            const double ex = edge.end_x - edge.x, ey = edge.end_y - edge.y;
            const double determinant = ray.dx * ey - ray.dy * ex;
            if (std::abs(determinant) < 1e-12) continue;
            const double ax = edge.x - ray.x, ay = edge.y - ray.y;
            const double t = (ax * ey - ay * ex) / determinant;
            const double u = (ax * ray.dy - ay * ray.dx) / determinant;
            if (t > entered + 1e-7 && u >= -1e-9 && u <= 1.0 + 1e-9 && t < wall_t) {
                wall_t = t;
                wall = &edge;
            }
        }
        if (std::abs(ray.dz) > 1e-12) {
            const bool down = ray.dz < 0;
            const double t = ((down ? room.floor : room.ceiling) - ray.z) / ray.dz;
            if (t >= 0 && t > entered && t <= wall_t)
                return {down ? Part::floor : Part::ceiling, polygon, polygon, t};
        }
        if (!wall) return {};
        const double z = ray.z + wall_t * ray.dz;
        if (wall->neighbor < 0 || wall->full)
            return {Part::primary, wall->side, polygon, wall_t, wall->line};
        const Room adjacent = room_at(wall->neighbor);
        if (z >= adjacent.ceiling || z <= adjacent.floor) {
            return {z <= adjacent.floor && wall->split ? Part::secondary : Part::primary, wall->side, polygon, wall_t, wall->line};
        }
        if ((wall->transparent || empty_transparent) && !through_transparent)
            return {Part::transparent, wall->side, polygon, wall_t, wall->line};
        entered = wall_t;
        polygon = wall->neighbor;
    }
    return {};
}
}
#endif
