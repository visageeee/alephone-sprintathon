// Standalone: g++ -std=c++17 tests/surface_editor_geometry_test.cpp -o /tmp/surface-editor-test
#include "../Source_Files/RenderOther/SurfaceEditorGeometry.h"
#include "../Source_Files/RenderOther/SurfaceEditorObjects.h"
#include <cassert>
#include <iostream>
using namespace surface_editor_geometry;

Room box(double x0, double x1, double floor, double ceiling, int side)
{
    return {floor, ceiling, {{x0, 0, x1, 0, -1, side}, {x1, 0, x1, 10, -1, side+1},
        {x1, 10, x0, 10, -1, side+2}, {x0, 10, x0, 0, -1, side+3}}};
}
int main()
{
    std::vector<Room> rooms{box(0, 10, 0, 10, 0), box(10, 20, 2, 8, 4)};
    auto room = [&](int i) { return rooms.at(i); };
    auto pick = [&](Ray ray, bool through = false) { return trace(ray, 0, room, through); };
    auto expect = [](Hit h, Part part, int index) { assert(h.part == part && h.index == index); };
    expect(pick({5,5,5,1,0,0}), Part::primary, 1);
    expect(pick({5,5,5,0,0,-1}), Part::floor, 0);
    expect(pick({5,5,5,0,0,1}), Part::ceiling, 0);
    expect(pick({5,5,5,1,0,-2}), Part::floor, 0);
    expect(pick({5,5,5,-1,0,0}), Part::primary, 3);
    rooms[0].edges[1].neighbor = 1;
    rooms[0].edges[1].split = true;
    rooms[1].edges[3].neighbor = 0;
    // A clear portal selects the far wall, not the invisible portal plane.
    expect(pick({5,5,5,1,0,0}), Part::primary, 5);
    expect(pick({5,5,1,1,0,0}), Part::secondary, 1);
    expect(pick({5,5,9,1,0,0}), Part::primary, 1);
    // Horizontal surfaces in the next room are picked with that room's index.
    expect(pick({5,5,5,1,0,-0.4}), Part::floor, 1);
    expect(pick({5,5,5,1,0,0.4}), Part::ceiling, 1);
    rooms[0].edges[1].transparent = true;
    expect(pick({5,5,5,1,0,0}), Part::transparent, 1);
    expect(pick({5,5,5,1,0,0}, true), Part::primary, 5);
    rooms[0].edges[1].full = true;
    expect(pick({5,5,5,1,0,0}, true), Part::primary, 1);
    rooms[0].edges[1].side = -1;
    expect(pick({5,5,5,1,0,0}), Part::none, -1);
    // Degenerate direction and exact portal starts must terminate safely.
    expect(pick({5,5,5,0,0,0}), Part::none, -1);
    expect(trace({10,5,5,1,0,0}, 1, room, false), Part::primary, 5);
    // World-coordinate extremes are doubles, avoiding 16-bit projectile wrap.
    rooms[0] = box(32000, 32760, -1000, 1000, 0);
    expect(pick({32700,5,0,1,0,0}), Part::primary, 1);
    auto close = [](double a, double b) { assert(std::abs(a-b) < 1e-8); };
    rooms[0] = box(0,10,0,10,0);
    // Off-centre pointer rays reach different points on the wall, not the
    // screen-centre target. Hit distance also supplies scenery-placement XYZ.
    auto ray = camera_ray(5,5,5,0,0,0.4,0.2,false);
    auto hit = pick(ray);
    expect(hit, Part::primary, 1);
    assert(hit.polygon == 0);
    close(ray.x+ray.dx*hit.distance, 10);
    close(ray.y+ray.dy*hit.distance, 7);
    close(ray.z+ray.dz*hit.distance, 6);
    ray = camera_ray(5,5,5,0,0,0,-2,false);
    hit = pick(ray);
    expect(hit, Part::floor, 0);
    close(ray.x+ray.dx*hit.distance, 7.5);
    close(ray.z+ray.dz*hit.distance, 0);
    // Yaw rotates the entire cursor ray; pitch rotates forward and up together.
    constexpr double pi = 3.141592653589793;
    ray = camera_ray(5,5,5,pi/2,0,0.4,0.2,false);
    hit = pick(ray);
    expect(hit, Part::primary, 2);
    close(ray.x+ray.dx*hit.distance, 3);
    close(ray.y+ray.dy*hit.distance, 10);
    ray = camera_ray(5,5,5,0,pi/4,0.2,0.5,false);
    close(ray.dx, std::sqrt(0.5)*0.5);
    close(ray.dy, 0.2);
    close(ray.dz, std::sqrt(0.5)*1.5);
    expect(pick(ray), Part::ceiling, 0);
    // Classic software-style perspective shears vertically instead of rotating.
    ray = camera_ray(5,5,5,0,pi/4,0.4,0.5,true);
    close(ray.dx, 1); close(ray.dy, 0.4); close(ray.dz, 1.5);
    expect(pick(ray), Part::ceiling, 0);
    // Scenery erasing uses visual sprite bounds, including zero-radius decor.
    const Ray eraser{0,0,1,1,0,0};
    close(sprite_hit(eraser,5,0,0,0,0,-1,1,0,2),5);
    assert(sprite_hit(eraser,5,3,0,0,0,-1,1,0,2) < 0);
    assert(sprite_hit(eraser,-5,0,0,0,0,-1,1,0,2) < 0);
    assert(sprite_hit(eraser,5,0,3,0,0,-1,1,0,2) < 0);
    const Ray tilted{0,0,0,std::sqrt(0.5),0,std::sqrt(0.5)};
    close(sprite_hit(tilted,5,0,5,0,pi/4,-1,1,-1,1),std::sqrt(50.0));
    // Portal traversal records distinguish overlapping but disconnected spaces.
    rooms[0] = box(0,10,0,10,0);
    rooms[0].edges[1].neighbor = 1;
    rooms[1] = box(10,20,0,10,4);
    rooms[1].edges[3].neighbor = 0;
    std::vector<std::pair<int,double>> visited;
    hit = trace({5,5,5,1,0,0},0,room,false,&visited);
    assert(visited.size()==2 && visited[0].first==0 && visited[1].first==1);
    close(visited[1].second,5); close(hit.distance,15);
    // A solid wall blocks sprites beyond it; an empty hit can't bypass it.
    rooms[0].edges[1].neighbor = -1;
    visited.clear();
    hit = trace({5,5,5,1,0,0},0,room,false,&visited);
    assert(visited.size()==1); close(hit.distance,5);
    // Removing saved scenery remaps ambient-source indices, not geometry.
    std::vector<int16_t> indexes{20,30,-1,1,4,8,-1,4,8,-1};
    surface_editor_objects::remap_sound_sources(indexes, {3,3,7,-1}, 2);
    assert((indexes==std::vector<int16_t>{20,30,-1,1,3,7,-1,3,7,-1}));
    surface_editor_objects::remap_sound_sources(indexes, {3,7}, 9);
    assert(indexes[4]==3 && indexes[5]==7);
    // Original map objects may appear anywhere in a polygon chain.
    std::vector<int> links{2,-1,1};
    auto valid = [](int) { return true; };
    auto next = [&](int i) { return links[i]; };
    for (int target : {0,1,2})
        assert(surface_editor_objects::removable(0,target,3,valid,next));
    assert(!surface_editor_objects::removable(-1,0,3,valid,next));
    assert(!surface_editor_objects::removable(1,0,3,valid,next));
    links[2] = 7; // out of range
    assert(!surface_editor_objects::removable(0,0,3,valid,next));
    links[2] = 0; // cycle
    assert(!surface_editor_objects::removable(0,0,3,valid,next));
    links[2] = -2; // invalid negative index
    assert(!surface_editor_objects::removable(0,0,3,valid,next));
    assert(!surface_editor_objects::removable(0,0,3,[](int){ return false; },next));
    assert(surface_editor_objects::light_level(0,65536)==65536);
    assert(surface_editor_objects::light_level(9,65536)==0);
    for (int i=1;i<10;++i)
        assert(surface_editor_objects::light_level(i,65536)<surface_editor_objects::light_level(i-1,65536));
    // Ctrl-drag projects onto the grabbed plane, not a new target surface.
    double u, v;
    assert(texture_drag_coordinates({0,0,5,1,2,-1},true,0,0,0,0,0,0,100,u,v));
    close(u,-5); close(v,-10);
    assert(texture_drag_coordinates({0,0,5,1,2,1},true,0,0,0,0,0,10,100,u,v));
    close(u,-5); close(v,-10);
    assert(texture_drag_coordinates({0,0,5,1,0.4,0.2},false,10,0,0,10,10,10,100,u,v));
    close(u,-4); close(v,7);
    assert(!texture_drag_coordinates({0,0,5,1,0,0},true,0,0,0,0,0,0,100,u,v));
    assert(!texture_drag_coordinates({0,0,5,0,1,0},false,10,0,0,10,10,10,100,u,v));
    assert(!texture_drag_coordinates({0,0,5,-1,0,0},false,10,0,0,10,10,10,100,u,v));
    std::cout << "Surface, cursor, scenery-picking and saved-reference checks passed\n";
}
