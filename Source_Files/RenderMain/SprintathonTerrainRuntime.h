#ifndef SPRINTATHON_TERRAIN_RUNTIME_H
#define SPRINTATHON_TERRAIN_RUNTIME_H
#include "SprintathonTerrain.h"
#include "map.h"
#include "player.h"
#include "render.h"
#include "shell.h"
#include "screen.h"
#include "vbl.h"
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <map>
#include <cstring>
namespace SprintathonTerrainRuntime {
inline const char* path(){static const char* p=std::getenv("SPRINTATHON_TERRAIN_MESH");return p;}
inline bool enabled(){
#ifdef HAVE_OPENGL
    return path() && *path() && !game_is_networked && !game_is_being_replayed() &&
        get_screen_mode()->acceleration==_opengl_acceleration && current_player && current_player->object_index>=0;
#else
    return false;
#endif
}
inline bool worldSpace(){const char* p=std::getenv("SPRINTATHON_TERRAIN_WORLD");return p && !std::strcmp(p,"1");}
struct Prepared {
    SprintathonTerrain::DrawMesh mesh;
    SprintathonTerrain::HeightIndex index;
    std::vector<double> key;
    std::vector<float> originalPositions;
    std::vector<SprintathonTerrain::Point> boundary;
};
struct State {
    bool loaded=false,failed=false;
    int selected=-1,level=-1,lastTick=-1;
    SprintathonTerrain::Mesh source;
    std::map<short,Prepared> cells;
};
// Inline-function local static is one object across rendering and physics TUs.
inline State& state(){static State s;return s;}
inline void reset(){state()=State{};}
inline const SprintathonTerrain::DrawMesh* meshFor(polygon_data* polygon){
    if(!enabled() || !polygon)return nullptr;
    State& s=state();
    if(s.level!=dynamic_world->current_level_number || dynamic_world->tick_count<s.lastTick){
        s.selected=-1;s.cells.clear();
        s.level=dynamic_world->current_level_number;
    }
    s.lastTick=dynamic_world->tick_count;
    if(s.failed)return nullptr;
    try {
        if(!s.loaded){s.source=SprintathonTerrain::load(path());s.loaded=true;
            std::fprintf(stderr,"Experimental terrain: loaded %zu triangles; player collision enabled\n",s.source.faces.size());}
        if(!worldSpace() && s.selected<0){const char* id=std::getenv("SPRINTATHON_TERRAIN_POLYGON");
            if(id && *id){char* end=nullptr;long n=std::strtol(id,&end,10);
                SprintathonTerrain::require(end && !*end && n>=0 && n<dynamic_world->polygon_count,"Invalid terrain polygon index");s.selected=int(n);}
            else s.selected=get_object_data(current_player->object_index)->polygon;
            std::fprintf(stderr,"Experimental terrain: host polygon %d\n",s.selected);
        }
        short cell=NONE;
        if(worldSpace()){
            // Scope world-space imports to the generated test level. Launching
            // another level with these environment variables cannot replace it.
            if(std::strncmp(static_world->level_name,"Myth terrain import",19))return nullptr;
            for(short i=0;i<dynamic_world->polygon_count;++i)if(polygon==get_polygon_data(i)){cell=i;break;}
        }else if(s.selected>=0 && s.selected<dynamic_world->polygon_count && polygon==get_polygon_data(short(s.selected)))cell=short(s.selected);
        if(cell==NONE)return nullptr;
        Prepared& prepared=s.cells[cell];
        // Liquid/landscape/platform floors have additional movement semantics.
        if(polygon->type==_polygon_is_platform || polygon->floor_transfer_mode!=_xfer_normal ||
           polygon->floor_texture==UNONE || polygon->media_index!=NONE)return nullptr;
        std::vector<SprintathonTerrain::Point> boundary;
        std::vector<double> key={double(s.level),double(cell),double(polygon->floor_height),double(polygon->ceiling_height)};
        for(int i=0;i<polygon->vertex_count;++i){auto p=get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            boundary.push_back({double(p.x),double(p.y),0,0,0});key.push_back(p.x);key.push_back(p.y);}
        if(key!=prepared.key){prepared.key=key;prepared.mesh=SprintathonTerrain::DrawMesh{};
            try{
                auto mesh=SprintathonTerrain::fit(s.source,boundary,polygon->floor_height,polygon->ceiling_height,WORLD_ONE,worldSpace());
                prepared.index.build(mesh);prepared.originalPositions=mesh.positions;prepared.boundary=boundary;
                if(worldSpace()){
                    double x0=1e30,y0=1e30,x1=-1e30,y1=-1e30;
                    for(const auto& p:s.source.points){x0=std::min(x0,p.x*WORLD_ONE);x1=std::max(x1,p.x*WORLD_ONE);
                        y0=std::min(y0,p.y*WORLD_ONE);y1=std::max(y1,p.y*WORLD_ONE);}
                    prepared.boundary={{x0,y0,0,0,0},{x1,y0,0,0,0},{x1,y1,0,0,0},{x0,y1,0,0,0}};
                }prepared.mesh=std::move(mesh);
            }catch(const std::exception& e){std::fprintf(stderr,"Experimental terrain: host rejected: %s; flat floor retained\n",e.what());}
        }
        return prepared.mesh.positions.empty()?nullptr:&prepared.mesh;
    }catch(const std::exception& e){s.failed=true;std::fprintf(stderr,"Experimental terrain disabled: %s\n",e.what());return nullptr;}
}
inline bool height(short polygon,double x,double y,double& z){
    if(!enabled() || polygon<0 || polygon>=dynamic_world->polygon_count)return false;
    const auto* mesh=meshFor(get_polygon_data(polygon));
    return mesh && state().cells.at(polygon).index.height(*mesh,x,y,z);
}
inline world_distance floorAt(short polygon,double x,double y){
    double z;if(height(polygon,x,y,z))return static_cast<world_distance>(std::lround(z));
    return get_polygon_data(polygon)->floor_height;
}
inline bool normalAt(short polygon,double x,double y,std::array<double,3>& normal){
    if(!enabled() || polygon<0 || polygon>=dynamic_world->polygon_count)return false;
    const auto* m=meshFor(get_polygon_data(polygon));double z;
    return m && state().cells.at(polygon).index.height(*m,x,y,z,&normal);
}
inline bool segment(short polygon,const world_point3d& a,const world_point3d& b,world_point3d& hit){
    if(!enabled() || polygon<0 || polygon>=dynamic_world->polygon_count)return false;
    const auto* m=meshFor(get_polygon_data(polygon));if(!m)return false;
    SprintathonTerrain::Point p{double(a.x),double(a.y),double(a.z),0,0},q{double(b.x),double(b.y),double(b.z),0,0};
    double t;if(!state().cells.at(polygon).index.segment(*m,p,q,t))return false;
    hit.x=static_cast<world_distance>(std::lround(a.x+(double(b.x)-a.x)*t));
    hit.y=static_cast<world_distance>(std::lround(a.y+(double(b.y)-a.y)*t));
    hit.z=static_cast<world_distance>(std::lround(a.z+(double(b.z)-a.z)*t));
    // Keep integer-rounded impacts on the visible side of the mesh.
    hit.z=std::max(hit.z,floorAt(polygon,hit.x,hit.y));return true;
}

inline bool dent(short polygon,const world_point3d& impact){
    if(!enabled() || polygon<0 || polygon>=dynamic_world->polygon_count)return false;
    const auto* mesh=meshFor(get_polygon_data(polygon));if(!mesh)return false;
    double ground;if(!state().cells.at(polygon).index.height(*mesh,impact.x,impact.y,ground) || std::abs(impact.z-ground)>8)return false;
    // Deform all cells within reach, using the same world-space bowl. The
    // shared outer boundary (not cell boundaries) controls perimeter fade.
    bool changed=false;
    const short first=worldSpace()?0:polygon,last=worldSpace()?dynamic_world->polygon_count:polygon+1;
    for(short i=first;i<last;++i){
        const auto* m=meshFor(get_polygon_data(i));if(!m)continue;
        auto& p=state().cells.at(i);
        if(impact.x+1.25*WORLD_ONE<p.index.x0 || impact.x-1.25*WORLD_ONE>p.index.x1 ||
           impact.y+1.25*WORLD_ONE<p.index.y0 || impact.y-1.25*WORLD_ONE>p.index.y1)continue;
        changed=SprintathonTerrain::crater(p.mesh,p.originalPositions,p.boundary,
            impact.x,impact.y,1.25*WORLD_ONE,.18*WORLD_ONE,.6*WORLD_ONE,WORLD_ONE) || changed;
    }
    return changed;
}

}
#endif
