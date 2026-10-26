#ifndef SPRINTATHON_BATCH_LIGHT_TIMING_H
#define SPRINTATHON_BATCH_LIGHT_TIMING_H
#include "SprintathonTextureLightBenchmark.h"
#include <array>
namespace SprintathonBatchLightTiming {
enum Stage {Dynamic,TextureFilter,Projectile,Records,Surface,ParametersUpload,LightsUpload,Count};
using Clock=std::chrono::steady_clock;
struct State {bool active=false;int stage=-1,phase=-1;unsigned frames=0;Clock::time_point stamp;std::array<double,Count> ms{};std::array<unsigned,Count> calls{};};
inline State& state(){static State s;return s;}
inline void charge(){auto& s=state();auto now=Clock::now();if(s.stage>=0)s.ms[s.stage]+=std::chrono::duration<double,std::milli>(now-s.stamp).count();s.stamp=now;}
inline void report(){auto& s=state();if(!s.frames)return;
 const char* names[]={"dynamic-other","texture-filter","projectile","record-pack","surface-detail","parameter-upload","light-upload"};
 std::fprintf(stderr,"Batch light CPU: phase=%s frames=%u exclusive ms/frame:",s.phase<0?"normal":SprintathonTextureLightBenchmark::label(s.phase),s.frames);
 for(int i=0;i<Count;++i)std::fprintf(stderr," %s=%.4f",names[i],s.ms[i]/s.frames);
 std::fprintf(stderr,"; calls/frame:");for(int i=0;i<Count;++i)std::fprintf(stderr," %s=%.1f",names[i],double(s.calls[i])/s.frames);
 std::fprintf(stderr,"\n");s.frames=0;s.ms.fill(0);s.calls.fill(0);
}
inline void begin(bool active){auto& s=state();int phase=SprintathonTextureLightBenchmark::enabled()?SprintathonTextureLightBenchmark::state().phase:-1;
 if(phase!=s.phase || !active)report();
 s.phase=phase;s.active=active;s.stage=-1;}
struct Scope {bool active;int previous;
 explicit Scope(Stage stage):active(state().active),previous(state().stage){if(active){charge();state().stage=stage;++state().calls[stage];}}
 ~Scope(){if(active){charge();state().stage=previous;}}
 Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
inline void finish(){auto& s=state();if(s.active && ++s.frames>=60)report();s.active=false;s.stage=-1;}
}
#endif
