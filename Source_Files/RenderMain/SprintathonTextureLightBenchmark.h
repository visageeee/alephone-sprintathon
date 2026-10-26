#ifndef SPRINTATHON_TEXTURE_LIGHT_BENCHMARK_H
#define SPRINTATHON_TEXTURE_LIGHT_BENCHMARK_H
#include <chrono>
#include <cstdio>
#include <cstdlib>
namespace SprintathonTextureLightBenchmark {
inline bool enabled(){static const bool on=[](){const char* p=std::getenv("SPRINTATHON_TEXTURE_LIGHT_BENCHMARK");return p && p[0]=='1';}();return on;}
// First 15 seconds warm up; then ON / OFF / ON, twenty seconds each.
inline int phaseAt(double seconds){return seconds<15?0:seconds<35?1:seconds<55?2:seconds<75?3:4;}
inline const char* label(int phase){return phase==0?"warmup":phase==1?"on-A":phase==2?"off-B":phase==3?"on-A2":"complete";}
struct State {
 using Clock=std::chrono::steady_clock;
 Clock::time_point start{},report{};
 int level=-1,phase=-1;bool started=false;
 unsigned frames=0;double refresh=0,selection=0,sceneryUpload=0;
};
inline State& state(){static State s;return s;}
inline bool suppress(){return enabled() && state().phase==2;}
inline void begin(int level){
 if(!enabled())return;
 auto& s=state();auto now=State::Clock::now();
 if(!s.started || s.level!=level){s=State{};s.started=true;s.level=level;s.start=s.report=now;}
 double elapsed=std::chrono::duration<double>(now-s.start).count();
 int phase=phaseAt(elapsed);
 if(phase!=s.phase){
  s.phase=phase;s.frames=0;s.refresh=s.selection=s.sceneryUpload=0;s.report=now;
  std::fprintf(stderr,"Texture-light benchmark: phase=%s elapsed=%.3f level=%d runtime-texture-lights=%s; baked lighting unchanged; discard first 2 seconds after transitions\n",label(phase),elapsed,level,suppress()?"off":"on");
 }
}
inline void sample(double refresh,double selection,double sceneryUpload){
 if(!enabled())return;
 auto& s=state();++s.frames;s.refresh+=refresh;s.selection+=selection;s.sceneryUpload+=sceneryUpload;
 auto now=State::Clock::now();
 if(std::chrono::duration<double>(now-s.report).count()<1)return;
 std::fprintf(stderr,"Texture-light CPU: phase=%s elapsed=%.3f frames=%u registry-refresh=%.4f emitter-selection=%.4f scenery-upload=%.4f ms/frame (shared texture/scenery preparation)\n",label(s.phase),std::chrono::duration<double>(now-s.start).count(),s.frames,s.refresh/s.frames,s.selection/s.frames,s.sceneryUpload/s.frames);
 s.frames=0;s.refresh=s.selection=s.sceneryUpload=0;s.report=now;
}
}
#endif
