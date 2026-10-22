#ifndef SPRINTATHON_OBJECT_OPS_H
#define SPRINTATHON_OBJECT_OPS_H
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
namespace SprintathonObjectOps {
enum Part { Other, TextureSetup, TextureBind, ShaderState, Lighting, Emitters,
    Refraction, Models, Draw, StateRead, Glow, Count };
static const char* names[Count]={"other","texture-setup","texture-bind","shader-state",
    "lighting","emitters","refraction","models","draw","state-read","glow"};
using Clock=std::chrono::steady_clock;
static bool active=false;
static Part current=Other;
static Clock::time_point changed;
static uint64_t frame=0;
static double ms[Count]={},maxCall[Count]={};
static unsigned calls[Count]={},helpers=0;
inline bool requested(){static const bool on=[](){const char* v=std::getenv("SPRINTATHON_OBJECT_OPS");return v && v[0]=='1';}();return on;}
inline void account(Clock::time_point now){ms[current]+=std::chrono::duration<double,std::milli>(now-changed).count();changed=now;}
inline void publish(){
    if(!helpers)return;
    double total=0;for(double v:ms)total+=v;
    std::fprintf(stderr,"Object ops CPU frame %llu: helpers %u total %.3f ms; exclusive-ms/calls/inclusive-max-ms:",(unsigned long long)frame,helpers,total);
    for(int i=0;i<Count;++i)std::fprintf(stderr," %s %.3f/%u/%.3f",names[i],ms[i],calls[i],maxCall[i]);
    std::fprintf(stderr,"\n");
}
struct Root {
    bool owner=false;
    explicit Root(bool mainWorld){
        if(active || !mainWorld || !requested() || !SprintathonStartupTiming::get().active())return;
        const auto id=SprintathonStartupTiming::get().frame_id;
        if(id!=frame){publish();frame=id;helpers=0;
            for(int i=0;i<Count;++i){ms[i]=maxCall[i]=0;calls[i]=0;}}
        ++helpers;active=owner=true;current=Other;changed=Clock::now();
    }
    ~Root(){if(owner){account(Clock::now());active=false;}}
};
struct Scope {
    bool enabled;Part previous,part;Clock::time_point start;
    explicit Scope(Part p):enabled(active),previous(current),part(p){
        if(enabled){start=Clock::now();account(start);current=p;++calls[p];}
    }
    ~Scope(){if(enabled){const auto now=Clock::now();account(now);
        const double elapsed=std::chrono::duration<double,std::milli>(now-start).count();
        if(elapsed>maxCall[part])maxCall[part]=elapsed;
        current=previous;}}
};
template<class F> auto measure(Part p,F f)->decltype(f()){Scope scope(p);return f();}
}
#endif
