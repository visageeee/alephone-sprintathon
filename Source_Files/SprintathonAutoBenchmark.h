#ifndef SPRINTATHON_AUTO_BENCHMARK_H
#define SPRINTATHON_AUTO_BENCHMARK_H
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <array>
#include <string>
namespace SprintathonAutoBenchmark {
inline bool enabled(){const char* v=std::getenv("SPRINTATHON_AUTO_BENCHMARK");return v && v[0]=='1';}
struct Sample{uint64_t frame;double time,ms;std::array<double,10> stages;int tick;};
struct State{
 bool started=false,measuring=false,done=false;int level=-1;uint64_t frame=0;
 std::chrono::steady_clock::time_point start,previous,measurement;
 std::vector<Sample> samples;
};
inline State& state(){static State s;return s;}
inline void finish(const char* status){
 auto& s=state();if(s.done)return;s.done=true;
 const char* path=std::getenv("SPRINTATHON_BENCHMARK_CSV");
 if(!path || !*path){std::fprintf(stderr,"Benchmark: missing CSV path\n");return;}
 FILE* f=std::fopen(path,"w");bool ok=f!=nullptr;
 if(f){
  ok=std::fprintf(f,"frame_id,time_s,frame_ms,other_ms,events_ms,timers_ms,update_ms,interpolation_ms,render_ms,swap_ms,limiter_ms,visibility_ms,hud_ms,tick,level\n")>=0;
  for(const auto& v:s.samples){
   if(std::fprintf(f,"%llu,%.6f,%.6f",static_cast<unsigned long long>(v.frame),v.time,v.ms)<0)ok=false;
   for(double x:v.stages)if(std::fprintf(f,",%.6f",x)<0)ok=false;
   if(std::fprintf(f,",%d,%d\n",v.tick,s.level)<0)ok=false;
  }
  if(std::fclose(f)!=0)ok=false;
 }
 const std::string statusPath=std::string(path)+".status";
 f=std::fopen(statusPath.c_str(),"w");
 if(f){std::fprintf(f,"%s\n",ok?status:"write_failed");std::fclose(f);}
 std::fprintf(stderr,"Benchmark: %s, %u frames, %s\n",ok?status:"write_failed",unsigned(s.samples.size()),path);
}
inline void context(int mode,int level){
 if(!enabled() || state().done || !state().started)return;
 if(mode!=0 || level!=state().level)finish("interrupted");
}
inline void present(int mode,int level,int tick,const double* stages){
 if(!enabled() || state().done || mode!=0)return;
 auto& s=state();++s.frame;const auto now=std::chrono::steady_clock::now();
 if(!s.started){s.started=true;s.level=level;s.start=s.previous=now;s.samples.reserve(65536);
  std::fprintf(stderr,"Benchmark: level index %d; warm-up 10 seconds, measurement 30 seconds. Leave controls untouched.\n",level);return;}
 if(!s.measuring){
  if(std::chrono::duration<double>(now-s.start).count()>=10){s.measuring=true;s.measurement=now;std::fprintf(stderr,"Benchmark: measuring\n");}
  s.previous=now;return;
 }
 Sample v;v.frame=s.frame;v.time=std::chrono::duration<double>(now-s.measurement).count();
 v.ms=std::chrono::duration<double,std::milli>(now-s.previous).count();v.tick=tick;
 for(int i=0;i<10;++i)v.stages[i]=stages[i];s.samples.push_back(v);s.previous=now;
 if(v.time>=30)finish("complete");
}
}
#endif
