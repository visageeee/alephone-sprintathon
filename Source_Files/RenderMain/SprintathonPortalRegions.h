#ifndef SPRINTATHON_PORTAL_REGIONS_H
#define SPRINTATHON_PORTAL_REGIONS_H
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

// Acceleration data only: retain every original polygon transition, node,
// automap update, clipping decision and transparent-surface ordering.
namespace SprintathonPortalRegions {
struct Member {
    short polygon;short floor,ceiling,type,count;
    std::array<short,MAXIMUM_VERTICES_PER_POLYGON> endpoints,adjacent,lines,sides;
    std::array<unsigned,MAXIMUM_VERTICES_PER_POLYGON> points;
    int exit=-1;
};
struct Point {short index;world_point2d position;};
struct Region {
    std::vector<Member> members;std::vector<Point> points;
    std::vector<int64_t> cross;
    uint64_t checked=0;bool valid=true,rayValid=false;
    world_point2d origin{};long_vector2d direction{};
};
struct Address {int region=-1,member=-1;};
static std::vector<Region> regions;
static std::vector<Address> addresses;
static uint64_t frame=0;
static bool active=false;
static unsigned hits=0,builds=0,corners=0,invalidations=0;
inline bool requested(){const char* p=std::getenv("SPRINTATHON_PORTAL_REGIONS");return p && p[0]=='1';}
inline bool eligible(short index){
    auto* p=get_polygon_data(index);
    if(POLYGON_IS_DETACHED(p) || p->type!=_polygon_is_normal || p->vertex_count<3)return false;
    for(int i=0;i<p->vertex_count;++i)
        if(p->adjacent_polygon_indexes[i]!=NONE &&
           get_polygon_data(p->adjacent_polygon_indexes[i])->type==_polygon_is_platform)return false;
    return true;
}
inline void preload(){
    regions.clear();addresses.clear();active=false;
    if(!requested())return;
    addresses.resize(dynamic_world->polygon_count);
    std::vector<bool> assigned(addresses.size(),false);
    unsigned grouped=0;
    for(int seed=0;seed<dynamic_world->polygon_count;++seed){
        if(assigned[seed] || !eligible(seed))continue;
        std::vector<short> members(1,short(seed));assigned[seed]=true;
        // Small connected groups bound speculative work on rays that only
        // cross a corner. Convexity is unnecessary: no polygons are skipped.
        for(size_t n=0;n<members.size() && members.size()<8;++n){
            auto* p=get_polygon_data(members[n]);
            for(int e=0;e<p->vertex_count && members.size()<8;++e){
                short next=p->adjacent_polygon_indexes[e];
                if(next==NONE || assigned[next] || !eligible(next))continue;
                auto* q=get_polygon_data(next);auto* line=get_line_data(p->line_indexes[e]);
                if(p->floor_height!=q->floor_height || p->ceiling_height!=q->ceiling_height ||
                   !LINE_IS_TRANSPARENT(line) || LINE_HAS_TRANSPARENT_SIDE(line) ||
                   p->side_indexes[e]!=NONE)continue;
                bool reverse=false;
                for(int j=0;j<q->vertex_count;++j)
                    if(q->line_indexes[j]==p->line_indexes[e] && q->adjacent_polygon_indexes[j]==members[n] && q->side_indexes[j]==NONE)reverse=true;
                if(!reverse)continue;
                members.push_back(next);assigned[next]=true;
            }
        }
        if(members.size()<2)continue;
        Region r;std::map<short,unsigned> pointIds;
        for(short index:members){
            auto* p=get_polygon_data(index);Member m{};
            m.polygon=index;m.floor=p->floor_height;m.ceiling=p->ceiling_height;m.type=p->type;m.count=p->vertex_count;
            for(int e=0;e<m.count;++e){
                short point=p->endpoint_indexes[e];
                auto found=pointIds.emplace(point,unsigned(r.points.size()));
                if(found.second)r.points.push_back({point,get_endpoint_data(point)->vertex});
                m.endpoints[e]=point;m.points[e]=found.first->second;
                m.adjacent[e]=p->adjacent_polygon_indexes[e];m.lines[e]=p->line_indexes[e];m.sides[e]=p->side_indexes[e];
            }
            addresses[index].region=int(regions.size());addresses[index].member=int(r.members.size());r.members.push_back(m);
        }
        grouped+=unsigned(members.size());r.cross.resize(r.points.size());regions.push_back(std::move(r));
    }
    std::fprintf(stderr,"Portal regions: %u polygons in %zu prepared groups (experimental)\n",grouped,regions.size());
}
inline void begin(){
    ++frame;active=requested() && addresses.size()==size_t(dynamic_world->polygon_count);
    hits=builds=corners=invalidations=0;
}
inline bool validate(Region& r){
    if(!r.valid)return false;
    if(r.checked==frame)return true;
    r.checked=frame;r.rayValid=false;
    for(const auto& m:r.members){
        auto* p=get_polygon_data(m.polygon);
        if(POLYGON_IS_DETACHED(p) || p->type!=m.type || p->floor_height!=m.floor || p->ceiling_height!=m.ceiling || p->vertex_count!=m.count){r.valid=false;break;}
        for(int e=0;e<m.count;++e)
            if(p->endpoint_indexes[e]!=m.endpoints[e] || p->adjacent_polygon_indexes[e]!=m.adjacent[e] || p->line_indexes[e]!=m.lines[e] || p->side_indexes[e]!=m.sides[e]){r.valid=false;break;}
        if(!r.valid)break;
    }
    if(r.valid)for(const auto& point:r.points){
        const auto& now=get_endpoint_data(point.index)->vertex;
        if(now.x!=point.position.x || now.y!=point.position.y){r.valid=false;break;}
    }
    if(!r.valid)++invalidations;
    return r.valid;
}
inline bool crossing(short polygon,const world_point2d& origin,const long_vector2d& direction,int& edge){
    if(!active || polygon<0 || size_t(polygon)>=addresses.size())return false;
    const auto& address=addresses[polygon];if(address.region<0)return false;
    auto& r=regions[address.region];if(!validate(r))return false;
    if(!r.rayValid || r.origin.x!=origin.x || r.origin.y!=origin.y || r.direction.i!=direction.i || r.direction.j!=direction.j){
        r.origin=origin;r.direction=direction;r.rayValid=true;++builds;
        for(size_t i=0;i<r.points.size();++i){const auto& p=r.points[i].position;
            r.cross[i]=int64_t(int32_t(p.x)-origin.x)*direction.j-int64_t(int32_t(p.y)-origin.y)*direction.i;
        }
        for(auto& m:r.members){
            m.exit=-1;bool corner=false;int exits=0;
            for(int e=0;e<m.count;++e){
                int64_t a=r.cross[m.points[e]],b=r.cross[m.points[(e+1)%m.count]];
                if(a==0 || b==0)corner=true;
                if(a>0 && b<0){m.exit=e;++exits;}
            }
            // Original biased/split-ray handling remains authoritative at vertices.
            if(corner || exits!=1)m.exit=-1;
        }
    }
    edge=r.members[address.member].exit;
    if(edge<0){++corners;return false;}
    ++hits;return true;
}
}
#endif
