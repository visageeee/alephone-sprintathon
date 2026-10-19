#ifndef SPRINTATHON_FRAME_TRACE_H
#define SPRINTATHON_FRAME_TRACE_H
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// Main-thread wall-clock accounting. Nested scopes are exclusive; no GPU waits
// are introduced. A frame is the interval between completed presentation calls.
namespace SprintathonFrameTrace {
enum Stage { Other, Events, Timers, Update, Interpolation, Render, Swap, Limiter, Count };
struct Trace {
    typedef std::chrono::steady_clock Clock;
    typedef Clock::time_point Time;
    bool enabled, started;
    int mode, level, tick, stage, ticks, loops;
    Time stamp, origin, previous, report;
    double bins[Count], sums[Count], worst[Count], total, maximum;
    unsigned frames, tick_frames;
    int sum_ticks, sum_loops;
    Trace() : enabled(false), started(false), mode(-1), level(-1), tick(0),
        stage(Other), ticks(0), loops(0), total(0), maximum(0), frames(0),
        tick_frames(0), sum_ticks(0), sum_loops(0) {
        const char* value = std::getenv("SPRINTATHON_FRAME_TRACE");
        enabled = value && value[0] == '1';
        stamp = origin = previous = report = Clock::now();
        clear(bins); clear(sums); clear(worst);
    }
    static void clear(double* p) { std::fill(p, p + Count, 0.0); }
    static double ms(Time a, Time b) {
        return std::chrono::duration<double, std::milli>(b-a).count();
    }
    void charge(Time now) { bins[stage] += ms(stamp, now); stamp = now; }
    void output(Time now) {
        if (!frames) return;
        std::fprintf(stderr, "Whole frame: t=%.3fs mode=%d level=%d tick=%d frames=%u avg=%.3f max=%.3f ms; ticks=%d frames-with-ticks=%u loops=%d\n",
            ms(origin,now)/1000.0, mode, level, tick, frames, total/frames, maximum,
            sum_ticks, tick_frames, sum_loops);
        const char* names[Count] = {"other", "events", "timers", "update", "interpolation", "render", "swap", "limiter"};
        std::fprintf(stderr, "Whole frame stages (exclusive wall ms):");
        for (int i=0;i<Count;++i) std::fprintf(stderr," %s=%.3f",names[i],sums[i]/frames);
        std::fprintf(stderr, "; worst-frame:");
        for (int i=0;i<Count;++i) std::fprintf(stderr," %s=%.3f",names[i],worst[i]);
        std::fprintf(stderr,"\n");
        clear(sums); clear(worst); frames=tick_frames=0;
        sum_ticks=sum_loops=0; total=maximum=0; report=now;
    }
    void context(int new_mode, int new_level, int new_tick) {
        if (!enabled) return;
        tick=new_tick;
        if (mode==new_mode && level==new_level) return;
        Time now=Clock::now(); charge(now); output(now);
        mode=new_mode; level=new_level; started=false;
        clear(bins); ticks=loops=0; report=now;
        std::fprintf(stderr,"Whole frame mode: t=%.3fs mode=%d level=%d tick=%d (0=play, 1=paused/controller-off, 2=screenshot, 3=visual, 4=outside-game); boundary interval omitted\n",
            ms(origin,now)/1000.0,mode,level,tick);
    }
    void present() {
        if (!enabled || mode<0 || mode==4) return;
        Time now=Clock::now(); charge(now);
        if (started) {
            double duration=ms(previous,now);
            ++frames; total+=duration; sum_ticks+=ticks; sum_loops+=loops;
            if (ticks) ++tick_frames;
            for(int i=0;i<Count;++i) sums[i]+=bins[i];
            if (duration>maximum) { maximum=duration; std::copy(bins,bins+Count,worst); }
        }
        started=true; previous=now; clear(bins); ticks=loops=0;
        if (ms(report,now)>=1000.0) output(now);
        // Logging overhead is included in the following interval.
    }
};
inline Trace& get() { static Trace instance; return instance; }
struct Scope {
    Trace& t; int saved;
    explicit Scope(Stage s) : t(get()), saved(t.stage) {
        if (t.enabled) { t.charge(Trace::Clock::now()); t.stage=s; }
    }
    ~Scope() {
        if (t.enabled) { t.charge(Trace::Clock::now()); t.stage=saved; }
    }
};
}
#endif
