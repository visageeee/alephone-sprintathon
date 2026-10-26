#ifndef SPRINTATHON_PRESENT_TRACE_H
#define SPRINTATHON_PRESENT_TRACE_H
#include "OGL_Headers.h"
#ifdef HAVE_OPENGL
#include <SDL2/SDL.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
// Timestamp queries coexist with GL_TIME_ELAPSED. Results are read only after
// availability, never by blocking or inserting glFinish. Shared across TUs.
namespace SprintathonPresentTrace {
#if defined(_WIN32) || defined(__WIN32__)
#define SPRINTATHON_PRESENT_CALL __stdcall
#elif defined(APIENTRY)
#define SPRINTATHON_PRESENT_CALL APIENTRY
#else
#define SPRINTATHON_PRESENT_CALL
#endif
using Gen=void(SPRINTATHON_PRESENT_CALL *)(GLsizei,GLuint*);
using Stamp=void(SPRINTATHON_PRESENT_CALL *)(GLuint,GLenum);
using Available=void(SPRINTATHON_PRESENT_CALL *)(GLuint,GLenum,GLuint*);
using Result=void(SPRINTATHON_PRESENT_CALL *)(GLuint,GLenum,uint64_t*);
#undef SPRINTATHON_PRESENT_CALL
using Clock=std::chrono::steady_clock;
inline bool benchmarkRequested(){static const bool on=[](){const char* v=std::getenv("SPRINTATHON_HUD_BENCHMARK");return v && v[0]=='1';}();return on;}
inline int benchmarkPhase(double seconds){return seconds<15?0:seconds<35?1:seconds<55?2:seconds<75?3:4;}
inline const char* phaseName(int phase){static const char* names[]={"warmup","HUD-on A","HUD-off B","HUD-on A2","complete"};return names[phase];}
struct Benchmark { int level=-1,phase=0; bool started=false; Clock::time_point start; };
inline Benchmark& benchmark(){static Benchmark b;return b;}
inline void updateBenchmark(int level){
    if(!benchmarkRequested())return;
    auto& b=benchmark();const auto now=Clock::now();
    const bool reset=!b.started || b.level!=level;
    if(reset){b.started=true;b.level=level;b.start=now;}
    const double seconds=std::chrono::duration<double>(now-b.start).count();
    const int phase=benchmarkPhase(seconds);
    if(reset || phase!=b.phase)std::fprintf(stderr,"HUD benchmark: %s; %.1f seconds; viewport unchanged\n",phaseName(phase),seconds);
    b.phase=phase;
}
inline bool suppressHUD(){return benchmarkRequested() && benchmark().started && benchmark().phase==2;}
struct Sample { GLuint q[6]={}; bool pending=false,valid=true; unsigned next=0; int phase=0; Clock::time_point cpu[6]; };
struct Totals { double gpu[5]={},cpu[5]={}; unsigned count=0; };
struct State {
    SDL_GLContext context=nullptr;
    bool initialized=false,supported=false;
    Gen gen=nullptr; Stamp stamp=nullptr; Available available=nullptr; Result result=nullptr;
    Sample samples[32]; int current=-1;
    Totals totals[5]; unsigned skipped=0;
    Clock::time_point published=Clock::now();
};
inline State& state(){static State s;return s;}
inline bool requested(){static const bool on=[](){const char* v=std::getenv("SPRINTATHON_PRESENT_TRACE");return v && v[0]=='1';}();return on;}
inline void mark(unsigned point){
    auto& s=state();
    if(s.current<0 || point>5)return;
    if(s.context!=SDL_GL_GetCurrentContext()){s.current=-1;return;}
    auto& sample=s.samples[s.current];
    if(sample.next!=point){s.current=-1;++s.skipped;return;}
    s.stamp(sample.q[point],0x8E28); // GL_TIMESTAMP
    sample.cpu[point]=Clock::now();++sample.next;
    if(point==5){sample.pending=true;s.current=-1;}
}
inline void begin(){
    if(!requested() && !benchmarkRequested())return;
    auto& s=state();const auto context=SDL_GL_GetCurrentContext();
    if(!context)return;
    if(s.context!=context){s=State{};s.context=context;}
    if(s.current>=0){
        // An interrupted view must not recycle queries that may still be in use.
        auto& old=s.samples[s.current];
        old.valid=false;
        while(old.next<6)mark(old.next);
        ++s.skipped;
    }
    if(!s.initialized){
        s.initialized=true;
        s.gen=reinterpret_cast<Gen>(SDL_GL_GetProcAddress("glGenQueries"));
        s.stamp=reinterpret_cast<Stamp>(SDL_GL_GetProcAddress("glQueryCounter"));
        s.available=reinterpret_cast<Available>(SDL_GL_GetProcAddress("glGetQueryObjectuiv"));
        s.result=reinterpret_cast<Result>(SDL_GL_GetProcAddress("glGetQueryObjectui64v"));
        int major=0,minor=0;const char* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if(version)std::sscanf(version,"%d.%d",&major,&minor);
        s.supported=s.gen && s.stamp && s.available && s.result &&
            (major>3 || (major==3 && minor>=3) || SDL_GL_ExtensionSupported("GL_ARB_timer_query"));
        std::fprintf(stderr,"Presentation trace: %s; GPU elapsed includes submission gaps; CPU overlaps GPU\n",s.supported?"asynchronous timestamps enabled":"unsupported");
        if(s.supported)for(auto& sample:s.samples)s.gen(6,sample.q);
    }
    if(!s.supported)return;
    for(auto& sample:s.samples){
        if(!sample.pending)continue;
        GLuint ready=0;s.available(sample.q[5],0x8867,&ready);
        if(!ready)continue;
        uint64_t ns[6]={};for(int i=0;i<6;++i)s.result(sample.q[i],0x8866,&ns[i]);
        bool valid=sample.valid;for(int i=0;i<5;++i)if(ns[i+1]<ns[i])valid=false;
        if(valid){auto& totals=s.totals[sample.phase];for(int i=0;i<5;++i){
            totals.gpu[i]+=double(ns[i+1]-ns[i])/1e6;
            totals.cpu[i]+=std::chrono::duration<double,std::milli>(sample.cpu[i+1]-sample.cpu[i]).count();
        }++totals.count;}
        sample.pending=false;
    }
    const auto now=Clock::now();
    if(std::chrono::duration<double>(now-s.published).count()>=1){
        for(int phase=0;phase<5;++phase){auto& t=s.totals[phase];if(!t.count)continue;
            double total=0;for(double v:t.gpu)total+=v;
            std::fprintf(stderr,"Presentation detail: phase=%s samples=%u GPU elapsed ms world=%.3f final-scene=%.3f overlays=%.3f HUD=%.3f tail=%.3f total=%.3f; CPU wall ms world=%.3f final-scene=%.3f overlays=%.3f HUD=%.3f tail=%.3f; skipped=%u\n",
                benchmarkRequested()?phaseName(phase):"normal",t.count,
                t.gpu[0]/t.count,t.gpu[1]/t.count,t.gpu[2]/t.count,t.gpu[3]/t.count,t.gpu[4]/t.count,total/t.count,
                t.cpu[0]/t.count,t.cpu[1]/t.count,t.cpu[2]/t.count,t.cpu[3]/t.count,t.cpu[4]/t.count,s.skipped);
            t=Totals{};
        }
        s.skipped=0;s.published=now;
    }
    for(int i=0;i<32;++i)if(!s.samples[i].pending){s.current=i;s.samples[i].next=0;s.samples[i].valid=true;s.samples[i].phase=benchmarkRequested()?benchmark().phase:0;mark(0);return;}
    ++s.skipped;
}
struct WorldScope { WorldScope(){begin();} ~WorldScope(){mark(1);} };
}
#endif
#endif
