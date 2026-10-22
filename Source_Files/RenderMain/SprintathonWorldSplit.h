#ifndef SPRINTATHON_WORLD_SPLIT_H
#define SPRINTATHON_WORLD_SPLIT_H
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Timestamp queries can coexist with the outer GL_TIME_ELAPSED query.
// Never wait for a result or recycle a pending sample.
namespace SprintathonWorldSplit {
#if defined(_WIN32) || defined(__WIN32__)
#define SPRINTATHON_SPLIT_CALL __stdcall
#elif defined(APIENTRY)
#define SPRINTATHON_SPLIT_CALL APIENTRY
#else
#define SPRINTATHON_SPLIT_CALL
#endif
using Gen=void(SPRINTATHON_SPLIT_CALL *)(GLsizei,GLuint*);
using Stamp=void(SPRINTATHON_SPLIT_CALL *)(GLuint,GLenum);
using Available=void(SPRINTATHON_SPLIT_CALL *)(GLuint,GLenum,GLuint*);
using Result=void(SPRINTATHON_SPLIT_CALL *)(GLuint,GLenum,uint64_t*);
#undef SPRINTATHON_SPLIT_CALL
static Gen gen=nullptr;
static Stamp stamp=nullptr;
static Available available=nullptr;
static Result result=nullptr;
using Clock=std::chrono::steady_clock;
struct Sample {
    GLuint query[4]={};
    uint64_t frame=0;
    bool pending=false;
    Clock::time_point cpu[4];
};
static Sample samples[16];
static SDL_GLContext context=nullptr;
static bool initialized=false,supported=false;
static int current=-1;
static unsigned skipped=0;
inline bool requested(){static const bool on=[](){
    const char* v=std::getenv("SPRINTATHON_WORLD_SPLIT");return v && v[0]=='1';
}();return on;}
inline void mark(unsigned point){
    if(current<0)return;
    auto& s=samples[current];
    stamp(s.query[point],0x8E28); // GL_TIMESTAMP
    s.cpu[point]=Clock::now();
    if(point==3){s.pending=true;current=-1;}
}
inline void begin(){
    current=-1;
    if(!requested() || !SprintathonStartupTiming::get().active())return;
    if(context!=SDL_GL_GetCurrentContext()){
        // Objects belong to the old context; do not delete names in the new one.
        for(auto& s:samples)s=Sample{};
        initialized=supported=false;context=SDL_GL_GetCurrentContext();skipped=0;
    }
    if(!initialized){
        initialized=true;
        gen=reinterpret_cast<Gen>(SDL_GL_GetProcAddress("glGenQueries"));
        stamp=reinterpret_cast<Stamp>(SDL_GL_GetProcAddress("glQueryCounter"));
        available=reinterpret_cast<Available>(SDL_GL_GetProcAddress("glGetQueryObjectuiv"));
        result=reinterpret_cast<Result>(SDL_GL_GetProcAddress("glGetQueryObjectui64v"));
        int major=0,minor=0;
        const char* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if(version)std::sscanf(version,"%d.%d",&major,&minor);
        supported=gen && stamp && available && result &&
            (SDL_GL_ExtensionSupported("GL_ARB_timer_query") || major>3 || (major==3 && minor>=3));
        std::fprintf(stderr,"World split: asynchronous timestamps %s; GPU elapsed includes submission gaps; CPU values overlap GPU\n",supported?"enabled":"unsupported");
        if(supported)for(auto& s:samples)gen(4,s.query);
    }
    if(!supported)return;
    for(auto& s:samples)if(s.pending){
        GLuint ready=0;available(s.query[3],0x8867,&ready);
        if(!ready)continue;
        uint64_t t[4]={};for(unsigned i=0;i<4;++i)result(s.query[i],0x8866,&t[i]);
        double cpu[3];for(unsigned i=0;i<3;++i)
            cpu[i]=std::chrono::duration<double,std::milli>(s.cpu[i+1]-s.cpu[i]).count();
        if(t[0]<=t[1] && t[1]<=t[2] && t[2]<=t[3])
            std::fprintf(stderr,"World split frame %llu: GPU elapsed ms pre-batch %.3f batched-opaque %.3f remaining %.3f total %.3f; CPU ms pre-batch %.3f batched-opaque %.3f remaining %.3f; skipped %u\n",
                (unsigned long long)s.frame,(t[1]-t[0])/1e6,(t[2]-t[1])/1e6,(t[3]-t[2])/1e6,(t[3]-t[0])/1e6,cpu[0],cpu[1],cpu[2],skipped);
        s.pending=false;
    }
    for(unsigned i=0;i<16;++i)if(!samples[i].pending){current=int(i);break;}
    if(current<0){++skipped;return;}
    samples[current].frame=SprintathonStartupTiming::get().frame_id;
    mark(0);
}
}
#endif
