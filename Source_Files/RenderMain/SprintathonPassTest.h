#ifndef SPRINTATHON_PASS_TEST_H
#define SPRINTATHON_PASS_TEST_H
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Diagnostic-only process-wide switches; never edit or save preferences.
struct SprintathonPassTest {
    enum Mode { Full, World, Shafts, Bloom, Overlays, Combined };
    Mode mode=Full;
    bool minimal=false, requested=false;
    const char* name="full";
    SprintathonPassTest() {
        const char* p=std::getenv("SPRINTATHON_PASS_TEST");
        if(p && *p) {
            requested=true;
            if(!std::strcmp(p,"world")){mode=World;name="world";}
            else if(!std::strcmp(p,"shafts")){mode=Shafts;name="shafts";}
            else if(!std::strcmp(p,"bloom")){mode=Bloom;name="bloom";}
            else if(!std::strcmp(p,"overlays")){mode=Overlays;name="overlays";}
            else if(!std::strcmp(p,"combined")){mode=Combined;name="combined";}
            else if(std::strcmp(p,"full"))
                std::fprintf(stderr,"Unknown SPRINTATHON_PASS_TEST '%s'; using full\n",p);
        }
        p=std::getenv("SPRINTATHON_MINIMAL_WORLD_SHADER");
        minimal=p && p[0]=='1';
        if(requested || minimal)
            std::fprintf(stderr,"Pass test: %s; minimal world shader %d; preferences still gate enabled passes\n",name,int(minimal));
    }
    static const SprintathonPassTest& get(){static const SprintathonPassTest t;return t;}
    bool shafts() const{return mode==Full || mode==Shafts || mode==Combined;}
    bool bloom() const{return mode==Full || mode==Bloom || mode==Combined;}
    bool overlays() const{return mode==Full || mode==Overlays || mode==Combined;}
    bool other() const{return mode==Full;}
    void frame() const {
        if(!requested && !minimal)return;
        using Clock=std::chrono::steady_clock;
        static bool started=false;
        static Clock::time_point last;
        static std::vector<double> intervals;
        const auto now=Clock::now();
        if(started) {
            const double ms=std::chrono::duration<double,std::milli>(now-last).count();
            // Menu/loading pauses are not gameplay frames. Restart the window.
            if(ms>1000)intervals.clear();
            else intervals.push_back(ms);
        }
        last=now;started=true;
        if(intervals.size()<120)return;
        double sum=0;for(double ms:intervals)sum+=ms;
        std::sort(intervals.begin(),intervals.end());
        std::fprintf(stderr,"Pass test cadence: %s minimal %d; 120 intervals avg %.2f median %.2f p95 %.2f max %.2f ms; %.2f FPS (render-entry intervals, includes simulation/present/waits)\n",
            name,int(minimal),sum/120,(intervals[59]+intervals[60])*0.5,
            intervals[113],intervals.back(),120000.0/std::max(sum,0.001));
        intervals.clear();
    }
};
#endif
