#ifndef SPRINTATHON_PORTAL_TRACE_H
#define SPRINTATHON_PORTAL_TRACE_H
#include "SprintathonStartupTiming.h"
#include <set>
#include <tuple>
#include <functional>
struct SprintathonPortalTrace {
    using Clock=std::chrono::steady_clock;
    bool sampled=false,world=false;
    unsigned sequence=0,rays=0,steps=0,draws=0,surfaces=0;
    unsigned long long vertices=0;
    std::set<std::tuple<uintptr_t,uintptr_t,int>> uniqueSurfaces;
    static SprintathonPortalTrace& get(){static SprintathonPortalTrace s;return s;}
    void begin(){
        const char* p=std::getenv("SPRINTATHON_PORTAL_TRACE");
        sampled=p && p[0]=='1' && SprintathonStartupTiming::get().active();
        if(sampled)sampled=(++sequence==1 || sequence%60==0);
        rays=steps=0;
    }
    void draw(int count){if(world){++draws;if(count>0)vertices+=unsigned(count);}}
    void surface(const void* owner,const void* section,int kind){
        if(world){++surfaces;uniqueSurfaces.emplace(reinterpret_cast<uintptr_t>(owner),reinterpret_cast<uintptr_t>(section),kind);}
    }
};
// Only sampled CPU scopes read clocks. No GL synchronization or new GPU queries.
struct SprintathonPortalScope {
    bool enabled;
    SprintathonPortalTrace::Clock::time_point start;
    std::function<void(double)> finish;
    SprintathonPortalScope(bool on,std::function<void(double)> f):enabled(on),finish(f){if(enabled)start=SprintathonPortalTrace::Clock::now();}
    ~SprintathonPortalScope(){if(enabled)finish(std::chrono::duration<double,std::milli>(SprintathonPortalTrace::Clock::now()-start).count());}
};
struct SprintathonPortalWorld {
    bool enabled;
    SprintathonPortalTrace::Clock::time_point start;
    explicit SprintathonPortalWorld(bool main):enabled(main && SprintathonPortalTrace::get().sampled){
        if(!enabled)return;
        auto& s=SprintathonPortalTrace::get();s.world=true;s.draws=s.surfaces=0;s.vertices=0;s.uniqueSurfaces.clear();start=SprintathonPortalTrace::Clock::now();
    }
    ~SprintathonPortalWorld(){
        if(!enabled)return;
        auto& s=SprintathonPortalTrace::get();s.world=false;
        double ms=std::chrono::duration<double,std::milli>(SprintathonPortalTrace::Clock::now()-start).count();
        fprintf(stderr,"Portal world capture %u frame %llu: CPU-submit %.3f ms; draw-calls %u vertex-or-index-count %llu; opaque-base submissions %u unique-surfaces %zu repeats %zu (not pixel overdraw)\n",
            s.sequence,static_cast<unsigned long long>(SprintathonStartupTiming::get().frame_id),ms,s.draws,s.vertices,s.surfaces,s.uniqueSurfaces.size(),size_t(s.surfaces)-s.uniqueSurfaces.size());
    }
};
#endif
