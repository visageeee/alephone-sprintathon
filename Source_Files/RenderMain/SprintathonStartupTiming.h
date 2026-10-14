#ifndef SPRINTATHON_STARTUP_TIMING_H
#define SPRINTATHON_STARTUP_TIMING_H
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
struct SprintathonStartupTiming {
    using Clock=std::chrono::steady_clock;
    Clock::time_point start=Clock::now(),report=start,last=start;
    bool loading=true,started=false;
    unsigned frames=0,uploads=0,compiles=0,evictions=0;
    uint64_t frame_id=0;
    double light_prepare_ms=0,fog_prepare_ms=0;
    static bool extended() {
        static const bool enabled=[] { const char* p=std::getenv("SPRINTATHON_RENDER_TRACE"); return p && p[0]=='1'; }();
        return enabled;
    }
    double cpu=0,cpu_max=0,interval_max=0,upload_ms=0,compile_ms=0;
    static SprintathonStartupTiming& get() { static SprintathonStartupTiming data;return data; }
    bool active() const { return started && !loading &&
        (extended() || std::chrono::duration<double>(Clock::now()-start).count()<60); }
    void begin() { *this=SprintathonStartupTiming{}; }
    void ready() {
        std::fprintf(stderr,"Startup loading: %u shaders %.1f ms; %u texture preparations/uploads %.1f ms\n",
            compiles,compile_ms,uploads,upload_ms);
        loading=false;started=true;start=report=last=Clock::now();
        uploads=compiles=evictions=0;upload_ms=compile_ms=0;
    }
};
struct SprintathonStartupWork {
    bool shader;
    SprintathonStartupTiming::Clock::time_point start=SprintathonStartupTiming::Clock::now();
    explicit SprintathonStartupWork(bool value):shader(value) {}
    ~SprintathonStartupWork() {
        auto& s=SprintathonStartupTiming::get();
        if (!s.loading && !s.active()) return;
        const double ms=std::chrono::duration<double,std::milli>(SprintathonStartupTiming::Clock::now()-start).count();
        if(shader){++s.compiles;s.compile_ms+=ms;}else{++s.uploads;s.upload_ms+=ms;}
    }
};
// CPU wall times include driver waits; they are not GPU execution times.
struct SprintathonStartupPhase {
    double& total;
    bool enabled;
    SprintathonStartupTiming::Clock::time_point start;
    explicit SprintathonStartupPhase(double& value):total(value),enabled(SprintathonStartupTiming::get().active()),start(SprintathonStartupTiming::Clock::now()) {}
    void finish() { if(enabled) { total+=std::chrono::duration<double,std::milli>(SprintathonStartupTiming::Clock::now()-start).count(); enabled=false; } }
    ~SprintathonStartupPhase() { finish(); }
};
struct SprintathonStartupFrame {
    double stages[14]={};
    int stage=0;
    bool enabled=false;
    unsigned uploads_before=0,compiles_before=0;
    double upload_before=0;
    SprintathonStartupTiming::Clock::time_point mark;
    SprintathonStartupFrame() {
        auto& s=SprintathonStartupTiming::get(); enabled=s.active(); mark=start;
        ++s.frame_id; s.light_prepare_ms=s.fog_prepare_ms=0;
        uploads_before=s.uploads; compiles_before=s.compiles; upload_before=s.upload_ms;
    }
    void pass(int next) {
        if(!enabled)return;
        auto now=SprintathonStartupTiming::Clock::now();
        stages[stage]+=std::chrono::duration<double,std::milli>(now-mark).count();
        stage=next;mark=now;
    }
    SprintathonStartupTiming::Clock::time_point start=SprintathonStartupTiming::Clock::now();
    ~SprintathonStartupFrame() {
        auto& s=SprintathonStartupTiming::get();if(!s.active())return;
        auto now=SprintathonStartupTiming::Clock::now();
        const double ms=std::chrono::duration<double,std::milli>(now-start).count();
        pass(stage);
        const double gap=std::chrono::duration<double,std::milli>(start-s.last).count();
        if(ms>=20 || gap>=35)
            std::fprintf(stderr,"Render slow CPU frame %llu: render %.2f gap %.2f ms; prep %.2f (lights %.2f); world %.2f shaft-source %.2f overlays %.2f depth-copy %.2f AO %.2f flares %.2f fog %.2f (fog-data %.2f) shafts %.2f weapon %.2f bloom-geometry %.2f bloom-filter %.2f underwater %.2f tail %.2f; uploads %u %.2f ms shaders %u\n",
                static_cast<unsigned long long>(s.frame_id),ms,gap,stages[0],s.light_prepare_ms,
                stages[1],stages[2],stages[3],stages[4],stages[5],stages[6],stages[7],s.fog_prepare_ms,
                stages[8],stages[9],stages[10],stages[11],stages[12],stages[13],s.uploads-uploads_before,s.upload_ms-upload_before,s.compiles-compiles_before);
        s.cpu+=ms;s.cpu_max=std::max(s.cpu_max,ms);
        s.interval_max=std::max(s.interval_max,std::chrono::duration<double,std::milli>(start-s.last).count());
        s.last=start;++s.frames;
        if(std::chrono::duration<double>(now-s.report).count()<1)return;
        std::fprintf(stderr,"Startup CPU: render avg/max %.2f/%.2f ms; frame interval max %.2f ms; uploads %u %.1f ms; shaders %u %.1f ms; evictions %u\n",
            s.cpu/s.frames,s.cpu_max,s.interval_max,s.uploads,s.upload_ms,s.compiles,s.compile_ms,s.evictions);
        s.report=now;s.frames=s.uploads=s.compiles=s.evictions=0;
        s.cpu=s.cpu_max=s.interval_max=s.upload_ms=s.compile_ms=0;
    }
};
#endif
