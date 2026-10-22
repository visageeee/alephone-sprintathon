#include "SprintathonLuaSun.h"
#include "shell_options.h"
/*
 *  RenderRasterize_Shader.cpp
 *  Created by Clemens Unterkofler on 1/20/09.
 *  for Aleph One
 *
 *  http://www.gnu.org/licenses/gpl.html
 */

#include "OGL_Headers.h"
#include "SprintathonPortalTrace.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

#include "RenderRasterize_Shader.h"

#include "lightsource.h"
#include "scenery.h"
#include "interface.h"
#include "projectiles.h"
#include "monsters.h"
#include "effects.h"
#include "media.h"
#include "player.h"
#include "weapons.h"
#include "AnimatedTextures.h"
#include "OGL_Faders.h"
#include "OGL_Blitter.h"
#include "OGL_Textures.h"
#include "OGL_Shader.h"
#include "SprintathonStartupTiming.h"
#include "SprintathonPassTest.h"
#include "ChaseCam.h"
#include "preferences.h"
#include "SprintathonRenderResolution.h"
#include "screen.h"
#include "map.h"
#include "SoundManager.h"
#include "SoundPlayer.h"
#include "FileHandler.h"

#ifdef HAVE_OPENGL
#include "SprintathonWaterNormals.h"
#include "SprintathonSpriteReflection.h"
#include "SprintathonSkyBake.h"
#include "SprintathonOpaqueGeometry.h"
#include "SprintathonOpaqueBatch.h"
#include "SprintathonOpaqueWorld.h"
#include "SprintathonWorldSplit.h"
#include "SprintathonObjectOps.h"
#include <cstring>

extern bool ShowPosition;

// Results are read only after availability is reported. A full queue skips
// sampling rather than waiting for the GPU. No glFinish or result busy-wait.
namespace {
// GLEW may undefine APIENTRY after declaring its entry points on Windows.
// Keep the OpenGL ABI explicit, especially for 32-bit Windows builds.
#if defined(_WIN32) || defined(__WIN32__)
#define SPRINTATHON_GL_CALL __stdcall
#elif defined(APIENTRY)
#define SPRINTATHON_GL_CALL APIENTRY
#else
#define SPRINTATHON_GL_CALL
#endif
// Sample category transitions without changing portal/blending draw order.
// Detailed elapsed queries use dedicated sampled frames, without the outer query.
static bool sprintathon_detail_frame() {
    const auto frame=SprintathonStartupTiming::get().frame_id;
    return SprintathonStartupTiming::get().active() && (frame==1 || frame%60==0);
}

// Fine GPU queries perturb submission-heavy scenes. Keep them explicitly opt-in.
static bool sprintathon_detail_gpu_requested() {
    static const bool enabled=[](){const char* v=std::getenv("SPRINTATHON_DETAIL_GPU");return v && v[0]=='1';}();
    return enabled;
}
// Sample exclusive CPU time, including driver calls, without extra GPU queries.
struct SprintathonSetupTrace {
    enum Part { Other, WallTexture, SpriteTexture, Emitters, PixelLight, Clip, Object, Draw, MaterialResolve, SectorEdges, BakedBinding, EmitterRecord, Count };
    using Clock=std::chrono::steady_clock;
    bool active=false;
    int part=Other;
    double ms[Count]={};
    unsigned calls[Count]={};
    unsigned mediaWindows=0, outsideBase=0, mediaExcluded=0;
    uint64_t frame=0;
    Clock::time_point mark;
    static SprintathonSetupTrace& get(){static SprintathonSetupTrace t;return t;}
    void change(int next){
        if(!active)return;
        const auto now=Clock::now();
        ms[part]+=std::chrono::duration<double,std::milli>(now-mark).count();
        mark=now;part=next;
    }
    void begin(){
        static const bool enabled=[](){const char* v=std::getenv("SPRINTATHON_SETUP_TRACE");return v && v[0]=='1';}();
        active=enabled && sprintathon_detail_frame();
        if(!active)return;
        frame=SprintathonStartupTiming::get().frame_id;
        for(int i=0;i<Count;++i){ms[i]=0;calls[i]=0;}
        mediaWindows=outsideBase=mediaExcluded=0;
        part=Other;mark=Clock::now();
    }
    void end(){
        if(!active)return;
        change(Other);active=false;
        double total=0;for(double t:ms)total+=t;
        std::fprintf(stderr,"World setup CPU frame %llu: total %.3f ms; exclusive ms/calls: other %.3f wall-texture %.3f/%u sprite-texture %.3f/%u emitters %.3f/%u pixel-light %.3f/%u clip %.3f/%u object-other %.3f/%u draw-submit %.3f/%u; fine-GPU %d\n",
            (unsigned long long)frame,total,ms[Other],ms[WallTexture],calls[WallTexture],ms[SpriteTexture],calls[SpriteTexture],
            ms[Emitters],calls[Emitters],ms[PixelLight],calls[PixelLight],ms[Clip],calls[Clip],ms[Object],calls[Object],ms[Draw],calls[Draw],
            int(sprintathon_detail_gpu_requested()));
        std::fprintf(stderr,"World preparation CPU frame %llu: exclusive ms/calls: material-resolve %.3f/%u sector-edges %.3f/%u baked-binding %.3f/%u emitter-record %.3f/%u; included in world total, excluded from other\n",
            (unsigned long long)frame,ms[MaterialResolve],calls[MaterialResolve],
            ms[SectorEdges],calls[SectorEdges],ms[BakedBinding],calls[BakedBinding],ms[EmitterRecord],calls[EmitterRecord]);
        std::fprintf(stderr,"Media setup frame %llu: window submissions %u; base-sprite bounds wholly outside retained half %u; bounds excluded (models/flyers) %u; shadows/trails not classified, no draws skipped\n",
            (unsigned long long)frame,mediaWindows,outsideBase,mediaExcluded);
    }
};
struct SprintathonSetupScope {
    bool active;int previous;
    explicit SprintathonSetupScope(int part):active(SprintathonSetupTrace::get().active),previous(SprintathonSetupTrace::get().part){
        if(active){auto& t=SprintathonSetupTrace::get();t.change(part);++t.calls[part];}
    }
    ~SprintathonSetupScope(){if(active)SprintathonSetupTrace::get().change(previous);}
};
struct SprintathonSetupCapture {
    bool enabled;
    explicit SprintathonSetupCapture(bool enabled):enabled(enabled){if(enabled)SprintathonSetupTrace::get().begin();}
    ~SprintathonSetupCapture(){if(enabled)SprintathonSetupTrace::get().end();}
};

#if defined(_WIN32) || defined(__WIN32__)
#define WORLD_GL_CALL __stdcall
#elif defined(APIENTRY)
#define WORLD_GL_CALL APIENTRY
#else
#define WORLD_GL_CALL
#endif
class SprintathonWorldDetail {
    using Gen = void (WORLD_GL_CALL *)(GLsizei, GLuint*);
    using Delete = void (WORLD_GL_CALL *)(GLsizei, const GLuint*);
    using Begin = void (WORLD_GL_CALL *)(GLenum, GLuint);
    using End = void (WORLD_GL_CALL *)(GLenum);
    using Available = void (WORLD_GL_CALL *)(GLuint, GLenum, GLuint*);
    using Result = void (WORLD_GL_CALL *)(GLuint, GLenum, uint64_t*);
#undef WORLD_GL_CALL
    static constexpr int capacity = 4096;
    struct Sample {
        GLuint queries[capacity] = {};
        int category[capacity] = {};
        unsigned draws[13] = {};
        int used = 0;
        bool pending = false, truncated = false;
        uint64_t frame = 0;
    } samples[3];
    Gen gen = nullptr;
    Delete remove = nullptr;
    Begin beginQuery = nullptr;
    End endQuery = nullptr;
    Available available = nullptr;
    Result result = nullptr;
    SDL_GLContext context = nullptr;
    bool initialized = false, supported = false;
    int current = -1;
    using CpuClock=std::chrono::steady_clock;
    CpuClock::time_point cpuStart,cpuMark;
    bool cpuActive=false;
    int cpuCategory=4;
    uint64_t cpuFrame=0;
    double cpuMs[13]={};
    unsigned cpuDraws[13]={};
    void cpuPass(int next) {
        if(!cpuActive)return;
        const auto now=CpuClock::now();
        cpuMs[cpuCategory]+=std::chrono::duration<double,std::milli>(now-cpuMark).count();
        cpuMark=now;cpuCategory=next;
    }
public:
    int category = 4; // opaque, other surfaces, sky, objects, unclassified
    void reset() {
        if (remove && context == SDL_GL_GetCurrentContext())
            for (auto& s : samples) if (s.queries[0]) remove(capacity, s.queries);
        for (auto& s : samples) s = Sample{};
        initialized = supported = false;
        current = -1;cpuActive=false;
    }
    void begin() {
        if (!sprintathon_detail_gpu_requested()) { cpuActive=false; current=-1; return; }
        if (!SprintathonStartupTiming::get().active()) {
            if (initialized) reset();
            return;
        }
        if (context != SDL_GL_GetCurrentContext()) reset();
        const auto frame = SprintathonStartupTiming::get().frame_id;
        cpuActive=frame==1 || frame%60==0;
        if(cpuActive){
            cpuFrame=frame;cpuCategory=4;category=4;cpuStart=cpuMark=CpuClock::now();
            for(int i=0;i<13;++i){cpuMs[i]=0;cpuDraws[i]=0;}
        }
        if (!initialized) {
            context = SDL_GL_GetCurrentContext();
            initialized = true;
            gen = reinterpret_cast<Gen>(SDL_GL_GetProcAddress("glGenQueries"));
            remove = reinterpret_cast<Delete>(SDL_GL_GetProcAddress("glDeleteQueries"));
            beginQuery = reinterpret_cast<Begin>(SDL_GL_GetProcAddress("glBeginQuery"));
            endQuery = reinterpret_cast<End>(SDL_GL_GetProcAddress("glEndQuery"));
            available = reinterpret_cast<Available>(SDL_GL_GetProcAddress("glGetQueryObjectuiv"));
            result = reinterpret_cast<Result>(SDL_GL_GetProcAddress("glGetQueryObjectui64v"));
            int major=0,minor=0;
            const char* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
            if(version)std::sscanf(version,"%d.%d",&major,&minor);
            supported = gen && remove && beginQuery && endQuery && available && result &&
                (SDL_GL_ExtensionSupported("GL_ARB_timer_query") || major>3 || (major==3 && minor>=3));
            fprintf(stderr,"World detail v4: CPU timings enabled; isolated GPU elapsed queries %s; GL %s\n",
                supported ? "enabled" : "unavailable",version?version:"unknown");
        }
        if (!supported) return;
        for (auto& s : samples) {
            if (!s.pending) continue;
            GLuint ready = 0;
            available(s.queries[s.used-1], 0x8867, &ready);
            if (!ready) {
                if(frame>s.frame+240) {
                    fprintf(stderr,"World detail: GPU query timeout for frame %llu; disabling detailed GPU queries (CPU and whole-pass timers remain available)\n",static_cast<unsigned long long>(s.frame));
                    supported=false;return;
                }
                continue;
            }
            double ms[13] = {};
            for (int i=0; i<s.used; ++i) {
                uint64_t ns=0;result(s.queries[i],0x8866,&ns);
                ms[s.category[i]]+=double(ns)/1000000.0;
            }
            fprintf(stderr, "World detail frame %llu: GPU opaque %.2f other-surfaces %.2f sky %.2f sprites-models %.2f unclassified %.2f ms; draws %u/%u/%u/%u/%u; transitions %d truncated %d\n",
                static_cast<unsigned long long>(s.frame), ms[0],ms[1],ms[2],(ms[3]+ms[5]+ms[6]+ms[7]+ms[8]+ms[9]+ms[10]+ms[11]+ms[12]),ms[4],
                s.draws[0],s.draws[1],s.draws[2],(s.draws[3]+s.draws[5]+s.draws[6]+s.draws[7]+s.draws[8]+s.draws[9]+s.draws[10]+s.draws[11]+s.draws[12]),s.draws[4],s.used-1,int(s.truncated));
            fprintf(stderr,"Object GPU frame %llu: scenery %.3f characters %.3f projectiles %.3f other %.3f models %.3f glow %.3f blur %.3f reflections %.3f shadows %.3f setup-gap %.3f ms; draws %u/%u/%u/%u/%u/%u/%u/%u/%u\n",
                static_cast<unsigned long long>(s.frame),ms[5],ms[6],ms[7],ms[3],ms[11],ms[8],ms[9],ms[10],ms[12],ms[4],
                s.draws[5],s.draws[6],s.draws[7],s.draws[3],s.draws[11],s.draws[8],s.draws[9],s.draws[10],s.draws[12]);
            s.pending = false;
        }
        if (!cpuActive) return;
        unsigned pending=0;for(const auto& s:samples)if(s.pending)++pending;
        if(pending==3)fprintf(stderr,"World detail frame %llu: GPU samples still pending; CPU detail remains available\n",
            static_cast<unsigned long long>(frame));
        for (int i=0;i<3;++i) if (!samples[i].pending) {
            auto& s=samples[i];
            if (!s.queries[0]) gen(capacity,s.queries);
            s.used=1;s.frame=frame;s.truncated=false;
            for (auto& n:s.draws)n=0;
            s.category[0]=4;category=4;current=i;
            beginQuery(0x88BF,s.queries[0]);
            break;
        }
    }
    void select(int next) {
        category=next;
        if(cpuActive)cpuPass(next);
        if(current<0)return;
        auto& s=samples[current];
        if(next==s.category[s.used-1])return;
        if(s.used>=capacity-1){s.truncated=true;return;}
        endQuery(0x88BF);s.category[s.used]=next;
        beginQuery(0x88BF,s.queries[s.used++]);
    }
    void draw() {
        select(category);
        if(cpuActive)++cpuDraws[category];
        if(current>=0)++samples[current].draws[category];
    }
    void end() {
        if(cpuActive){
            cpuPass(cpuCategory);
            const double total=std::chrono::duration<double,std::milli>(CpuClock::now()-cpuStart).count();
            fprintf(stderr,"World CPU detail frame %llu: total %.2f; opaque %.2f other-surfaces %.2f sky %.2f sprites-models %.2f unclassified %.2f ms; draws %u/%u/%u/%u/%u (includes driver waits)\n",
                static_cast<unsigned long long>(cpuFrame),total,cpuMs[0],cpuMs[1],cpuMs[2],(cpuMs[3]+cpuMs[5]+cpuMs[6]+cpuMs[7]+cpuMs[8]+cpuMs[9]+cpuMs[10]+cpuMs[11]+cpuMs[12]),cpuMs[4],
                cpuDraws[0],cpuDraws[1],cpuDraws[2],(cpuDraws[3]+cpuDraws[5]+cpuDraws[6]+cpuDraws[7]+cpuDraws[8]+cpuDraws[9]+cpuDraws[10]+cpuDraws[11]+cpuDraws[12]),cpuDraws[4]);
            cpuActive=false;
        }
        if (current < 0) return;
        auto& s=samples[current];
        endQuery(0x88BF);
        s.pending=true;current=-1;
    }
};
static SprintathonWorldDetail sprintathon_world_detail;

// End attribution at function exit, rather than charging setup until the next draw.
struct SprintathonDetailScope {
    explicit SprintathonDetailScope(int category=4){sprintathon_world_detail.select(category);}
    ~SprintathonDetailScope(){sprintathon_world_detail.select(4);}
};
struct SprintathonObjectCounts {
    uint64_t frame=0;bool enabled=false;
    std::set<const void*> records[4];unsigned calls[4]={},depthless=0,baseDraws=0;
    void begin(){
        enabled=sprintathon_detail_frame();if(!enabled)return;
        frame=SprintathonStartupTiming::get().frame_id;depthless=baseDraws=0;
        for(int i=0;i<4;++i){records[i].clear();calls[i]=0;}
    }
    void visit(const void* object,int kind){if(enabled){records[kind].insert(object);++calls[kind];}}
    void finish(){if(!enabled)return;
        fprintf(stderr,"Object records frame %llu: scenery %zu/%u characters %zu/%u projectiles %zu/%u other %zu/%u unique-render-records/helper-visits; sprite-base-draws %u depth-test-disabled %u\n",
            static_cast<unsigned long long>(frame),records[0].size(),calls[0],records[1].size(),calls[1],records[2].size(),calls[2],records[3].size(),calls[3],baseDraws,depthless);
        enabled=false;
    }
};
static SprintathonObjectCounts sprintathon_object_counts;
struct SprintathonObjectCapture {
    bool enabled;
    explicit SprintathonObjectCapture(bool main):enabled(main){if(enabled)sprintathon_object_counts.begin();}
    ~SprintathonObjectCapture(){if(enabled)sprintathon_object_counts.finish();}
};

class SprintathonGpuProfiler {
    using Gen = void (SPRINTATHON_GL_CALL *)(GLsizei, GLuint*);
    using Delete = void (SPRINTATHON_GL_CALL *)(GLsizei, const GLuint*);
    using Begin = void (SPRINTATHON_GL_CALL *)(GLenum, GLuint);
    using End = void (SPRINTATHON_GL_CALL *)(GLenum);
    using Available = void (SPRINTATHON_GL_CALL *)(GLuint, GLenum, GLuint*);
    using Result = void (SPRINTATHON_GL_CALL *)(GLuint, GLenum, uint64_t*);
#undef SPRINTATHON_GL_CALL
    Gen gen = nullptr;
    Delete remove = nullptr;
    Begin begin = nullptr;
    End end = nullptr;
    Available available = nullptr;
    Result result = nullptr;
    static constexpr GLenum elapsed = 0x88BF;
    static constexpr GLenum query_available = 0x8867;
    static constexpr GLenum query_result = 0x8866;
    static constexpr int pass_count=13;
    struct Sample { GLuint queries[pass_count] = {}; bool pending = false; uint64_t frame_id=0; } samples[12];
    SDL_GLContext context = nullptr;
    bool initialized = false;
    int current = -1;
    bool active = false;
    double sums[pass_count] = {};
    unsigned count = 0;
    uint64_t last_publish = 0;
    template<class T> static T load(const char *core, const char *extension) {
        void *address = SDL_GL_GetProcAddress(core);
        if (!address) address = SDL_GL_GetProcAddress(extension);
        return reinterpret_cast<T>(address);
    }
public:
    void reset() {
        if (initialized && remove && context == SDL_GL_GetCurrentContext())
            for (auto& sample : samples)
                if (sample.queries[0]) remove(pass_count, sample.queries);
        for (auto& sample : samples) sample = Sample{};
        initialized = active = false;
        current = -1;
        count = 0;
        for (double& sum : sums) sum = 0;
        sprintathon_gpu_timings = SprintathonGpuTimings{};
    }
    void begin_frame() {
        if (!ShowPosition && !SprintathonStartupTiming::get().active()) {
            if (initialized) reset();
            return;
        }
        if (context != SDL_GL_GetCurrentContext()) reset();
        if (!initialized) {
            context = SDL_GL_GetCurrentContext();
            initialized = true;
            gen = load<Gen>("glGenQueries", "glGenQueriesARB");
            remove = load<Delete>("glDeleteQueries", "glDeleteQueriesARB");
            begin = load<Begin>("glBeginQuery", "glBeginQueryARB");
            end = load<End>("glEndQuery", "glEndQueryARB");
            available = load<Available>("glGetQueryObjectuiv", "glGetQueryObjectuivARB");
            result = load<Result>("glGetQueryObjectui64v", "glGetQueryObjectui64vEXT");
            sprintathon_gpu_timings.supported = gen && remove && begin && end && available && result &&
                (SDL_GL_ExtensionSupported("GL_ARB_timer_query") ||
                 SDL_GL_ExtensionSupported("GL_EXT_timer_query"));
            last_publish = machine_tick_count();
            if (sprintathon_gpu_timings.supported)
                for (auto& sample : samples) gen(pass_count, sample.queries);
        }
        if (!sprintathon_gpu_timings.supported) return;
        if(sprintathon_detail_gpu_requested() && sprintathon_detail_frame()) { current=-1; return; }
        for (auto& sample : samples) {
            if (!sample.pending) continue;
            GLuint ready = 0;
            available(sample.queries[pass_count-1], query_available, &ready);
            if (!ready) continue;
            double frame_ms[pass_count]={},total_ms=0;
            for (int i = 0; i < pass_count; ++i) {
                uint64_t nanoseconds = 0;
                result(sample.queries[i], query_result, &nanoseconds);
                frame_ms[i]=double(nanoseconds)/1000000.0;
                total_ms+=frame_ms[i]; sums[i]+=frame_ms[i];
            }
            if (SprintathonStartupTiming::get().active() && total_ms>=16.0)
                std::fprintf(stderr,"Render slow GPU frame %llu: total %.2f ms; world %.2f shaft-source %.2f overlays %.2f depth-copy %.2f AO %.2f flares %.2f fog %.2f shafts %.2f weapon %.2f bloom-geometry %.2f bloom-filter %.2f underwater %.2f tail %.2f\n",
                    static_cast<unsigned long long>(sample.frame_id),total_ms,frame_ms[0],frame_ms[1],frame_ms[2],frame_ms[3],frame_ms[4],frame_ms[5],frame_ms[6],frame_ms[7],frame_ms[8],frame_ms[9],frame_ms[10],frame_ms[11],frame_ms[12]);
            ++count;
            sample.pending = false;
        }
        const uint64_t now = machine_tick_count();
        if (count && now - last_publish >= MACHINE_TICKS_PER_SECOND / 4) {
            if (SprintathonStartupTiming::get().active())
                std::fprintf(stderr,"Startup GPU ms: world %.2f; shaft-source %.2f; overlays %.2f; depth-copy %.2f; AO %.2f; flares %.2f; fog %.2f; shafts %.2f; weapon %.2f; bloom-geometry %.2f; bloom-filter %.2f; underwater %.2f\n",
                    sums[0]/count,sums[1]/count,sums[2]/count,sums[3]/count,sums[4]/count,sums[5]/count,
                    sums[6]/count,sums[7]/count,sums[8]/count,sums[9]/count,sums[10]/count,sums[11]/count);
            sprintathon_gpu_timings.milliseconds[0]=sums[0]/count;
            sprintathon_gpu_timings.milliseconds[1]=(sums[1]+sums[2]+sums[3]+sums[4]+sums[5]+sums[6]+sums[7])/count;
            sprintathon_gpu_timings.milliseconds[2]=sums[8]/count;
            sprintathon_gpu_timings.milliseconds[3]=(sums[9]+sums[10])/count;
            sprintathon_gpu_timings.milliseconds[4]=(sums[11]+sums[12])/count;
            for (double& sum:sums) sum=0;
            count = 0;
            last_publish = now;
            sprintathon_gpu_timings.ready = true;
        }
        current = -1;
        for (int i = 0; i < 12; ++i)
            if (!samples[i].pending) { current = i; samples[i].frame_id=SprintathonStartupTiming::get().frame_id; break; }
    }
    void pass(int index) {
        if (current < 0 || index < 0 || index >= pass_count) return;
        if (active) end(elapsed);
        begin(elapsed, samples[current].queries[index]);
        active = true;
    }
    void end_frame() {
        if (current < 0) return;
        if (active) end(elapsed);
        active = false;
        samples[current].pending = true;
        current = -1;
    }
};
SprintathonGpuProfiler sprintathon_gpu_profiler;
struct SprintathonGpuProfileFrame {
    SprintathonGpuProfileFrame() { sprintathon_gpu_profiler.begin_frame(); }
    ~SprintathonGpuProfileFrame() { sprintathon_gpu_profiler.end_frame(); }
    void pass(int index) { sprintathon_gpu_profiler.pass(index); }
};
}

#define MAXIMUM_VERTICES_PER_WORLD_POLYGON (MAXIMUM_VERTICES_PER_POLYGON+4)

class Blur {

private:
	FBOSwapper _swapper;
	Shader *_shader_blur;
	Shader *_shader_bloom;
	GLuint _width;
	GLuint _height;

public:

	Blur(GLuint w, GLuint h, Shader* s_blur, Shader* s_bloom)
	: _swapper(w, h, Bloom_sRGB), _shader_blur(s_blur), _shader_bloom(s_bloom), _width(w), _height(h) {}

	GLuint width() { return _width; }
	GLuint height() { return _height; }
	
	void begin() {
		_swapper.activate();
		glDisable(GL_FRAMEBUFFER_SRGB_EXT); // don't blend for initial
	}

	void end() {
		_swapper.swap();
	}

	void draw(FBOSwapper& dest) {
		
		int passes = _shader_bloom->passes();
		if (passes < 0)
			passes = 5;

		glBlendFunc(GL_SRC_ALPHA,GL_ONE);
		for (int i = 0; i < passes; i++) {
			_shader_blur->enable();
			_shader_blur->setFloat(Shader::U_OffsetX, 1);
			_shader_blur->setFloat(Shader::U_OffsetY, 0);
			_shader_blur->setFloat(Shader::U_Pass, i + 1);
			_swapper.filter(false);

			_shader_blur->setFloat(Shader::U_OffsetX, 0);
			_shader_blur->setFloat(Shader::U_OffsetY, 1);
			_shader_blur->setFloat(Shader::U_Pass, i + 1);
			_swapper.filter(false);

			_shader_bloom->enable();
			_shader_bloom->setFloat(Shader::U_Pass, i + 1);
//			if (Bloom_sRGB)
//				dest.blend(_swapper.current_contents(), true);
//			else
				dest.blend_multisample(_swapper.current_contents());
			
			Shader::disable();
		}
		
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	}
};


struct SprintathonVolumeTarget {
    GLuint fbo=0,color=0,rooms=0;
    int width=0,height=0,room_height=0;
    std::vector<GLfloat> room_data;
};
static SprintathonVolumeTarget sprintathon_volume_target;
static void sprintathon_prepare_volume_buffer(int half_width,int half_height){
    auto& target=sprintathon_volume_target;
    if(!target.fbo) glGenFramebuffersEXT(1,&target.fbo);
    if(!target.color) glGenTextures(1,&target.color);
    if(!target.rooms) glGenTextures(1,&target.rooms);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,target.color);
    if(target.width!=half_width || target.height!=half_height) {
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0x881A /* RGBA16F */,half_width,half_height,0,GL_RGBA,GL_FLOAT,nullptr);
        target.width=half_width;target.height=half_height;
    }
}
static void sprintathon_release_volume_target() {
    auto& t=sprintathon_volume_target;
    if(t.fbo) glDeleteFramebuffersEXT(1,&t.fbo);
    if(t.color) glDeleteTextures(1,&t.color);
    if(t.rooms) glDeleteTextures(1,&t.rooms);
    t=SprintathonVolumeTarget{};
}

RenderRasterize_Shader::RenderRasterize_Shader() = default;
static void sprintathon_release_scenery_light_texture();
// Render_Shader and the shared effect caches live in different translation
// units. At process exit these caches may already have been destroyed, and
// the GL context may be gone. Let their own destructors release CPU storage;
// context destruction releases the remaining GL objects.
RenderRasterize_Shader::~RenderRasterize_Shader() = default;

/*
 * initialize some stuff
 * happens once after opengl, shaders and textures are setup
 */
void RenderRasterize_Shader::setupGL(Rasterizer_Shader_Class& Rasterizer) {
    sprintathon_release_volume_target();
    sprintathon_release_scenery_light_texture();
    sprintathon_gpu_profiler.reset();
    sprintathon_world_detail.reset();
    SprintathonWaterNormals::reset();

	RasPtr = &Rasterizer;

	Shader::loadAll();

	Shader* s_blur = Shader::get(Shader::S_Blur);
	Shader* s_bloom = Shader::get(Shader::S_Bloom);

	blur.reset();
	if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Blur)) {
		if(s_blur && s_bloom) {
			blur.reset(new Blur(640., 640. * graphics_preferences->screen_mode.height / graphics_preferences->screen_mode.width, s_blur, s_bloom));
		}
	}
	
//	glDisable(GL_CULL_FACE);
//	glDisable(GL_LIGHTING);
}

/*
 * override for RenderRasterizerClass::render_tree()
 *
 * with multiple rendering passes for glow effect
 */
const double TWO_PI = 8*atan(1.0);
const float FixedAngleToRadians = TWO_PI/(float(FIXED_ONE)*float(FULL_CIRCLE));
const float FixedAngleToDegrees = 360.0/(float(FIXED_ONE)*float(FULL_CIRCLE));

static void sprintathon_set_visible_sun(Shader* shader,const view_data* view)
{
    const auto& config=Get_OGL_ConfigureData();
    const double azimuth=(SprintathonLuaSun::get(SprintathonLuaSun::Azimuth)-90.0)*0.017453292519943295;
    const double elevation=SprintathonLuaSun::get(SprintathonLuaSun::Elevation)*0.017453292519943295;
    const float strength=graphics_preferences->visible_sun && SprintathonLuaSun::get(SprintathonLuaSun::Enabled) && !IsInfravisionActive() ?
        float(SprintathonLuaSun::get(SprintathonLuaSun::Brightness)) : 0.0f;
    shader->setVector4(Shader::U_VisibleSun,std::cos(azimuth)*std::cos(elevation),
        std::sin(azimuth)*std::cos(elevation),std::sin(elevation),
        std::cos(SprintathonLuaSun::get(SprintathonLuaSun::Size)*0.5*0.017453292519943295));
    shader->setVector4(Shader::U_VisibleSunColor,SprintathonLuaSun::get(SprintathonLuaSun::Red),SprintathonLuaSun::get(SprintathonLuaSun::Green),SprintathonLuaSun::get(SprintathonLuaSun::Blue),strength);
    if(view) shader->setVector4(Shader::U_VisibleSunEye,view->origin.x,view->origin.y,view->origin.z,0);
    shader->setVector4(Shader::U_ScriptSunEffects,
        float(SprintathonLuaSun::get(SprintathonLuaSun::Glare)),
        float(SprintathonLuaSun::get(SprintathonLuaSun::Bloom)),0,0);
}

// One clock shared by liquid surfaces and their projected caustics.
static float sprintathon_liquid_ripple_phase(int16 mediaType, double rate = 1.0, bool wrap = true)
{
	const OGL_ConfigureData& config = Get_OGL_ConfigureData();
	int16 ripple_speed_index = config.AnimatedMediaRippleSpeed;
	switch (mediaType)
	{
		case _media_lava:
			ripple_speed_index = config.AnimatedLavaRippleSpeed;
			break;
		case _media_goo:
			ripple_speed_index = config.AnimatedGooRippleSpeed;
			break;
		case _media_sewage:
			ripple_speed_index = config.AnimatedSewageRippleSpeed;
			break;
		case _media_jjaro:
			ripple_speed_index = config.AnimatedJjaroRippleSpeed;
			break;
		default:
			break;
	}
	const float ripple_speed =
		(static_cast<float>(ripple_speed_index) + 1.0f) * 0.25f;
	// The temporal phase wraps at 2-pi. Shader time harmonics and spatial wave
	// cycles are integers, so both time and scrolling texture UVs wrap cleanly.
	// Accumulate phase instead of multiplying absolute wall-clock time. This
	// lets bullet time change animation speed without producing a phase jump.
	static uint32 last_ripple_tick = machine_tick_count();
	static double ripple_seconds = 0.0;
	const uint32 ripple_tick = machine_tick_count();
	const uint32 elapsed_ripple_ticks = ripple_tick - last_ripple_tick;
	if (elapsed_ripple_ticks > 0)
	{
		const double elapsed = std::min(
			static_cast<double>(elapsed_ripple_ticks) /
				MACHINE_TICKS_PER_SECOND, 0.25);
		ripple_seconds += elapsed *
			(sprintathon_bullet_time_active() ? 0.35 : 1.0);
		last_ripple_tick = ripple_tick;
	}
    const double phase = ripple_seconds * 1.05 * ripple_speed * rate;
    // Texture ripples use a periodic phase; flowing normals need a continuous clock.
    return static_cast<float>(wrap ? std::fmod(phase,TWO_PI) : phase);
}

static float sprintathon_underwater_phase(int16 media_type)
{
	const OGL_ConfigureData& config = Get_OGL_ConfigureData();
	int16 speed_index = config.AnimatedMediaRippleSpeed;
	switch (media_type)
	{
		case _media_lava: speed_index = config.AnimatedLavaRippleSpeed; break;
		case _media_goo: speed_index = config.AnimatedGooRippleSpeed; break;
		case _media_sewage: speed_index = config.AnimatedSewageRippleSpeed; break;
		case _media_jjaro: speed_index = config.AnimatedJjaroRippleSpeed; break;
		default: break;
	}

	static uint32 last_tick = machine_tick_count();
	static double phase = 0.0;
	const uint32 now = machine_tick_count();
	const uint32 elapsed_ticks = now - last_tick;
	if (elapsed_ticks > 0)
	{
		const double elapsed = std::min(
			static_cast<double>(elapsed_ticks) / MACHINE_TICKS_PER_SECOND,
			0.25);
		const double bullet_time_rate =
			sprintathon_bullet_time_active() ? 0.35 : 1.0;
		const double media_rate =
			(static_cast<double>(speed_index) + 1.0) * 0.25;
		phase = std::fmod(phase + elapsed * bullet_time_rate * media_rate,
			TWO_PI);
		last_tick = now;
	}
	return static_cast<float>(phase);
}

static float sprintathon_invisibility_phase()
{
	static uint32 last_tick = machine_tick_count();
	static double phase = 0.0;
	const uint32 now = machine_tick_count();
	const uint32 elapsed_ticks = now - last_tick;
	if (elapsed_ticks > 0)
	{
		const double elapsed = std::min(
			static_cast<double>(elapsed_ticks) / MACHINE_TICKS_PER_SECOND,
			0.25);
		// Cloaking should read as energetic optical interference rather than
		// slow-moving water. Keep the accumulator seamless, but move it quickly.
		phase = std::fmod(phase + elapsed * 5.4, TWO_PI);
		last_tick = now;
	}
	return static_cast<float>(phase);
}

static float sprintathon_fog_haze_phase()
{
	static uint32 last_tick = machine_tick_count();
	static double phase = 0.0;
	const uint32 now = machine_tick_count();
	const uint32 elapsed_ticks = now - last_tick;
	if (elapsed_ticks > 0)
	{
		const double elapsed = std::min(
			static_cast<double>(elapsed_ticks) / MACHINE_TICKS_PER_SECOND,
			0.25);
		const double bullet_time_rate =
			sprintathon_bullet_time_active() ? 0.35 : 1.0;
		phase = std::fmod(phase + elapsed * 0.72 * bullet_time_rate,
			TWO_PI);
		last_tick = now;
	}
	return static_cast<float>(phase);
}

		static GLuint media_scene_texture = 0;
		static GLsizei media_scene_width = 0;
		static GLsizei media_scene_height = 0;
static void sprintathon_prepare_media_scene(GLsizei width,GLsizei height){
		glActiveTextureARB(GL_TEXTURE2_ARB);
		if (!media_scene_texture)
			glGenTextures(1, &media_scene_texture);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, media_scene_texture);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (width != media_scene_width || height != media_scene_height)
		{
			glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
				0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			media_scene_width = width;
			media_scene_height = height;
		}
}

// Share the first invisibility background within one world view. Reset on
// every render invocation, not simulation ticks or diagnostic frame counters.
struct SprintathonInvisibilityCapture {
    bool world=false,captured=false;
    unsigned requests=0,copies=0;
};
static SprintathonInvisibilityCapture sprintathon_invisibility_capture;
static bool sprintathon_reuse_invisibility_capture()
{
    const char* v=std::getenv("SPRINTATHON_REFRACTION_REUSE");
    return v?v[0]!='0':graphics_preferences->reuse_refraction_capture;
}
struct SprintathonInvisibilityView {
    SprintathonInvisibilityCapture previous;
    explicit SprintathonInvisibilityView(bool mainWorld):previous(sprintathon_invisibility_capture){
        sprintathon_invisibility_capture=SprintathonInvisibilityCapture{};
        sprintathon_invisibility_capture.world=mainWorld;
    }
    ~SprintathonInvisibilityView(){
        const auto& c=sprintathon_invisibility_capture;
        const auto frame=SprintathonStartupTiming::get().frame_id;
        if(c.world && c.requests && SprintathonStartupTiming::get().active() && (frame%60==0 || frame==1))
            std::fprintf(stderr,"Invisibility capture frame %llu: requests %u scene-copies %u reused %u; reuse %d\n",
                (unsigned long long)frame,c.requests,c.copies,c.requests-c.copies,int(sprintathon_reuse_invisibility_capture()));
        sprintathon_invisibility_capture=previous;
        // A nested view may overwrite the same texture; a resumed parent must recapture.
        sprintathon_invisibility_capture.captured=false;
    }
};

static void setup_invisibility_refraction(
	Shader *shader, GLsizei width, GLsizei height, float visibility)
{
	static GLuint scene_texture = 0;
	static GLsizei texture_width = 0;
	static GLsizei texture_height = 0;
    static SDL_GLContext texture_context=nullptr;
    auto& capture=sprintathon_invisibility_capture;
    if(texture_context!=SDL_GL_GetCurrentContext()){
        texture_context=SDL_GL_GetCurrentContext();scene_texture=0;
        texture_width=texture_height=0;capture.captured=false;
    }

	glActiveTextureARB(GL_TEXTURE2_ARB);
	if (!scene_texture)
		glGenTextures(1, &scene_texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, scene_texture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (width != texture_width || height != texture_height)
	{
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
			0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		texture_width = width;
		texture_height = height;
        capture.captured=false;
	}
	if(!shader){glActiveTextureARB(GL_TEXTURE0_ARB);return;}
    if(capture.world)++capture.requests;
    if(!capture.world || !sprintathon_reuse_invisibility_capture() || !capture.captured){
        glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
            0, 0, width, height);
        if(capture.world){capture.captured=true;++capture.copies;}
    }
	glActiveTextureARB(GL_TEXTURE0_ARB);

	shader->setFloat(Shader::U_Visibility, visibility);
	shader->setFloat(Shader::U_Time, sprintathon_invisibility_phase());
	shader->setFloat(Shader::U_PixelWidth, static_cast<float>(width));
	shader->setFloat(Shader::U_PixelHeight, static_cast<float>(height));
}

static void setup_classic_invisibility(Shader *shader, float visibility)
{
	shader->setFloat(Shader::U_Visibility, visibility);
	// The fragment shader uses a zero-sized scene to select its inexpensive
	// classic translucent-grey path. Set both values so a prior refractive draw
	// cannot leave valid framebuffer dimensions behind in the shader program.
	shader->setFloat(Shader::U_PixelWidth, 0.0f);
	shader->setFloat(Shader::U_PixelHeight, 0.0f);
}

static GLuint sprintathon_capture_scene_depth(GLsizei width, GLsizei height, bool capture = true)
{
	static GLuint depth_texture = 0;
	static GLsizei texture_width = 0;
	static GLsizei texture_height = 0;

	glActiveTextureARB(GL_TEXTURE2_ARB);
	if (!depth_texture)
		glGenTextures(1, &depth_texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, depth_texture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (width != texture_width || height != texture_height)
	{
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_DEPTH_COMPONENT24,
			width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		texture_width = width;
		texture_height = height;
	}
	if(capture) glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		0, 0, width, height);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	return depth_texture;
}

static GLuint sprintathon_capture_scene_color(GLsizei width, GLsizei height, bool capture = true)
{
	static GLuint color_texture = 0;
	static GLsizei texture_width = 0;
	static GLsizei texture_height = 0;

	glActiveTextureARB(GL_TEXTURE0_ARB);
	if (!color_texture)
		glGenTextures(1, &color_texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, color_texture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (width != texture_width || height != texture_height)
	{
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
			width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		texture_width = width;
		texture_height = height;
	}
	if(capture) glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		0, 0, width, height);
	return color_texture;
}

struct SprintathonShaftSource
{
	GLuint framebuffer = 0;
	GLuint color = 0;
	GLuint depth = 0;
	GLsizei width = 0;
	GLsizei height = 0;
	GLint previous_framebuffer = 0;
	GLint previous_viewport[4] = {0, 0, 0, 0};
	GLint previous_matrix_mode = GL_MODELVIEW;
	bool active = false;
};

static SprintathonShaftSource sprintathon_shaft_source;

static bool sprintathon_fast_shaft_source()
{
    static const bool enabled = [] {
        const char* value = std::getenv("SPRINTATHON_SHAFT_FAST_SOURCE");
        return !value || value[0] != '0';
    }();
    return sprintathon_shaft_source.active && enabled;
}


// Diagnostic CPU wall times include waits inside driver calls. They do not
// measure GPU execution; compare the matching asynchronous GPU frame ID.
struct SprintathonShaftTrace {
    using Clock=std::chrono::steady_clock;
    bool enabled=false, checked=false, matched=false;
    int material=0; // other, occlusion mask, landscape
    unsigned draws[3]={},vertices=0,wall_setups=0,sprite_setups=0,clips=0;
    double draw_ms=0,wall_ms=0,sprite_ms=0,clip_ms=0;
    GLhandleARB mask_program=0;
    GLint linked=0;
    Clock::time_point start;
};
static SprintathonShaftTrace sprintathon_shaft_trace;
struct SprintathonShaftCpuScope {
    double* total;
    SprintathonShaftTrace::Clock::time_point start;
    explicit SprintathonShaftCpuScope(double& value):
        total(sprintathon_shaft_trace.enabled ? &value : nullptr),
        start(total ? SprintathonShaftTrace::Clock::now() :
            SprintathonShaftTrace::Clock::time_point()) {}
    ~SprintathonShaftCpuScope() {
        if(total) *total+=std::chrono::duration<double,std::milli>(
            SprintathonShaftTrace::Clock::now()-start).count();
    }
};
static void sprintathon_trace_mask_bound()
{
    auto& t=sprintathon_shaft_trace;
    if(!t.enabled)return;
    t.material=1;
    if(!t.mask_program) {
        t.mask_program=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
        if(t.mask_program)
            glGetObjectParameterivARB(t.mask_program,GL_OBJECT_LINK_STATUS_ARB,&t.linked);
    }
}
static void sprintathon_trace_shaft_draw(GLsizei count)
{
    auto& t=sprintathon_shaft_trace;
    ++t.draws[t.material];
    if(count>0)t.vertices+=unsigned(count);
    // One binding check per source frame, at the first mask draw. Avoid
    // a synchronous state query for every submitted polygon.
    if(t.material==1 && !t.checked) {
        t.checked=true;
        t.matched=t.mask_program &&
            glGetHandleARB(GL_PROGRAM_OBJECT_ARB)==t.mask_program;
    }
}
// Diagnostic only: preserve traversal, materials, lighting and GL cleanup;
// suppress draw calls issued by main-world billboard helpers, including their
// glow, blur and contact-shadow draws. Models, liquid reflections and weapons
// have separate entry points and remain rendered.
// Diagnostic bit mask: bypass only selected main-world billboard lighting.
// No preferences change; all object geometry and ordinary texture setup remain.
enum { SprintathonSkipActorBake=1, SprintathonSkipActorTint=2, SprintathonSkipActorEmitters=4 };
static unsigned sprintathon_object_light_isolation()
{
    static const unsigned mask=[]()->unsigned{
        const char* mode=std::getenv("SPRINTATHON_OBJECT_LIGHT_TEST");
        if(!mode || !mode[0] || std::strcmp(mode,"off")==0)return 0;
        unsigned value=0;
        if(std::strcmp(mode,"all")==0)value=7;
        else if(std::strcmp(mode,"baked")==0)value=1;
        else if(std::strcmp(mode,"tint")==0)value=2;
        else if(std::strcmp(mode,"emitters")==0)value=4;
        else {std::fprintf(stderr,"Object lighting isolation: unknown mode '%s'; unchanged\n",mode);return 0;}
        std::fprintf(stderr,"Object lighting isolation: %s; bypass baked %d tint %d emitter-collection %d; sprites visible, models/weapons unchanged\n",
            mode,int((value&1)!=0),int((value&2)!=0),int((value&4)!=0));
        return value;
    }();
    return mask;
}
static bool sprintathon_skip_sprite_draws_requested()
{
    static const bool enabled=[](){
        const char* v=std::getenv("SPRINTATHON_SKIP_WORLD_SPRITE_DRAWS");
        const bool on=v && v[0]=='1';
        if(on)std::fprintf(stderr,"Sprite draw isolation: main-world billboard draws disabled; setup retained; models/reflections/weapons retained\n");
        return on;
    }();
    return enabled;
}
static bool sprintathon_suppress_sprite_draw=false;
struct SprintathonSpriteDrawIsolation {
    bool previous;
    explicit SprintathonSpriteDrawIsolation(bool mainWorld):previous(sprintathon_suppress_sprite_draw){
        sprintathon_suppress_sprite_draw=previous || (mainWorld && sprintathon_skip_sprite_draws_requested());
    }
    ~SprintathonSpriteDrawIsolation(){sprintathon_suppress_sprite_draw=previous;}
};
static bool sprintathon_skip_sprite_draw()
{
    if(!sprintathon_suppress_sprite_draw)return false;
    static uint64_t frame=0;
    static unsigned skipped=0;
    const auto now=SprintathonStartupTiming::get().frame_id;
    if(now!=frame){
        if(skipped && frame%60==0 && SprintathonStartupTiming::get().active())
            std::fprintf(stderr,"Sprite draw isolation frame %llu: skipped %u draw calls (object base-draw counters remain attempted draws)\n",(unsigned long long)frame,skipped);
        frame=now;skipped=0;
    }
    ++skipped;return true;
}
static void sprintathon_shaft_draw_arrays(GLenum mode, GLint first, GLsizei count)
{
    SprintathonObjectOps::Scope opsDraw(SprintathonObjectOps::Draw);
    if(sprintathon_skip_sprite_draw())return;
    SprintathonSetupScope setupTime(SprintathonSetupTrace::Draw);
    sprintathon_world_detail.draw();
    SprintathonPortalTrace::get().draw(count);
    if(!sprintathon_shaft_trace.enabled) { glDrawArrays(mode,first,count); return; }
    sprintathon_trace_shaft_draw(count);
    SprintathonShaftCpuScope time(sprintathon_shaft_trace.draw_ms);
    glDrawArrays(mode,first,count);
}
static void sprintathon_draw_opaque_geometry(GLenum mode,GLsizei count,
    const GLfloat* vertices,const GLfloat* uv,bool eligible,SprintathonOpaqueGeometry::CpuEntry* prepared=nullptr){
    const bool cached=eligible && ((prepared && SprintathonOpaqueGeometry::bindPrepared(*prepared)) ||
        SprintathonOpaqueGeometry::bind(count,vertices,uv,prepared?&prepared->gpuOffset:nullptr));
    sprintathon_shaft_draw_arrays(mode,0,count);
    if(cached)SprintathonOpaqueGeometry::restore(vertices,uv);
}
void sprintathon_preload_opaque_geometry(){SprintathonOpaqueGeometry::preload();SprintathonOpaqueWorld::preload();}

static void sprintathon_shaft_draw_elements(GLenum mode, GLsizei count, GLenum type, const void* indices)
{
    SprintathonObjectOps::Scope opsDraw(SprintathonObjectOps::Draw);
    if(sprintathon_skip_sprite_draw())return;
    SprintathonSetupScope setupTime(SprintathonSetupTrace::Draw);
    sprintathon_world_detail.draw();
    SprintathonPortalTrace::get().draw(count);
    if(!sprintathon_shaft_trace.enabled) { glDrawElements(mode,count,type,indices); return; }
    sprintathon_trace_shaft_draw(count);
    SprintathonShaftCpuScope time(sprintathon_shaft_trace.draw_ms);
    glDrawElements(mode,count,type,indices);
}
static void sprintathon_finish_shaft_trace()
{
    auto& t=sprintathon_shaft_trace;
    if(!t.enabled)return;
    const double total=std::chrono::duration<double,std::milli>(
        SprintathonShaftTrace::Clock::now()-t.start).count();
    const auto id=SprintathonStartupTiming::get().frame_id;
    if(total>=12.0 || id%60==0 || (t.checked && (!t.matched || !t.linked)))
        std::fprintf(stderr,"Shaft detail frame %llu: dedicated %d size %dx%d; CPU total %.2f draw-calls %.2f wall-setup %.2f sprite-setup %.2f clipping %.2f other %.2f ms; draws mask/sky/other %u/%u/%u vertices-or-indices %u; setups wall/sprite %u/%u clips %u; mask-first-draw checked/matched/linked %d/%d/%d\n",
            static_cast<unsigned long long>(id),int(sprintathon_fast_shaft_source()),
            sprintathon_shaft_source.width,sprintathon_shaft_source.height,
            total,t.draw_ms,t.wall_ms,t.sprite_ms,t.clip_ms,
            std::max(0.0,total-t.draw_ms-t.wall_ms-t.sprite_ms-t.clip_ms),
            t.draws[1],t.draws[2],t.draws[0],t.vertices,t.wall_setups,t.sprite_setups,t.clips,
            int(t.checked),int(t.matched),int(t.linked));
    t.enabled=false;
}

static const float sprintathon_shaft_overscan = 1.60f;

static bool sprintathon_begin_shaft_source(GLsizei main_width,
	GLsizei main_height, const GLfloat *projection, bool allocate_only = false)
{
	if (!FBO_Allowed)
		return false;

	SprintathonShaftSource& source = sprintathon_shaft_source;
	// Shaft sources need sky silhouettes, not full scene detail. Half the
	// previous width and height shades one quarter as many source pixels.
	// Keep overscan and projection unchanged; the filter receives the actual
	// source dimensions below. Retain the old size for visual/performance A/B.
	static const bool legacy_resolution = [] {
		const char* value = std::getenv("SPRINTATHON_SHAFT_LEGACY_RESOLUTION");
		return value && value[0] == '1';
	}();
	const GLsizei divisor = legacy_resolution ? 4 : 8;
	const GLsizei width = std::max<GLsizei>(1, (main_width * 3 + divisor - 1) / divisor);
	const GLsizei height = std::max<GLsizei>(1, (main_height * 3 + divisor - 1) / divisor);

	if (!source.framebuffer)
		glGenFramebuffersEXT(1, &source.framebuffer);
	if (!source.color)
		glGenTextures(1, &source.color);
	if (!source.depth)
		glGenTextures(1, &source.depth);

	if (width != source.width || height != source.height)
	{
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, source.color);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, width, height,
			0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		glActiveTextureARB(GL_TEXTURE3_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, source.depth);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_DEPTH_COMPONENT24,
			width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		source.width = width;
		source.height = height;
	}

	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &source.previous_framebuffer);
	glGetIntegerv(GL_VIEWPORT, source.previous_viewport);
	glGetIntegerv(GL_MATRIX_MODE, &source.previous_matrix_mode);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, source.framebuffer);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_RECTANGLE_ARB, source.color, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
		GL_TEXTURE_RECTANGLE_ARB, source.depth, 0);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) !=
		GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, source.previous_framebuffer);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		return false;
	}

    if(allocate_only){
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,source.previous_framebuffer);
        glActiveTextureARB(GL_TEXTURE0_ARB);return true;
    }
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glViewport(0, 0, width, height);
	glDisable(GL_SCISSOR_TEST);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	GLfloat expanded_projection[16];
	std::copy(projection, projection + 16, expanded_projection);
	expanded_projection[0] /= sprintathon_shaft_overscan;
	expanded_projection[5] /= sprintathon_shaft_overscan;
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadMatrixf(expanded_projection);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	source.active = true;
    sprintathon_shaft_trace=SprintathonShaftTrace{};
    sprintathon_shaft_trace.enabled=SprintathonStartupTiming::get().active();
    sprintathon_shaft_trace.start=SprintathonShaftTrace::Clock::now();
	glActiveTextureARB(GL_TEXTURE0_ARB);
	return true;
}

static void sprintathon_end_shaft_source()
{
	SprintathonShaftSource& source = sprintathon_shaft_source;
	if (!source.active)
		return;
    sprintathon_finish_shaft_trace();

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(source.previous_matrix_mode);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, source.previous_framebuffer);
	glPopAttrib();
	glViewport(source.previous_viewport[0], source.previous_viewport[1],
		source.previous_viewport[2], source.previous_viewport[3]);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	source.active = false;
}

	static GLuint ao_texture = 0;
	static GLsizei ao_texture_width = 0;
	static GLsizei ao_texture_height = 0;

static void sprintathon_prepare_ao(GLsizei mask_width,GLsizei mask_height){
	glActiveTextureARB(GL_TEXTURE1_ARB);
	if (!ao_texture)
		glGenTextures(1, &ao_texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, ao_texture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (mask_width != ao_texture_width || mask_height != ao_texture_height)
	{
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
			mask_width, mask_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		ao_texture_width = mask_width;
		ao_texture_height = mask_height;
	}

}

static void sprintathon_draw_ambient_occlusion(GLuint color_texture,
	GLuint depth_texture, GLsizei width, GLsizei height,
	const GLfloat *projection, float strength, float fog_mode)
{
	const GLsizei mask_width = std::max<GLsizei>(1, (width + 1) / 2);
	const GLsizei mask_height = std::max<GLsizei>(1, (height + 1) / 2);

	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	// Keep the AO field separate from scene color so it can be softened without
	// blurring wall textures. Half resolution provides a broad, inexpensive
	// kernel and remains compatible with the renderer's framebuffer-copy path.
	sprintathon_prepare_ao(mask_width,mask_height);

	glViewport(0, 0, mask_width, mask_height);
	Shader *shader = Shader::get(Shader::S_AmbientOcclusion);
	shader->enable();
	shader->setFloat(Shader::U_PixelWidth, static_cast<float>(width));
	shader->setFloat(Shader::U_PixelHeight, static_cast<float>(height));
	shader->setFloat(Shader::U_ScaleX, projection[10]);
	shader->setFloat(Shader::U_ScaleY, projection[14]);

	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, depth_texture);
	glActiveTextureARB(GL_TEXTURE0_ARB);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), 0.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), static_cast<float>(height)); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, static_cast<float>(height)); glVertex2f(-1.0f, 1.0f);
	glEnd();
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	Shader::disable();

	// The mask currently occupies the lower-left half-size viewport. Copy it to
	// its own texture; the following full-screen scene composite immediately
	// restores the overwritten framebuffer region before presentation.
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, ao_texture);
	glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		0, 0, mask_width, mask_height);

	glViewport(0, 0, width, height);
	shader = Shader::get(Shader::S_AmbientOcclusionComposite);
	shader->enable();
	shader->setFloat(Shader::U_PixelWidth, static_cast<float>(width));
	shader->setFloat(Shader::U_PixelHeight, static_cast<float>(height));
	shader->setFloat(Shader::U_ScaleX, projection[10]);
	shader->setFloat(Shader::U_ScaleY, projection[14]);
	shader->setFloat(Shader::U_LogicalWidth, projection[0]);
	shader->setFloat(Shader::U_LogicalHeight, projection[5]);
	shader->setFloat(Shader::U_BloomScale, strength);
	shader->setFloat(Shader::U_FogMode, fog_mode);

	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, depth_texture);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, ao_texture);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, color_texture);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), 0.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), static_cast<float>(height)); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, static_cast<float>(height)); glVertex2f(-1.0f, 1.0f);
	glEnd();
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	Shader::disable();
	glPopAttrib();
	glActiveTextureARB(GL_TEXTURE0_ARB);
}

static void sprintathon_draw_fog_haze(GLuint color_texture,
	GLuint depth_texture, GLsizei width, GLsizei height,
	const GLfloat *projection, float fog_mode, float global_fog_blend,
	bool distortion_enabled, bool clouds_enabled, float cloud_intensity)
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	Shader *shader = Shader::get(Shader::S_FogHaze);
	shader->enable();
	shader->setFloat(Shader::U_PixelWidth, static_cast<float>(width));
	shader->setFloat(Shader::U_PixelHeight, static_cast<float>(height));
	shader->setFloat(Shader::U_ScaleX, projection[10]);
	shader->setFloat(Shader::U_ScaleY, projection[14]);
	shader->setFloat(Shader::U_Time, sprintathon_fog_haze_phase());
	shader->setFloat(Shader::U_FogMode, fog_mode);
	shader->setFloat(Shader::U_BloomScale, global_fog_blend);
	shader->setFloat(Shader::U_BloomShift, cloud_intensity);
	shader->setFloat(Shader::U_MediaRipple,
		distortion_enabled ? 1.0f : 0.0f);
	shader->setFloat(Shader::U_MediaWetness,
		clouds_enabled ? 1.0f : 0.0f);

	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, depth_texture);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, color_texture);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), 0.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), static_cast<float>(height)); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, static_cast<float>(height)); glVertex2f(-1.0f, 1.0f);
	glEnd();
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	Shader::disable();
	glPopAttrib();
	glActiveTextureARB(GL_TEXTURE0_ARB);
}

struct SprintathonShaftScattering
{
	GLuint framebuffer = 0;
	GLuint color = 0;
	GLsizei width = 0;
	GLsizei height = 0;
};
static SprintathonShaftScattering sprintathon_shaft_scattering;

static bool sprintathon_prepare_shaft_scattering(GLsizei width, GLsizei height)
{
	const char *override_resolution = std::getenv("SPRINTATHON_SHAFT_HALF_RES");
	if (!FBO_Allowed || (override_resolution && override_resolution[0] == '0') ||
		!SDL_GL_ExtensionSupported("GL_ARB_texture_float"))
		return false;
	SprintathonShaftScattering& target = sprintathon_shaft_scattering;
	if (!target.framebuffer) glGenFramebuffersEXT(1, &target.framebuffer);
	if (!target.color) glGenTextures(1, &target.color);
	const GLsizei half_width = std::max<GLsizei>(1, (width + 1) / 2);
	const GLsizei half_height = std::max<GLsizei>(1, (height + 1) / 2);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, target.color);
	if (target.width != half_width || target.height != half_height)
	{
		// RGBA16F: RGB carries scattering; alpha carries logarithmic scene depth.
		// Nearest sampling lets the composite apply its own depth-aware weights.
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0x881A /* GL_RGBA16F */,
			half_width, half_height, 0, GL_RGBA, GL_FLOAT, nullptr);
		target.width = half_width;
		target.height = half_height;
	}
	GLint previous_framebuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previous_framebuffer);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, target.framebuffer);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_RECTANGLE_ARB, target.color, 0);
	const bool ready = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) ==
		GL_FRAMEBUFFER_COMPLETE_EXT;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, previous_framebuffer);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	return ready;
}

static void sprintathon_draw_landscape_light_shafts(GLuint color_texture,
	GLuint scene_depth, GLuint source_color, GLuint source_depth, GLsizei width, GLsizei height,
	GLsizei source_width, GLsizei source_height,
	const GLfloat *projection, const GLfloat *modelview, float camera_yaw, float camera_pitch,
	float strength, float length, float sun_azimuth, float sun_elevation)
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	const bool half_resolution = sprintathon_prepare_shaft_scattering(width, height);
	GLint previous_framebuffer, previous_draw_buffer;
	GLint previous_viewport[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previous_framebuffer);
	glGetIntegerv(GL_DRAW_BUFFER, &previous_draw_buffer);
	glGetIntegerv(GL_VIEWPORT, previous_viewport);
	if (half_resolution)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, sprintathon_shaft_scattering.framebuffer);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glViewport(0, 0, sprintathon_shaft_scattering.width, sprintathon_shaft_scattering.height);
	}

	Shader *shader = Shader::get(Shader::S_LandscapeLightShafts);
	shader->enable();
	sprintathon_set_visible_sun(shader,nullptr);
    // Project the world sun through this frame's actual camera, including roll
    // and asymmetric projection. A direction has w=0, so jumping adds no parallax.
    const float direction[4]={std::cos(sun_azimuth)*std::cos(sun_elevation),
        std::sin(sun_azimuth)*std::cos(sun_elevation),std::sin(sun_elevation),0};
    float eye[4]={0,0,0,0},clip[4]={0,0,0,0};
    for(int row=0;row<4;++row)
        for(int col=0;col<4;++col) eye[row]+=modelview[col*4+row]*direction[col];
    for(int row=0;row<4;++row)
        for(int col=0;col<4;++col) clip[row]+=projection[col*4+row]*eye[col];
    const float denominator=std::max(clip[3],0.08f);
    shader->setVector4(Shader::U_VisibleSunEye,clip[0]/denominator,
        clip[1]/denominator,-eye[2],0);

	shader->setFloat(Shader::U_Pass, half_resolution ? 1.0f : 0.0f);
	shader->setFloat(Shader::U_ScaleX, projection[10]);
	shader->setFloat(Shader::U_ScaleY, projection[14]);
	shader->setFloat(Shader::U_PixelWidth, static_cast<float>(width));
	shader->setFloat(Shader::U_PixelHeight, static_cast<float>(height));
	shader->setFloat(Shader::U_OffsetX, static_cast<float>(source_width));
	shader->setFloat(Shader::U_OffsetY, static_cast<float>(source_height));
	shader->setFloat(Shader::U_Repeat, sprintathon_shaft_overscan);
	shader->setFloat(Shader::U_BloomScale, strength);
	shader->setFloat(Shader::U_BloomShift, length);
	shader->setFloat(Shader::U_LogicalWidth, projection[0]);
	shader->setFloat(Shader::U_LogicalHeight, projection[5]);
	shader->setFloat(Shader::U_Yaw, camera_yaw);
	shader->setFloat(Shader::U_Pitch, camera_pitch);
	shader->setFloat(Shader::U_SunAzimuth, sun_azimuth);
	shader->setFloat(Shader::U_SunElevation, sun_elevation);

	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, scene_depth);
	glActiveTextureARB(GL_TEXTURE3_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, source_depth);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, source_color);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, color_texture);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	const auto draw_quad = [width, height]() {
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
		glTexCoord2f(static_cast<float>(width), 0.0f); glVertex2f(1.0f, -1.0f);
		glTexCoord2f(static_cast<float>(width), static_cast<float>(height)); glVertex2f(1.0f, 1.0f);
		glTexCoord2f(0.0f, static_cast<float>(height)); glVertex2f(-1.0f, 1.0f);
		glEnd();
	};
	draw_quad();
	if (half_resolution)
	{
		Shader::disable();
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, previous_framebuffer);
		glDrawBuffer(previous_draw_buffer);
		glViewport(previous_viewport[0], previous_viewport[1],
			previous_viewport[2], previous_viewport[3]);
		shader = Shader::get(Shader::S_LandscapeLightShaftsComposite);
		shader->enable();
        sprintathon_set_visible_sun(shader,nullptr);
        const float sunRadius=float(SprintathonLuaSun::get(SprintathonLuaSun::Size))*0.5f*0.017453292519943295f;
        shader->setVector4(Shader::U_VisibleSunEye,clip[0]/denominator,clip[1]/denominator,
            std::abs(projection[0])*sunRadius/denominator,
            std::abs(projection[5])*sunRadius/denominator);
        if(-eye[2]<=0.1f) shader->setVector4(Shader::U_VisibleSunColor,0,0,0,0);
        shader->setVector4(Shader::U_LiquidSkyParams,float(source_width),float(source_height),
            sprintathon_shaft_overscan,strength);
        glActiveTextureARB(GL_TEXTURE5_ARB);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source_color);


		shader->setFloat(Shader::U_PixelWidth, static_cast<float>(width));
		shader->setFloat(Shader::U_PixelHeight, static_cast<float>(height));
		shader->setFloat(Shader::U_OffsetX, static_cast<float>(sprintathon_shaft_scattering.width));
		shader->setFloat(Shader::U_OffsetY, static_cast<float>(sprintathon_shaft_scattering.height));
		shader->setFloat(Shader::U_ScaleX, projection[10]);
		shader->setFloat(Shader::U_ScaleY, projection[14]);
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, sprintathon_shaft_scattering.color);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, color_texture);
		draw_quad();
	}

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	Shader::disable();
	glPopAttrib();
	glActiveTextureARB(GL_TEXTURE0_ARB);
}


struct SprintathonFlareBuffers {
    GLuint framebuffer[2]={0,0}, color[2]={0,0};
    GLsizei width=0,height=0;
    bool ready=false;
};
static SprintathonFlareBuffers sprintathon_flare_buffers;
static bool sprintathon_prepare_flare_buffers(GLsizei width,GLsizei height) {
    const char* override_mode=std::getenv("SPRINTATHON_FLARE_HALF_RES");
    if(!FBO_Allowed || (override_mode && override_mode[0]=='0') ||
       !SDL_GL_ExtensionSupported("GL_ARB_texture_float")) return false;
    auto& t=sprintathon_flare_buffers;
    const GLsizei w=std::max<GLsizei>(1,(width+1)/2),h=std::max<GLsizei>(1,(height+1)/2);
    if(t.width==w && t.height==h) return t.ready;
    GLint previous;glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&previous);
    if(!t.framebuffer[0])glGenFramebuffersEXT(2,t.framebuffer);
    if(!t.color[0])glGenTextures(2,t.color);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    t.ready=true;
    for(int i=0;i<2;++i) {
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,t.color[i]);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0x881A /* RGBA16F */,w,h,0,GL_RGBA,GL_FLOAT,nullptr);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.framebuffer[i]);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,t.color[i],0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        t.ready=t.ready && glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    }
    t.width=w;t.height=h;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,previous);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    return t.ready;
}

static void sprintathon_draw_anamorphic_lens_flare(GLuint color_texture,
	GLuint depth_texture, GLsizei width, GLsizei height,
	const GLfloat *projection, float strength, float fog_mode)
{
	if(strength<=0.0f)return;
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	GLint previous_fbo=0,previous_draw=0,viewport[4];
	glGetIntegerv(GL_VIEWPORT,viewport);
	glGetIntegerv(GL_DRAW_BUFFER,&previous_draw);
	if(FBO_Allowed)glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&previous_fbo);
	const bool reduced=sprintathon_prepare_flare_buffers(width,height);
	Shader *shader = Shader::get(Shader::S_AnamorphicLensFlare);
	shader->enable();
    shader->setFloat(Shader::U_Pass, static_cast<float>(
        Get_OGL_ConfigureData().LensFlareStyle == 2 ? 2 : 0));
	shader->setFloat(Shader::U_PixelWidth, static_cast<float>(width));
	shader->setFloat(Shader::U_PixelHeight, static_cast<float>(height));
	shader->setFloat(Shader::U_BloomScale, strength);
	shader->setFloat(Shader::U_ScaleX, projection[10]);
	shader->setFloat(Shader::U_ScaleY, projection[14]);
	shader->setFloat(Shader::U_FogMode, fog_mode);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, depth_texture);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, color_texture);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	const auto draw_quad=[width,height]() {
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), 0.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(static_cast<float>(width), static_cast<float>(height)); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, static_cast<float>(height)); glVertex2f(-1.0f, 1.0f);
	glEnd();
    };
    if(reduced) {
        auto& t=sprintathon_flare_buffers;
        const bool glow=Get_OGL_ConfigureData().LensFlareStyle==2;
        shader->setFloat(Shader::U_OffsetX,float(t.width));
        shader->setFloat(Shader::U_OffsetY,float(t.height));
        glViewport(0,0,t.width,t.height);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.framebuffer[0]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        // Avoid binding an attached render target as a sampled texture.
        glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        shader->setFloat(Shader::U_Pass,glow?4.0f:3.0f);draw_quad();
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.framebuffer[1]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,t.color[0]);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        shader->setFloat(Shader::U_Pass,glow?6.0f:5.0f);draw_quad();
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,previous_fbo);
        glDrawBuffer(previous_draw);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,t.color[1]);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        shader->setFloat(Shader::U_Pass,7.0f);draw_quad();
    } else draw_quad();
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	Shader::disable();
	glPopAttrib();
	glActiveTextureARB(GL_TEXTURE0_ARB);
}


// Game-tick lifetime keeps flares paused along with the game.
struct SprintathonDroppedFlare {
    float x, y, z;
    int32 placed_tick;
    short polygon_index;
    std::shared_ptr<SoundPlayer> burning_sound;
};
static std::vector<SprintathonDroppedFlare> sprintathon_dropped_flares;
static int16 sprintathon_flare_level = NONE;
static constexpr int sprintathon_flare_capacity = 2;
static constexpr int32 sprintathon_flare_lifetime = 30 * TICKS_PER_SECOND;

static FileSpecifier sprintathon_flare_sound_file(const char *name)
{
    FileSpecifier file(std::string("snd/") + name);
    if (!file.Exists()) file = FileSpecifier(std::string("../snd/") + name);
    if (!file.Exists() && !file.SetNameWithPath(name))
        file = FileSpecifier(get_data_path(kPathDefaultData) + "/Sprintathon/" + name);
    return file;
}

static void sprintathon_play_flare_sound(SprintathonDroppedFlare& flare,
                                         const char *name, bool loop)
{
    FileSpecifier file = sprintathon_flare_sound_file(name);
    if (!file.Exists()) return;
    SoundParameters params;
    params.is_2d = false;
    params.in_world = true;
    params.loop = loop;
    params.spatialize_stereo = true;
    // Keep the loop strong nearby, with a steeper fade beyond that radius.
    params.source_gain = 4.0f;
    params.behavior = loop ? _sound_is_normal : _sound_is_loud;
    params.distance_rolloff = loop ? 4.0f : 1.0f;
    params.source_location3d.point.x = static_cast<world_distance>(flare.x);
    params.source_location3d.point.y = static_cast<world_distance>(flare.y);
    params.source_location3d.point.z = static_cast<world_distance>(flare.z);
    params.source_location3d.polygon_index = flare.polygon_index;
    auto player = SoundManager::instance()->PlayExternalSound(file, params);
    if (loop) flare.burning_sound = player;
}

static void sprintathon_stop_flare(SprintathonDroppedFlare& flare)
{
    if (flare.burning_sound) flare.burning_sound->AskStop();
    flare.burning_sound.reset();
}

static void sprintathon_update_flares()
{
    if (sprintathon_flare_level != dynamic_world->current_level_number) {
        for (auto& flare : sprintathon_dropped_flares) sprintathon_stop_flare(flare);
        sprintathon_dropped_flares.clear();
        sprintathon_flare_level = dynamic_world->current_level_number;
    }
    const int32 tick = dynamic_world->tick_count;
    sprintathon_dropped_flares.erase(
        std::remove_if(sprintathon_dropped_flares.begin(), sprintathon_dropped_flares.end(),
            [tick](SprintathonDroppedFlare& flare) {
                const bool expired = tick < flare.placed_tick ||
                    tick - flare.placed_tick >= sprintathon_flare_lifetime;
                if (expired) sprintathon_stop_flare(flare);
                return expired;
            }), sprintathon_dropped_flares.end());
}

bool sprintathon_drop_flare()
{
    if (!dynamic_world || !current_player ||
        !graphics_preferences->projectile_lights_per_pixel) return false;
    sprintathon_update_flares();
    // Keep both burning flares; another can be dropped after one expires.
    if (sprintathon_dropped_flares.size() >= sprintathon_flare_capacity)
        return false;
    const float direction = current_player->facing * (6.28318530718f / FULL_CIRCLE);
    const float forward = 0.75f * WORLD_ONE;
    const short polygon_index = current_player->supporting_polygon_index;
    const float floor_z = polygon_index != NONE ?
        float(get_polygon_data(polygon_index)->floor_height) :
        float(current_player->location.z);
    sprintathon_dropped_flares.push_back({
        float(current_player->location.x) + std::cos(direction) * forward,
        float(current_player->location.y) + std::sin(direction) * forward,
        floor_z + WORLD_ONE / 8.0f, dynamic_world->tick_count, polygon_index, {}});
    auto& flare = sprintathon_dropped_flares.back();
    sprintathon_play_flare_sound(flare, "lightflare.ogg", false);
    sprintathon_play_flare_sound(flare, "burningflare.ogg", true);
    return true;
}

static float sprintathon_flare_strength(const SprintathonDroppedFlare& flare)
{
    const float age = float(dynamic_world->tick_count - flare.placed_tick) /
        sprintathon_flare_lifetime;
    const float burnout = std::min(1.0f, (1.0f - age) * 5.0f);
    const float phase = float(dynamic_world->tick_count - flare.placed_tick);
    const float flicker = 0.80f + 0.12f * std::sin(phase * 0.91f) +
        0.08f * std::sin(phase * 2.47f);
    return std::max(0.0f, burnout * flicker);
}

// A depth-tested billboard spark at each flare. The shader still lights the world.
// Experimental camera-facing fog cone approximation, confined to its source sector.
struct SprintathonFogCone {
    short object_index, polygon_index;
    int source_shape;
    float anchor_z;
    float x, y, z, floor, radius, red, green, blue;
};
static std::vector<SprintathonFogCone> sprintathon_fog_cones;
static std::vector<SprintathonFogCone> sprintathon_floor_cones;

static void sprintathon_collect_fog_cone(short index, float z, const float *rgb)
{
    if (!graphics_preferences->ceiling_fog_cones || index < 0 ||
        size_t(index) >= ObjectList.size()) return;
    // Render caches can outlive objects deleted while the editor is paused.
    // get_object_data asserts on unused slots, so inspect the slot first.
    const object_data *object = &ObjectList[index];
    if (!SLOT_IS_USED(object) || GET_OBJECT_OWNER(object) != _object_is_scenery ||
        object->polygon == NONE) return;
    const polygon_data *polygon = get_polygon_data(object->polygon);
    // Ceiling scenery uses its ceiling anchor, not the sprite's apparent size.
    if (std::abs(int(object->location.z) - polygon->ceiling_height) > WORLD_ONE/4) return;
    for (const auto& cone : sprintathon_fog_cones)
        if (cone.object_index == index) return;
    float radius = WORLD_ONE * 1.25f;
    // Sector divisions are not walls. Walk through openings that contain the
    // full cone height, and limit its radius only at actual room boundaries.
    std::vector<short> sectors(1, object->polygon);
    for (size_t visit=0; visit<sectors.size(); ++visit) {
        const auto* sector=get_polygon_data(sectors[visit]);
        for (short i=0; i<sector->vertex_count; ++i) {
            const auto& a=get_endpoint_data(sector->endpoint_indexes[i])->vertex;
            const auto& b=get_endpoint_data(sector->endpoint_indexes[(i+1)%sector->vertex_count])->vertex;
            const float dx=float(b.x)-a.x,dy=float(b.y)-a.y;
            const float length2=dx*dx+dy*dy;
            if(length2<1)continue;
            const float t=std::max(0.0f,std::min(1.0f,
                ((object->location.x-a.x)*dx+(object->location.y-a.y)*dy)/length2));
            const float ex=object->location.x-(a.x+t*dx),ey=object->location.y-(a.y+t*dy);
            const float distance=std::sqrt(ex*ex+ey*ey);
            if(distance>=radius)continue;
            const short adjacent=sector->adjacent_polygon_indexes[i];
            if(adjacent!=NONE) {
                const auto* neighbor=get_polygon_data(adjacent);
                if(neighbor->floor_height<=polygon->floor_height &&
                   neighbor->ceiling_height>=std::min(z,float(polygon->ceiling_height)-1)) {
                    if(std::find(sectors.begin(),sectors.end(),adjacent)!=sectors.end())continue;
                    if(sectors.size()<64) { sectors.push_back(adjacent); continue; }
                }
            }
            radius=std::min(radius,distance);
        }
    }
    if (radius < WORLD_ONE*0.03f) return;
    z = std::min(z, float(polygon->ceiling_height)-1);
    if (z <= polygon->floor_height) return;
    sprintathon_fog_cones.push_back({index, object->polygon, int(object->shape),
        float(object->location.z), float(object->location.x), float(object->location.y),
        z, float(polygon->floor_height), radius, rgb[0], rgb[1], rgb[2]});
}

static void sprintathon_draw_fog_cones(const view_data *camera)
{
    if (!graphics_preferences->ceiling_fog_cones ||
        graphics_preferences->ceiling_fog_cone_strength == 0 || sprintathon_fog_cones.empty()) return;
    const auto fog = OGL_GetCurrFogData();
    const bool forced = TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_ForceFog);
    if (!TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Fog) ||
        ((!fog || !fog->IsPresent) && !forced)) return;
    auto distance2 = [&](const SprintathonFogCone& c) {
        const float dx=c.x-camera->origin.x, dy=c.y-camera->origin.y;
        return dx*dx+dy*dy;
    };
    Shader::disable();
    GLint active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_TEXTURE_2D); glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE); glDisable(GL_FOG);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    const float density = fog && fog->IsPresent ? std::min(1.0f, 8.0f/std::max(1.0f, fog->Depth)) : 0.5f;
    const float strength = graphics_preferences->ceiling_fog_cone_strength/100.0f;
    for (size_t n=0; n<sprintathon_fog_cones.size(); ++n) {
        const auto& c=sprintathon_fog_cones[n];
        const float distance=std::sqrt(distance2(c));
        // Visibility is portal-based, never distance- or nearest-light-based.
        if (!TEST_RENDER_FLAG(c.polygon_index, _polygon_is_visible)) continue;
        if (distance < 1) continue;
        const float rx=-(camera->origin.y-c.y)/distance, ry=(camera->origin.x-c.x)/distance;
        const float height=std::min(c.z-c.floor, 5.0f*WORLD_ONE);
        // Smoothly sampled cross-section: transparent edges, no hard base cap.
        for (int j=0;j<24;++j) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int i=0;i<=16;++i) for (int k=0;k<2;++k) {
                const float t=float(j+k)/24, u=float(i)/8-1;
                const float tip_width=std::min(c.radius*0.1f, WORLD_ONE*0.04f);
                const float base_width=std::min(c.radius, WORLD_ONE*0.04f+height*0.28f);
                const float width=tip_width+(base_width-tip_width)*t;
                const float noise=0.92f+0.08f*std::sin(t*13+u*4+camera->tick_count/float(TICKS_PER_SECOND)*0.6f);
                const float alpha=0.32f*strength*density*noise*(1-u*u)*std::sin(t*3.14159265f);
                glColor4f(c.red,c.green,c.blue,alpha);
                glVertex3f(c.x+rx*u*width,c.y+ry*u*width,c.z-t*height);
            }
            glEnd();
        }
    }
    glPopAttrib(); glActiveTextureARB(active_texture);
}

static void sprintathon_draw_flare_stars(const view_data *camera)
{
    if (sprintathon_dropped_flares.empty()) return;
    Shader::disable();
    GLint active_texture = GL_TEXTURE0_ARB;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                 GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_POINT_BIT);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (const auto& flare : sprintathon_dropped_flares) {
        const float flicker = sprintathon_flare_strength(flare);
        if (flicker <= 0.0f) continue;
        float dx = camera->origin.x - flare.x;
        float dy = camera->origin.y - flare.y;
        float dz = camera->origin.z - flare.z;
        const float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (distance < 1.0f) continue;
        dx /= distance; dy /= distance; dz /= distance;
        const float horizontal = std::max(0.001f, std::sqrt(dx*dx + dy*dy));
        const float rx = -dy / horizontal, ry = dx / horizontal;
        const float ux = -dz*ry, uy = dz*rx, uz = horizontal;
        const float size = WORLD_ONE * 0.11f;
        const auto vertex = [&](float u, float v) {
            glVertex3f(flare.x + rx*u + ux*v,
                       flare.y + ry*u + uy*v, flare.z + uz*v);
        };
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0f, 0.28f, 0.10f, flicker);
        vertex(0, 0);
        for (int i = 0; i <= 16; ++i) {
            const float angle = i*(6.28318530718f/16.0f);
            const float length = size*(i%4 == 0 ? 1.30f :
                                       (i%2 == 0 ? 1.0f : 0.42f));
            glColor4f(1.0f, 0.10f, 0.04f, 0.0f);
            vertex(std::cos(angle)*length, std::sin(angle)*length);
        }
        glEnd();
        // Additive layers saturate the compact center to a hot near-white red.
        for (int pass = 0; pass < 3; ++pass) {
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(1.0f, 0.65f, 0.50f, flicker);
            vertex(0, 0);
            for (int i = 0; i <= 12; ++i) {
                const float angle = i*(6.28318530718f/12.0f);
                glColor4f(1.0f, 0.20f, 0.06f, 0.0f);
                vertex(std::cos(angle)*size*0.32f, std::sin(angle)*size*0.32f);
            }
            glEnd();
        }
        glPointSize(4.0f);
        glBegin(GL_POINTS);
        glColor4f(1.0f, 0.90f, 0.75f, flicker);
        vertex(0, 0);
        glEnd();
    }
    glActiveTextureARB(active_texture);
    glPopAttrib();
}

// Small translucent smoke puffs, timed by game ticks so pause and bullet time
// affect smoke just as they affect the burning flare. No particle assets needed.
static void sprintathon_draw_flare_smoke(const view_data *camera)
{
    if (sprintathon_dropped_flares.empty()) return;
    struct Puff { float x, y, z, radius, alpha, distance_squared; };
    std::vector<Puff> puffs;
    puffs.reserve(sprintathon_flare_capacity * 8);
    const float interval = 0.65f;
    const float lifetime = interval * 8;
    for (const auto& flare : sprintathon_dropped_flares) {
        const float age = float(dynamic_world->tick_count - flare.placed_tick) /
                          TICKS_PER_SECOND;
        if (age < 0.0f || age >= float(sprintathon_flare_lifetime) / TICKS_PER_SECOND)
            continue;
        const int latest = int(age / interval);
        for (int i = 0; i < 8; ++i) {
            const int emission = latest - i;
            if (emission < 0) continue;
            const float elapsed = age - emission * interval;
            const float progress = elapsed / lifetime;
            if (progress >= 1.0f) continue;
            const float phase = emission * 2.39996323f + flare.x * 0.001f;
            const float drift = WORLD_ONE * 0.12f * progress;
            Puff puff;
            puff.x = flare.x + std::sin(phase + elapsed * 0.55f) * drift;
            puff.y = flare.y + std::cos(phase + elapsed * 0.45f) * drift;
            puff.z = flare.z + WORLD_ONE * (0.07f + elapsed * 0.16f);
            puff.radius = WORLD_ONE * (0.045f + progress * 0.16f);
            puff.alpha = 0.16f * std::min(elapsed / 0.3f, 1.0f) *
                         (1.0f - progress) * (1.0f - progress);
            const float dx = puff.x - camera->origin.x;
            const float dy = puff.y - camera->origin.y;
            const float dz = puff.z - camera->origin.z;
            puff.distance_squared = dx*dx + dy*dy + dz*dz;
            puffs.push_back(puff);
        }
    }
    if (puffs.empty()) return;
    // Alpha blending needs the farther puffs first, including between flares.
    std::sort(puffs.begin(), puffs.end(), [](const Puff& a, const Puff& b) {
        return a.distance_squared > b.distance_squared;
    });
    Shader::disable();
    GLint active_texture = GL_TEXTURE0_ARB;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    GLfloat modelview[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                 GL_CURRENT_BIT | GL_TEXTURE_BIT);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const auto& puff : puffs) {
        // Concentric rings approximate a soft Gaussian profile without a texture.
        const auto vertex = [&](float radius, float angle) {
            const float u = std::cos(angle) * radius * puff.radius;
            const float v = std::sin(angle) * radius * puff.radius;
            const float opacity = radius == 1.0f ? 0.0f :
                                  std::exp(-4.0f * radius * radius);
            glColor4f(0.95f, 0.94f, 0.92f, puff.alpha * opacity);
            glVertex3f(puff.x + modelview[0]*u + modelview[1]*v,
                       puff.y + modelview[4]*u + modelview[5]*v,
                       puff.z + modelview[8]*u + modelview[9]*v);
        };
        for (int ring = 0; ring < 4; ++ring) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int segment = 0; segment <= 16; ++segment) {
                const float angle = segment * (6.28318530718f / 16.0f);
                vertex(ring * 0.25f, angle);
                vertex((ring + 1) * 0.25f, angle);
            }
            glEnd();
        }
    }
    glActiveTextureARB(active_texture);
    glPopAttrib();
}

// Approximate short-range colored projectile light on nearby surfaces.
// Evaluated once per rendered frame; idle maps pay no per-surface projectile scans.
static bool sprintathon_has_active_lighting = false;
static std::vector<const monster_data *> sprintathon_burning_monsters;

// Visual projectile colors are learned from the same sprite image the renderer draws.
struct SprintathonProjectileVisual {
    int16 object_index;
    int16 projectile_type;
    float rgb[3];
};
static std::vector<SprintathonProjectileVisual> sprintathon_projectile_visuals;
static int16 sprintathon_visual_level = NONE;

static bool sprintathon_projectile_visual_color(const projectile_data& projectile,
                                                float rgb[3])
{
    const size_t index = &projectile - ProjectileList.data();
    if (index >= sprintathon_projectile_visuals.size() ||
        sprintathon_visual_level != dynamic_world->current_level_number) return false;
    const auto& visual = sprintathon_projectile_visuals[index];
    if (visual.object_index != projectile.object_index ||
        visual.projectile_type != projectile.type) return false;
    rgb[0] = visual.rgb[0]; rgb[1] = visual.rgb[1]; rgb[2] = visual.rgb[2];
    return true;
}


static void sprintathon_projectile_light_rgb(float x, float y, float z, float rgb[3])
{
    rgb[0] = rgb[1] = rgb[2] = 0.0f;
    // Off means no dynamic projectile tint, not a fallback to flat polygon light.
    // This helper is also used by sprites and the held weapon.
    if (!graphics_preferences->projectile_lights_per_pixel ||
        !sprintathon_has_active_lighting) return;
    for (const projectile_data& projectile : ProjectileList) {
        if (!SLOT_IS_USED(&projectile)) continue;
        float strength = 0.0f;
        float red = 1.0f, green = 1.0f, blue = 1.0f;
        switch (projectile.type) {
            case _projectile_minor_fusion_dispersal:
            case _projectile_fusion_bolt_minor:
                strength = 0.36f; red = 0.40f; green = 0.65f; blue = 1.0f; break;
            case _projectile_major_fusion_dispersal:
            case _projectile_fusion_bolt_major:
                strength = 0.55f; red = 0.50f; green = 0.45f; blue = 1.0f; break;
            case _projectile_compiler_bolt_minor:
            case _projectile_compiler_bolt_major:
                strength = 0.50f; red = 0.95f; green = 0.30f; blue = 0.65f; break;
            case _projectile_staff_bolt:
                strength = 0.48f; red = 0.45f; green = 1.0f; blue = 0.50f; break;
            case _projectile_alien_weapon:
                strength = 0.55f; red = 1.0f; green = 0.48f; blue = 0.16f; break;
            case _projectile_minor_defender:
            case _projectile_major_defender:
                strength = 0.48f; red = 0.40f; green = 0.80f; blue = 1.0f; break;
            case _projectile_minor_hummer:
            case _projectile_major_hummer:
            case _projectile_durandal_hummer:
                strength = 0.55f; red = 0.85f; green = 0.45f; blue = 1.0f; break;
            case _projectile_rocket:
            case _projectile_juggernaut_rocket:
            case _projectile_juggernaut_missile:
            case _projectile_flamethrower_burst:
                strength = 0.68f; red = 1.0f; green = 0.50f; blue = 0.16f; break;
            case _projectile_armageddon_sphere:
            case _projectile_overloaded_fusion_dispersal:
                strength = 0.68f; red = 0.70f; green = 0.65f; blue = 1.0f; break;
            default: continue;
        }
        float sampled[3];
        if (sprintathon_projectile_visual_color(projectile, sampled)) {
            red = sampled[0]; green = sampled[1]; blue = sampled[2];
        }
        const object_data *object = get_object_data(projectile.object_index);
        if (!object) continue;
        const float dx = (x - object->location.x) / WORLD_ONE;
        const float dy = (y - object->location.y) / WORLD_ONE;
        const float dz = (z - object->location.z) / WORLD_ONE;
        const float radius = 4.5f;
        const float falloff = std::max(0.0f, 1.0f - (dx*dx + dy*dy + dz*dz) / (radius*radius));
        const float amount = strength * falloff * falloff;
        rgb[0] += amount * red;
        rgb[1] += amount * green;
        rgb[2] += amount * blue;
    }
    // Explosions are effects, not projectiles: keep their light at the blast site.
    for (const effect_data& effect : EffectList) {
        if (!SLOT_IS_USED(&effect) || effect.delay > 0) continue;
        float radius = 0.0f, strength = 0.0f;
        switch (effect.type) {
            case _effect_rocket_explosion:
                radius = 9.0f; strength = 1.0f; break;
            case _effect_grenade_explosion:
                radius = 7.0f; strength = 0.82f; break;
            default: continue;
        }
        const object_data *object = get_object_data(effect.object_index);
        if (!object) continue;
        const float dx = (x - object->location.x) / WORLD_ONE;
        const float dy = (y - object->location.y) / WORLD_ONE;
        const float dz = (z - object->location.z) / WORLD_ONE;
        const float falloff = std::max(0.0f, 1.0f -
            (dx*dx + dy*dy + dz*dz) / (radius*radius));
        const float amount = strength * falloff * falloff;
        rgb[0] += amount;
        rgb[1] += amount * 0.46f;
        rgb[2] += amount * 0.13f;
    }

    // Flaming death animations are short-lived, moving orange light sources.
    for (const monster_data *monster : sprintathon_burning_monsters) {
        const object_data *object = get_object_data(monster->object_index);
        const float dx = (x-object->location.x)/WORLD_ONE;
        const float dy = (y-object->location.y)/WORLD_ONE;
        const float dz = (z-object->location.z-WORLD_ONE/2)/WORLD_ONE;
        const float falloff = std::max(0.0f, 1.0f-(dx*dx+dy*dy+dz*dz)/(7.0f*7.0f));
        const float amount = 1.3f*falloff*falloff;
        rgb[0] += amount;
        rgb[1] += amount*0.40f;
        rgb[2] += amount*0.09f;
    }

    const float gain = graphics_preferences->colored_light_intensity / 100.0f;
    rgb[0] *= gain; rgb[1] *= gain; rgb[2] *= gain;
}

// Light source discovery distance is independent of the light's actual radius.
static float sprintathon_light_render_radius(bool scenery)
{
    const int reach = scenery ? graphics_preferences->scenery_light_render_distance :
                                graphics_preferences->light_render_distance;
    return (16.0f + 0.8f * reach) * WORLD_ONE;
}

// Bounded world-space registry, refreshed by visible texture observations.
struct SprintathonTextureEmitter {
    float x, y, z, r, g, b;
    bool scenery;
    uint64_t last_seen;
    short light_index;
    float ambient_delta;
    float shade_gain;
    short scenery_object_index;
    short scenery_type;
    float scenery_object_z;
    bool liquid; // Distinguish liquid emission from ordinary bright textures.
    short liquid_type;
    float reflection_radius; // estimated from the rendered scenery frame
    bool baked;
    // Map-owned addresses remain stable for the lifetime of this level; never
    // use a transient render surface or a clipped position as an identity.
    const polygon_data* source_polygon;
    const side_texture_definition* source_wall_section;
    short source_sample; // floor=0, ceiling=1, media center=2, media banks=3+i
};
// Discovered during loading, independently of camera visibility. Emission is
// cached here; map-light intensity and moving geometry are read at runtime.
struct SprintathonMapEmitter {
    SprintathonTextureEmitter light;
    short room, edge;
    bool secondary;
    shape_descriptor shape;
};
static std::vector<SprintathonMapEmitter> sprintathon_map_emitters;

static bool sprintathon_update_map_emitter(SprintathonMapEmitter& entry)
{
    auto& out=entry.light;
    const auto* p=get_polygon_data(entry.room);
    out.source_polygon=p;
    if(entry.edge==NONE) {
        const bool ceiling=out.source_sample==1;
        if((ceiling?p->ceiling_texture:p->floor_texture)!=entry.shape)return false;
        out.z=ceiling?p->ceiling_height:p->floor_height;
        out.light_index=ceiling?p->ceiling_lightsource_index:p->floor_lightsource_index;
        out.ambient_delta=0;
    } else {
        if(entry.edge>=p->vertex_count || p->side_indexes[entry.edge]==NONE)return false;
        const auto* side=get_side_data(p->side_indexes[entry.edge]);
        const auto* section=entry.secondary?&side->secondary_texture:&side->primary_texture;
        if(section->texture!=entry.shape)return false;
        out.source_wall_section=section;
        out.light_index=entry.secondary?side->secondary_lightsource_index:side->primary_lightsource_index;
        out.ambient_delta=side->ambient_delta;
        float bottom=p->floor_height,top=p->ceiling_height;
        if(side->type!=_full_side) {
            const short adjacent=p->adjacent_polygon_indexes[entry.edge];
            if(adjacent==NONE)return false;
            const auto* n=get_polygon_data(adjacent);
            if(entry.secondary || side->type==_low_side)top=std::min(top,float(n->floor_height));
            else bottom=std::max(bottom,float(n->ceiling_height));
        }
        if(top<=bottom)return false;
        out.z=(bottom+top)*0.5f;
    }
    out.baked=SprintathonSkyBake::textureSourceBaked(p,entry.edge==NONE?
        out.source_sample:2+entry.edge,entry.shape,out.light_index);
    return true;
}

template<class Emission>
static void sprintathon_preload_map_emitters(Emission emission,void (*progress)())
{
    sprintathon_map_emitters.clear();
    struct Color {bool emits;float rgb[3];};
    std::map<shape_descriptor,Color> colors;
    for(short room=0;room<dynamic_world->polygon_count;++room) {
        const auto* p=get_polygon_data(room);
        if(p->vertex_count<3)continue;
        float cx=0,cy=0;
        for(short j=0;j<p->vertex_count;++j){const auto& v=get_endpoint_data(p->endpoint_indexes[j])->vertex;cx+=v.x;cy+=v.y;}
        cx/=p->vertex_count;cy/=p->vertex_count;
        auto add=[&](shape_descriptor shape,short mode,short edge,bool secondary,short sample,float x,float y) {
            // Animated texture frames and special transfer modes retain their
            // existing render-discovery path. Pulsating MAP lights are included.
            if(shape==UNONE || mode!=_xfer_normal || AnimTxtr_IsAnimated(shape))return;
            auto found=colors.find(shape);
            if(found==colors.end()){Color c{};c.emits=emission(shape,c.rgb);found=colors.emplace(shape,c).first;}
            if(!found->second.emits)return;
            SprintathonMapEmitter entry{};
            entry.room=room;entry.edge=edge;entry.secondary=secondary;entry.shape=shape;
            auto& light=entry.light;light.x=x;light.y=y;
            light.r=found->second.rgb[0]*0.8f;light.g=found->second.rgb[1]*0.8f;light.b=found->second.rgb[2]*0.8f;
            light.source_polygon=p;light.source_sample=sample;
            light.liquid_type=NONE;light.scenery_object_index=NONE;light.scenery_type=NONE;
            // Keep closed platform sections too; they can become exposed later.
            sprintathon_update_map_emitter(entry);
            sprintathon_map_emitters.push_back(entry);
        };
        add(p->floor_texture,p->floor_transfer_mode,NONE,false,0,cx,cy);
        add(p->ceiling_texture,p->ceiling_transfer_mode,NONE,false,1,cx,cy);
        for(short j=0;j<p->vertex_count;++j) {
            if(p->side_indexes[j]==NONE)continue;
            const auto* side=get_side_data(p->side_indexes[j]);
            const auto& a=get_endpoint_data(p->endpoint_indexes[j])->vertex;
            const auto& b=get_endpoint_data(p->endpoint_indexes[(j+1)%p->vertex_count])->vertex;
            const float x=(float(a.x)+b.x)*0.5f,y=(float(a.y)+b.y)*0.5f;
            add(side->primary_texture.texture,side->primary_transfer_mode,j,false,0,x,y);
            if(side->type==_split_side)add(side->secondary_texture.texture,side->secondary_transfer_mode,j,true,0,x,y);
        }
        if(progress)progress();
    }
}


// Trace-only counters: reset once per main scene frame.
struct SprintathonLightChangeTrace {
    unsigned observations=0, added=0, removed=0, recolored=0, brightened=0, activated=0, extinguished=0;
    unsigned allocations=0, uploads=0;
    size_t bytes=0;
    double refresh_ms=0, upload_ms=0;
};
static SprintathonLightChangeTrace sprintathon_light_change_trace;
struct SprintathonLightTraceScope {
    using Clock=std::chrono::steady_clock;
    double* total; Clock::time_point start;
    explicit SprintathonLightTraceScope(double& value):
        total(SprintathonStartupTiming::get().active()?&value:nullptr),
        start(total?Clock::now():Clock::time_point()) {}
    ~SprintathonLightTraceScope() {
        if(total)*total+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    }
};
static std::vector<SprintathonTextureEmitter> sprintathon_texture_lights;
static std::vector<SprintathonTextureEmitter> sprintathon_texture_lights_next;

// Optional uncapped list: one object identity per visible scenery fixture.
static std::vector<bool> sprintathon_visible_scenery;
static GLuint sprintathon_scenery_light_texture = 0;
static int sprintathon_scenery_light_texture_height = 0;
static int sprintathon_all_scenery_count = 0;
static std::vector<float> sprintathon_scenery_light_pixels;

static int sprintathon_world_light_budget() {
    const int quality=graphics_preferences->per_pixel_light_quality;
    return quality==0?20:(quality==1?8:4);
}

static bool sprintathon_all_scenery_enabled()
{
    return graphics_preferences->projectile_lights_per_pixel &&
           graphics_preferences->bright_scenery_lights &&
           graphics_preferences->all_visible_scenery_lights &&
           graphics_preferences->per_pixel_light_quality==0;
}

static bool sprintathon_is_forced_scenery(const SprintathonTextureEmitter& source)
{
    return sprintathon_all_scenery_enabled() && source.scenery &&
           source.scenery_object_index >= 0 &&
           size_t(source.scenery_object_index) < sprintathon_visible_scenery.size() &&
           sprintathon_visible_scenery[source.scenery_object_index];
}

static void sprintathon_release_scenery_light_texture()
{
    if (sprintathon_scenery_light_texture)
        glDeleteTextures(1, &sprintathon_scenery_light_texture);
    sprintathon_scenery_light_texture = 0;
    sprintathon_scenery_light_texture_height = 0;
    sprintathon_all_scenery_count = 0;
    sprintathon_scenery_light_pixels.clear();
}

// Keep a bounded pool for each source type. Nearby wall textures must not
// evict every scenery sprite before the view-wide light selection runs.
static void sprintathon_keep_emitter(std::vector<SprintathonTextureEmitter>& emitter_pool,
                                     const SprintathonTextureEmitter& candidate)
{
    if (candidate.scenery && sprintathon_all_scenery_enabled()) {
        emitter_pool.push_back(candidate);
        return;
    }
    size_t category_count = 0;
    size_t farthest = 0;
    float farthest_distance = -1.0f;
    for (size_t i = 0; i < emitter_pool.size(); ++i) {
        if (emitter_pool[i].scenery != candidate.scenery) continue;
        ++category_count;
        const float dx = emitter_pool[i].x - current_player->location.x;
        const float dy = emitter_pool[i].y - current_player->location.y;
        const float distance = dx*dx + dy*dy;
        if (distance > farthest_distance) {
            farthest_distance = distance;
            farthest = i;
        }
    }
    const size_t capacity = candidate.scenery ? 32 : 96;
    if (category_count < capacity) {
        emitter_pool.push_back(candidate);
    } else {
        const float dx = candidate.x - current_player->location.x;
        const float dy = candidate.y - current_player->location.y;
        if (dx*dx + dy*dy < farthest_distance * 0.90f)
            emitter_pool[farthest] = candidate;
    }
}

// Repeated portal draws of one scenery object are duplicates; adjacent objects
// are independent emitters even when their positions or light circles overlap.
static bool sprintathon_same_emitter(const SprintathonTextureEmitter& a,
                                    const SprintathonTextureEmitter& b)
{
    if (a.scenery != b.scenery) return false;
    if (a.scenery)
        return a.scenery_object_index != NONE &&
               a.scenery_object_index == b.scenery_object_index &&
               a.scenery_type == b.scenery_type;
    return a.source_polygon && a.source_polygon == b.source_polygon &&
           a.source_wall_section == b.source_wall_section &&
           a.source_sample == b.source_sample;
}

// Refresh hidden scenery from its actual map object, instead of expiring it
// because it did not appear in a render pass. Destroyed/reused slots are rejected.
static bool sprintathon_refresh_scenery_emitter(SprintathonTextureEmitter& source)
{
    if (!source.scenery) return true;
    if (source.scenery_object_index < 0 ||
        size_t(source.scenery_object_index) >= ObjectList.size()) return false;
    const auto& object = ObjectList[source.scenery_object_index];
    if (!SLOT_IS_USED(&object) || GET_OBJECT_OWNER(&object) != _object_is_scenery ||
        object.permutation != source.scenery_type) return false;
    source.x = object.location.x;
    source.y = object.location.y;
    source.z += float(object.location.z) - source.scenery_object_z;
    source.scenery_object_z = object.location.z;
    return true;
}

// Read the map's actual animated light, not the camera-shaded texture image.
// Keep the source registered even when dark so strobes do not churn light slots.
static float sprintathon_texture_light_shade(const SprintathonTextureEmitter& source)
{
    if (source.light_index == NONE) return 1.0f; // Scenery and emissive lava.
    if (source.light_index < 0 || size_t(source.light_index) >= LightList.size()) return 0.0f;
    const float intensity = (float(get_light_intensity(source.light_index)) +
                             source.ambient_delta) / float(FIXED_ONE - 1);
    const float shade = std::max(0.0f, std::min(1.0f, intensity));
    if (source.liquid && source.liquid_type != _media_lava) {
        // Only brightly shaded liquids emit. Fade in to avoid threshold pops;
        // evaluate the live map light every frame, including animated lights.
        const float t = std::max(0.0f, std::min(1.0f, (shade - 0.70f) / 0.25f));
        return shade * t * t * (3.0f - 2.0f * t);
    }
    return shade;
}

// Preserve known static texture sources before adding newly visible ones. Camera
// clipping must not expire a wall light or let draw order replace its position.
static void sprintathon_refresh_texture_lights()
{
    const bool trace=SprintathonStartupTiming::get().active();
    auto& stats=sprintathon_light_change_trace;
    SprintathonLightTraceScope timing(stats.refresh_ms);
    const size_t before=sprintathon_texture_lights.size();

    std::vector<SprintathonTextureEmitter> observations;
    observations.swap(sprintathon_texture_lights_next);
    if(graphics_preferences->bright_texture_lights) {
        const float radius=sprintathon_light_render_radius(false);
        for(auto& entry:sprintathon_map_emitters) {
            const float dx=entry.light.x-current_player->location.x;
            const float dy=entry.light.y-current_player->location.y;
            if(dx*dx+dy*dy>radius*radius || !sprintathon_update_map_emitter(entry))continue;
            // The geometry-owned observation takes precedence over portal draws.
            observations.erase(std::remove_if(observations.begin(),observations.end(),
                [&](const SprintathonTextureEmitter& other){return sprintathon_same_emitter(entry.light,other);}),observations.end());
            entry.light.last_seen=machine_tick_count();
            entry.light.shade_gain=sprintathon_texture_light_shade(entry.light);
            observations.push_back(entry.light);
        }
    }
    auto enabled = [](bool scenery) {
        return scenery ? graphics_preferences->bright_scenery_lights :
                         graphics_preferences->bright_texture_lights;
    };
    sprintathon_texture_lights.erase(std::remove_if(
        sprintathon_texture_lights.begin(), sprintathon_texture_lights.end(),
        [&](SprintathonTextureEmitter& source) {
            if (!sprintathon_refresh_scenery_emitter(source)) return true;
            const float dx = source.x - current_player->location.x;
            const float dy = source.y - current_player->location.y;
            const float radius = sprintathon_light_render_radius(source.scenery);
            return !enabled(source.scenery) ||
                   (!(source.scenery && sprintathon_all_scenery_enabled()) &&
                    dx*dx + dy*dy > radius*radius);
        }), sprintathon_texture_lights.end());
    if(trace){stats.removed+=unsigned(before-sprintathon_texture_lights.size());stats.observations+=unsigned(observations.size());}
    for (auto observation : observations) {
        if (!enabled(observation.scenery) ||
            !sprintathon_refresh_scenery_emitter(observation)) continue;
        const float dx = observation.x - current_player->location.x;
        const float dy = observation.y - current_player->location.y;
        const float radius = sprintathon_light_render_radius(observation.scenery);
        if (!(observation.scenery && sprintathon_all_scenery_enabled()) &&
            dx*dx + dy*dy > radius*radius) continue;
        size_t nearest = sprintathon_texture_lights.size();
        for (size_t i = 0; i < sprintathon_texture_lights.size(); ++i) {
            if (sprintathon_same_emitter(sprintathon_texture_lights[i], observation)) {
                nearest = i;
                break;
            }
        }
        if (nearest != sprintathon_texture_lights.size()) {
            auto& source = sprintathon_texture_lights[nearest];
            if(trace && (std::fabs(source.r-observation.r)>1.0f/255 ||
                std::fabs(source.g-observation.g)>1.0f/255 || std::fabs(source.b-observation.b)>1.0f/255))++stats.recolored;
            source.r = observation.r;
            source.g = observation.g;
            source.b = observation.b;
            source.last_seen = observation.last_seen;
            source.ambient_delta = observation.ambient_delta;
            source.light_index = observation.light_index;
            source.baked = observation.baked;
            source.liquid = observation.liquid;
            source.liquid_type = observation.liquid_type;
            source.x = observation.x;
            source.y = observation.y;
            source.z = observation.z;
            source.reflection_radius = observation.reflection_radius;
            if (source.scenery)
                source.scenery_object_z = observation.scenery_object_z;
        } else {
            if(trace)++stats.added; // New-source candidate, possibly replacing a capped entry.
            sprintathon_keep_emitter(sprintathon_texture_lights, observation);
        }
    }
    // Runs every rendered frame, including when the source surface is hidden.
    // The map simulation supplies pause/bullet-time timing and discrete changes.
    for (auto& source : sprintathon_texture_lights) {
        const float previous=source.shade_gain;
        source.shade_gain = sprintathon_texture_light_shade(source);
        if(trace) {
            if(source.shade_gain>previous+1.0f/255)++stats.brightened;
            if(previous<=0.001f && source.shade_gain>0.001f)++stats.activated;
            if(previous>0.001f && source.shade_gain<=0.001f)++stats.extinguished;
        }
    }
}

static void sprintathon_record_texture_light(TextureManager *texture,
                                             float x, float y, float z, RenderStep step, bool vertical, bool lava = false,
                                             short light_index = NONE, float ambient_delta = 0.0f,
                                             short scenery_object_index = NONE, short liquid_type = NONE, float reflection_radius = 0.0f, bool baked = false,
                                             const polygon_data* source_polygon = nullptr,
                                             const side_texture_definition* source_wall_section = nullptr,
                                             short source_sample = 0)
{
    if (sprintathon_fast_shaft_source()) return;
    if (lava) liquid_type = _media_lava;
    if (step != kDiffuse || !graphics_preferences->projectile_lights_per_pixel ||
        !(texture && (texture->TextureType == OGL_Txtr_Inhabitant ?
            graphics_preferences->bright_scenery_lights :
            graphics_preferences->bright_texture_lights)) ||
        !texture || (texture->TextureType != OGL_Txtr_Wall &&
                     texture->TextureType != OGL_Txtr_Inhabitant)) return;
    if (lava || texture->TextureType == OGL_Txtr_Inhabitant) light_index = NONE;
    float u, v, rgb[3];
    if (lava) {
        u = v = 0.5f;
        rgb[0] = 1.0f; rgb[1] = 0.38f; rgb[2] = 0.10f;
    } else if (liquid_type != NONE) {
        u = v = 0.5f;
        // Whole-texture colour; liquid emission is gated by map shading,
        // not by the bright-pixel fixture detector.
        if (!texture->GetProjectileVisualColor(rgb)) return;
    } else if (!texture->GetBrightEmission(u, v, rgb)) return;
    if (texture->TextureType == OGL_Txtr_Wall) {
        if (vertical) {
            z += (0.5f - v) * (0.75f * WORLD_ONE);
        } else {
            x += (u - 0.5f) * (0.6f * WORLD_ONE);
            y += (v - 0.5f) * (0.6f * WORLD_ONE);
        }
    }
    // Keep only nearby emitters and merge repeated clipping-window draws.
    const float dx = x - current_player->location.x;
    const float dy = y - current_player->location.y;
    const float discovery_radius = sprintathon_light_render_radius(
        texture->TextureType == OGL_Txtr_Inhabitant);
    if (!(texture->TextureType == OGL_Txtr_Inhabitant && sprintathon_all_scenery_enabled()) &&
        dx*dx + dy*dy > discovery_radius * discovery_radius) return;
    const bool scenery = texture->TextureType == OGL_Txtr_Inhabitant;
    // Missing geometry identity must not fall back to proximity matching.
    if (!scenery && !source_polygon) return;
    if (scenery && (scenery_object_index < 0 ||
        size_t(scenery_object_index) >= ObjectList.size())) return;
    SprintathonTextureEmitter light = {x, y, z,
        rgb[0] * 0.8f, rgb[1] * 0.8f, rgb[2] * 0.8f,
        scenery, machine_tick_count(), light_index, ambient_delta, 1.0f,
        scenery_object_index, static_cast<short>(scenery ? ObjectList[scenery_object_index].permutation : NONE),
        scenery ? float(ObjectList[scenery_object_index].location.z) : 0.0f,
        liquid_type != NONE, liquid_type, reflection_radius, baked,
        source_polygon, source_wall_section, source_sample};
    // Merge repeated portal draws of this exact map surface only.
    for (const auto& existing : sprintathon_texture_lights_next)
        if (sprintathon_same_emitter(existing, light)) return;
    sprintathon_keep_emitter(sprintathon_texture_lights_next, light);
}

// Reach is in world units; scenery defaults to a tighter circle.
static float sprintathon_emitter_radius(bool scenery)
{
    const int reach = scenery ? graphics_preferences->scenery_light_reach :
                                graphics_preferences->texture_light_reach;
    return (2.0f + 0.14f * reach) * WORLD_ONE;
}

static float sprintathon_emitter_gain(bool scenery)
{
    return (scenery ? graphics_preferences->scenery_light_intensity :
                      graphics_preferences->texture_light_intensity) / 100.0f;
}

// Texture records avoid the fixed uniform-slot limit. Only the opt-in path
// samples these records; the ordinary path keeps its compact light uniforms.
static void sprintathon_upload_all_scenery_lights()
{
    const bool trace=SprintathonStartupTiming::get().active();
    auto& stats=sprintathon_light_change_trace;
    SprintathonLightTraceScope timing(stats.upload_ms);
    sprintathon_all_scenery_count = 0;
    if (!sprintathon_all_scenery_enabled() ||
        graphics_preferences->scenery_light_intensity == 0) return;
    std::vector<float> pixels;
    const float gain = sprintathon_emitter_gain(true);
    const float inverse_radius = 1.0f / std::max(sprintathon_emitter_radius(true), 1.0f);
    for (const auto& source : sprintathon_texture_lights) {
        if (!sprintathon_is_forced_scenery(source)) continue;
        pixels.insert(pixels.end(), {source.x, source.y, source.z, inverse_radius,
            source.r * gain, source.g * gain, source.b * gain,
            SprintathonSkyBake::scenerySourceBaked(source.scenery_object_index) ?
                -1.0f-source.reflection_radius : source.reflection_radius});
        ++sprintathon_all_scenery_count;
    }
    if (pixels.empty()) return;
    // 128 records per row. Even a full 32767-object map fits in 256 rows.
    const int height = (sprintathon_all_scenery_count + 127) / 128;
    pixels.resize(size_t(height) * 256 * 4, 0.0f);
    GLint active_texture;
    glGetIntegerv(GL_ACTIVE_TEXTURE_ARB, &active_texture);
    glActiveTextureARB(GL_TEXTURE3_ARB);
    if (!sprintathon_scenery_light_texture) {
        glGenTextures(1, &sprintathon_scenery_light_texture);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, sprintathon_scenery_light_texture);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, sprintathon_scenery_light_texture);
    }
    if (height != sprintathon_scenery_light_texture_height) {
        if(trace){++stats.allocations;stats.bytes+=pixels.size()*sizeof(float);}
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA32F_ARB, 256, height, 0,
                     GL_RGBA, GL_FLOAT, pixels.data());
        sprintathon_scenery_light_texture_height = height;
    } else if (pixels != sprintathon_scenery_light_pixels) {
        if(trace){++stats.uploads;stats.bytes+=pixels.size()*sizeof(float);}
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, 256, height,
                        GL_RGBA, GL_FLOAT, pixels.data());
    }
    sprintathon_scenery_light_pixels.swap(pixels);
    glActiveTextureARB(active_texture);
}

// Match the floor pool's footprint and falloff at the held weapon's position.
static void sprintathon_floor_pool_weapon_tint(const view_data *camera, float rgb[3])
{
    if(!graphics_preferences->ceiling_fog_cones || !graphics_preferences->projectile_lights_per_pixel ||
       !graphics_preferences->ceiling_fog_cone_strength || camera->origin_polygon_index==NONE)return;
    const auto fog=OGL_GetCurrFogData();
    if(!TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_Fog) ||
       ((!fog || !fog->IsPresent) && !TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_ForceFog)))return;
    const auto* polygon=get_polygon_data(camera->origin_polygon_index);
    int count=0;
    for(const auto& cone:sprintathon_floor_cones) {
        if(count==4)break;
        if(cone.object_index<0 || size_t(cone.object_index)>=ObjectList.size())continue;
        const auto* object=&ObjectList[cone.object_index];
        if(!SLOT_IS_USED(object) || GET_OBJECT_OWNER(object)!=_object_is_scenery ||
           object->polygon!=camera->origin_polygon_index)continue;
        const float height=cone.z-polygon->floor_height;
        if(height<=0 || height>5.0f*WORLD_ONE)continue;
        const float radius=2.0f*std::min(cone.radius,WORLD_ONE*0.04f+height*0.28f);
        if(radius<1)continue;
        ++count;
        // Do not tint a camera above the fixture or below this floor.
        if(camera->origin.z>cone.z || camera->origin.z<polygon->floor_height)continue;
        const float dx=camera->origin.x-cone.x,dy=camera->origin.y-cone.y;
        const float t=std::min(1.0f,std::sqrt(dx*dx+dy*dy)/radius);
        const float falloff=1.0f-t*t*(3.0f-2.0f*t);
        const float gain=0.65f*(graphics_preferences->ceiling_fog_cone_strength/100.0f)/
            (1.0f+0.12f*(height/WORLD_ONE)*(height/WORLD_ONE));
        rgb[0]+=cone.red*gain*falloff;
        rgb[1]+=cone.green*gain*falloff;
        rgb[2]+=cone.blue*gain*falloff;
    }
}

static void sprintathon_emitter_plane(const SprintathonTextureEmitter* light,float plane[4]);

// Calculate one colored tint per sprite instead of looping over lights per pixel.
static void sprintathon_set_sprite_light(Shader *shader, const rectangle_definition& rect,
                                        short type, const view_data *camera)
{
    if (!(type == OGL_Txtr_WeaponsInHand && graphics_preferences->ceiling_fog_cones &&
          !sprintathon_floor_cones.empty()) &&
        !sprintathon_has_active_lighting && sprintathon_dropped_flares.empty() &&
        (!(graphics_preferences->bright_texture_lights ||
           graphics_preferences->bright_scenery_lights) || sprintathon_texture_lights.empty())) {
        shader->setVector4(Shader::U_SprintathonLightColor, 0, 0, 0, 0);
        return;
    }
    const bool held_weapon = type == OGL_Txtr_WeaponsInHand;
    const float x = held_weapon ? camera->origin.x : rect.Position.x;
    const float y = held_weapon ? camera->origin.y : rect.Position.y;
    const float z = held_weapon ? camera->origin.z :
        rect.Position.z + 0.5f * (rect.WorldBottom + rect.WorldTop) * rect.Scale;
    float rgb[3] = {0.0f, 0.0f, 0.0f};
    sprintathon_projectile_light_rgb(x, y, z, rgb);
    if (held_weapon) sprintathon_floor_pool_weapon_tint(camera, rgb);
    if (graphics_preferences->projectile_lights_per_pixel) {
        const float gain = graphics_preferences->colored_light_intensity / 100.0f;
        for (const auto& flare : sprintathon_dropped_flares) {
            const float dx = x - flare.x, dy = y - flare.y, dz = z - flare.z;
            const float radius = 5.5f * WORLD_ONE;
            const float distance = (dx*dx + dy*dy + dz*dz) / (radius*radius);
            if (distance >= 1.0f) continue;
            const float strength = sprintathon_flare_strength(flare) * gain *
                (1.0f - distance) * (1.0f - distance);
            rgb[0] += strength; rgb[1] += strength * 0.12f; rgb[2] += strength * 0.04f;
        }
    }
    if (graphics_preferences->projectile_lights_per_pixel &&
        (graphics_preferences->bright_texture_lights ||
         graphics_preferences->bright_scenery_lights)) {
        // CPU-only lighting: keep the strongest contributions, not merely the
        // nearest fixture. A weak source must not hide a bright one behind it.
        struct Contribution { float score=0,r=0,g=0,b=0; } best[4];
        for (const auto& light : sprintathon_texture_lights) {
            if(!(light.scenery?graphics_preferences->bright_scenery_lights:
                graphics_preferences->bright_texture_lights))continue;
            if (light.shade_gain <= 0.0f || sprintathon_emitter_gain(light.scenery) <= 0.0f) continue;
            const float dx=x-light.x,dy=y-light.y,dz=z-light.z;
            const float radius=std::max(sprintathon_emitter_radius(light.scenery),1.0f);
            const float distance=(dx*dx+dy*dy+dz*dz)/(radius*radius);
            if(distance>=1.0f)continue;
            float plane[4];sprintathon_emitter_plane(&light,plane);
            // Unrestricted sources use zero normal and must not be feathered.
            float facing=1.0f;
            if(plane[0]!=0 || plane[1]!=0 || plane[2]!=0){
                const float t=std::max(0.0f,std::min(1.0f,
                    (x*plane[0]+y*plane[1]+z*plane[2]+plane[3]+32.0f)/128.0f));
                facing=t*t*(3.0f-2.0f*t);
            }
            const float falloff=1.0f-distance;
            const float gain=falloff*falloff/(1.0f+16.0f*distance)*facing*
                sprintathon_emitter_gain(light.scenery)*light.shade_gain*
                (light.liquid && light.liquid_type!=_media_lava?
                    graphics_preferences->liquid_fluorescence/100.0f:1.0f);
            Contribution c;c.r=light.r*gain;c.g=light.g*gain;c.b=light.b*gain;
            c.score=std::max(c.r,std::max(c.g,c.b));
            for(int i=0;i<4;++i)if(c.score>best[i].score){
                for(int j=3;j>i;--j)best[j]=best[j-1];
                best[i]=c;break;
            }
        }
        for(const auto& c:best){rgb[0]+=c.r;rgb[1]+=c.g;rgb[2]+=c.b;}
    }
    shader->setVector4(Shader::U_SprintathonLightColor,
                       rgb[0], rgb[1], rgb[2], 1.0f);
}

// One nearby projectile per surface bounds the shader cost and smooths the glow per pixel.
static Shader *sprintathon_surface_shader(RenderStep step, short texture_type)
{
    // Dedicated source shaders have no surface-light uniforms.
    if (sprintathon_fast_shaft_source()) return nullptr;
    if (texture_type != OGL_Txtr_Wall || current_player->infravision_duration)
        return nullptr;
    const bool bump = TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap) &&
        !sprintathon_fast_shaft_source();
    return Shader::get(bump ? (step == kGlow ? Shader::S_BumpBloom : Shader::S_Bump)
                            : (step == kGlow ? Shader::S_WallBloom : Shader::S_Wall));
}

// Keep twenty shader slots stable. A previous emitter gets a modest priority
// while it remains near the best candidate, and its brightness fades when lost.
static constexpr int sprintathon_emitter_slots = 20;
static SprintathonTextureEmitter sprintathon_previous_emitters[sprintathon_emitter_slots];
static bool sprintathon_previous_valid[sprintathon_emitter_slots] = {};
static float sprintathon_emitter_fade[sprintathon_emitter_slots] = {};
static int16 sprintathon_previous_level = NONE;
static SprintathonTextureEmitter sprintathon_view_emitters[sprintathon_emitter_slots];
static bool sprintathon_view_valid[sprintathon_emitter_slots] = {};
static uint32 sprintathon_emitter_tick = 0;

static void sprintathon_select_view_emitters(const view_data *camera)
{
    const uint32 now = machine_tick_count();
    const float elapsed = sprintathon_emitter_tick ?
        std::min<uint32>(now - sprintathon_emitter_tick, 250) : 16;
    sprintathon_emitter_tick = now;
    const float fade_step = 1.0f - std::exp(-elapsed / 160.0f);
    const bool reset = sprintathon_previous_level != dynamic_world->current_level_number ||
        !graphics_preferences->projectile_lights_per_pixel ||
        (graphics_preferences->texture_light_intensity == 0 &&
         graphics_preferences->scenery_light_intensity == 0);
    if (reset) {
        for (int i = 0; i < sprintathon_emitter_slots; ++i) {
            sprintathon_previous_valid[i] = false;
            sprintathon_emitter_fade[i] = 0;
        }
        sprintathon_previous_level = dynamic_world->current_level_number;
    }
    for (int i = 0; i < sprintathon_emitter_slots; ++i)
        sprintathon_view_valid[i] = false;
    if (!graphics_preferences->projectile_lights_per_pixel ||
        (graphics_preferences->texture_light_intensity == 0 &&
         graphics_preferences->scenery_light_intensity == 0)) return;

    struct Candidate { const SprintathonTextureEmitter *source; float score; };
    std::vector<Candidate> candidates;
    for (const auto& emitter : sprintathon_texture_lights) {
        if (sprintathon_is_forced_scenery(emitter) ||
            sprintathon_emitter_gain(emitter.scenery) <= 0.0f) continue;
        if ((emitter.scenery ? graphics_preferences->scenery_light_limit :
                                graphics_preferences->texture_light_limit) == 0) continue;
        const float dx = emitter.x-camera->origin.x;
        const float dy = emitter.y-camera->origin.y;
        const float dz = emitter.z-camera->origin.z;
        const float distance = dx*dx+dy*dy+dz*dz;
        const float radius = sprintathon_light_render_radius(emitter.scenery);
        if (distance <= radius*radius) candidates.push_back({&emitter, distance});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.source->x != b.source->x) return a.source->x < b.source->x;
        if (a.source->y != b.source->y) return a.source->y < b.source->y;
        return a.source->z < b.source->z;
    });
    auto matches = [](const SprintathonTextureEmitter& a, const SprintathonTextureEmitter& b) {
        return sprintathon_same_emitter(a, b);
    };
    auto eligible = [&](const SprintathonTextureEmitter& source, int scenery_count, int texture_count) {
        if (source.scenery ? scenery_count >= graphics_preferences->scenery_light_limit :
                             texture_count >= graphics_preferences->texture_light_limit) return false;
        for (int j=0; j<sprintathon_emitter_slots; ++j) {
            if (!sprintathon_view_valid[j]) continue;
            const auto& selected = sprintathon_view_emitters[j];
            if (sprintathon_same_emitter(source, selected)) return false;
        }
        return true;
    };
    const int texture_slots = sprintathon_dropped_flares.empty() ?
        sprintathon_emitter_slots : sprintathon_emitter_slots - sprintathon_flare_capacity;
    int scenery_count=0, texture_count=0;
    // Retain a slot only when its old source is still competitive. This
    // prevents two almost-equidistant emitters swapping every frame.
    for (int i=0; i<texture_slots; ++i) {
        if (!sprintathon_previous_valid[i]) continue;
        size_t rank = 0;
        for (const auto& candidate : candidates) {
            // Keep an old light if it still ranks near the available slots.
            if (rank++ >= 2 * sprintathon_emitter_slots) break;
            if (!matches(*candidate.source,sprintathon_previous_emitters[i])) continue;
            if (eligible(*candidate.source, scenery_count, texture_count)) {
                sprintathon_view_emitters[i]=*candidate.source;
                sprintathon_view_valid[i]=true;
                if (candidate.source->scenery) ++scenery_count; else ++texture_count;
            }
            break;
        }
    }
    for (const auto& candidate : candidates) {
        if (!eligible(*candidate.source, scenery_count, texture_count)) continue;
        int slot=0;
        while (slot<texture_slots && sprintathon_view_valid[slot]) ++slot;
        if (slot==texture_slots) break;
        sprintathon_view_emitters[slot]=*candidate.source;
        sprintathon_view_valid[slot]=true;
        if (candidate.source->scenery) ++scenery_count; else ++texture_count;
    }
    for (int i=0; i<sprintathon_emitter_slots; ++i) {
        if (sprintathon_view_valid[i]) {
            if (!sprintathon_previous_valid[i] ||
                !matches(sprintathon_previous_emitters[i], sprintathon_view_emitters[i]))
                sprintathon_emitter_fade[i]=0;
            sprintathon_emitter_fade[i] += (1.0f-sprintathon_emitter_fade[i])*fade_step;
            sprintathon_previous_emitters[i]=sprintathon_view_emitters[i];
            sprintathon_previous_valid[i]=true;
        } else if (sprintathon_previous_valid[i]) {
            sprintathon_emitter_fade[i] *= (1.0f-fade_step);
            if (sprintathon_emitter_fade[i]<0.01f)
                sprintathon_previous_valid[i]=false;
        }
    }
}

// Light-position .w stores inverse radius, shared by both surface shaders.
struct SprintathonSurfaceLightBounds {
    float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    bool valid = false;
};


// CPU broad phase only. Leaves contain the original registry indices; exact
// distance checks, baked-source exclusion, ranking and slot limits stay below.
struct SprintathonLightIndex {
    struct Box {double lo[3],hi[3];};
    struct Node {Box box;size_t first=0,last=0;int left=-1,right=-1;};
    std::vector<Box> boxes;
    std::vector<size_t> order, result;
    std::vector<Node> nodes;
    bool ready=false;
    unsigned queries=0;
    uint64_t tested=0, full=0;
    double buildMs=0;
    static SprintathonLightIndex& get(){static SprintathonLightIndex i;return i;}
    static bool enabled(){static const bool value=[](){const char* v=std::getenv("SPRINTATHON_LIGHT_INDEX");return v && v[0]=='1';}();return value;}
    void reset(){ready=false;queries=0;tested=full=0;buildMs=0;}
    int build(size_t first,size_t last){
        Node n;n.first=first;n.last=last;n.box=boxes[order[first]];
        for(size_t i=first+1;i<last;++i)for(int a=0;a<3;++a){
            n.box.lo[a]=std::min(n.box.lo[a],boxes[order[i]].lo[a]);
            n.box.hi[a]=std::max(n.box.hi[a],boxes[order[i]].hi[a]);
        }
        const int index=int(nodes.size());nodes.push_back(n);
        if(last-first>8){
            int axis=0;for(int a=1;a<3;++a)if(n.box.hi[a]-n.box.lo[a]>n.box.hi[axis]-n.box.lo[axis])axis=a;
            size_t mid=first+(last-first)/2;
            std::nth_element(order.begin()+first,order.begin()+mid,order.begin()+last,[&](size_t a,size_t b){
                double x=boxes[a].lo[axis]+boxes[a].hi[axis],y=boxes[b].lo[axis]+boxes[b].hi[axis];
                return x!=y?x<y:a<b;
            });
            int left=build(first,mid),right=build(mid,last);
            nodes[index].left=left;nodes[index].right=right;
        }
        return index;
    }
    static bool overlaps(const Box& a,const Box& b){
        for(int i=0;i<3;++i)if(a.hi[i]<b.lo[i] || a.lo[i]>b.hi[i])return false;
        return true;
    }
    void visit(int index,const Box& receiver){
        const auto& n=nodes[index];if(!overlaps(n.box,receiver))return;
        if(n.left>=0){visit(n.left,receiver);visit(n.right,receiver);return;}
        for(size_t i=n.first;i<n.last;++i)if(overlaps(boxes[order[i]],receiver))result.push_back(order[i]);
    }
    const std::vector<size_t>& query(const SprintathonSurfaceLightBounds& bounds){
        if(!ready){
            const auto start=std::chrono::steady_clock::now();
            boxes.clear();order.clear();nodes.clear();
            for(size_t i=0;i<sprintathon_texture_lights.size();++i){
                const auto& l=sprintathon_texture_lights[i];
                const double radius=double(std::max(sprintathon_emitter_radius(l.scenery),1.0f))+1.0;
                Box b={{l.x-radius,l.y-radius,l.z-radius},{l.x+radius,l.y+radius,l.z+radius}};
                boxes.push_back(b);order.push_back(i);
            }
            if(!order.empty())build(0,order.size());
            ready=true;
            buildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
        result.clear();
        const Box receiver={{bounds.x0,bounds.y0,bounds.z0},{bounds.x1,bounds.y1,bounds.z1}};
        if(!nodes.empty())visit(0,receiver);
        ++queries;tested+=result.size();full+=boxes.size();
        return result;
    }
};
struct SprintathonLightIndexFrame {
    SprintathonLightIndexFrame(){SprintathonLightIndex::get().reset();}
    ~SprintathonLightIndexFrame(){
        if(SprintathonLightIndex::enabled() && sprintathon_detail_frame()){
            const auto& t=SprintathonLightIndex::get();
            std::fprintf(stderr,"Light index frame %llu: queries %u candidates %llu full-scan %llu build %.3f ms\n",
                (unsigned long long)SprintathonStartupTiming::get().frame_id,t.queries,
                (unsigned long long)t.tested,(unsigned long long)t.full,t.buildMs);
        }
    }
};

static bool sprintathon_light_reaches_surface(const SprintathonSurfaceLightBounds *bounds,
                                             float x, float y, float z, float radius)
{
    if (!bounds || !bounds->valid) return true;
    // Distance to the entire surface bounds, never its centroid. A large floor
    // can be lit at its edge even when its center is far beyond the light range.
    const double dx = std::max(0.0, std::max(double(bounds->x0) - x, double(x) - bounds->x1));
    const double dy = std::max(0.0, std::max(double(bounds->y0) - y, double(y) - bounds->y1));
    const double dz = std::max(0.0, std::max(double(bounds->z0) - z, double(z) - bounds->z1));
    // Roundoff guard: retain touching lights, including float shader boundary cases.
    const double conservative_radius = double(radius) + 1.0;
    return !(dx*dx + dy*dy + dz*dz > conservative_radius*conservative_radius);
}

// Keep a contiguous range in the original accumulation order. Only remove
// leading/trailing lights whose spheres cannot touch any part of the surface.
// Interior non-contributors remain harmless; no light cap or centroid test.
static void sprintathon_scenery_surface_range(const SprintathonSurfaceLightBounds *bounds,
                                              int& first, int& end)
{
    first = 0;
    end = sprintathon_all_scenery_count;
    static const bool enabled = [] {
        const char *setting = std::getenv("SPRINTATHON_SCENERY_SURFACE_RANGE");
        return !setting || setting[0] != '0';
    }();
    if (!enabled || !bounds || !bounds->valid || end <= 0) return;
    if (sprintathon_scenery_light_pixels.size() < size_t(end) * 8) return;
    auto reaches = [&](int index) {
        const float *record = &sprintathon_scenery_light_pixels[size_t(index) * 8];
        if (!(record[3] > 0.0f)) return true;
        return sprintathon_light_reaches_surface(bounds,
            record[0], record[1], record[2], 1.0f / record[3]);
    };
    while (first < end && !reaches(first)) ++first;
    while (end > first && !reaches(end - 1)) --end;
}

// Select two all-visible scenery sources on the CPU, using distance to
// the complete liquid bounds so a large pool can receive light at its edge.
static void sprintathon_set_water_scenery(Shader* shader,
    const SprintathonSurfaceLightBounds& bounds)
{
    if(!shader || !bounds.valid || !sprintathon_all_scenery_enabled())return;
    // Reflection reach is independent of the diffuse-light bounds trim.
    int first=0,end=sprintathon_all_scenery_count;
    if(sprintathon_scenery_light_pixels.size()<size_t(end)*8)return;
    int best[2]={-1,-1};float score[2]={0,0};
    for(int i=first;i<end;++i) {
        const float* p=&sprintathon_scenery_light_pixels[size_t(i)*8];
        if(p[3]<=0 || p[2]<=bounds.z1)continue;
        float dx=std::max(bounds.x0,std::min(p[0],bounds.x1))-p[0];
        float dy=std::max(bounds.y0,std::min(p[1],bounds.y1))-p[1];
        float dz=bounds.z1-p[2];
        float r2=(dx*dx+dy*dy+dz*dz)*p[3]*p[3]/9.0f;
        if(r2>=1)continue;
        float value=std::max(p[4],std::max(p[5],p[6]))*(1-r2)*(1-r2)/(1+2*r2);
        if(value>score[0]){best[1]=best[0];score[1]=score[0];best[0]=i;score[0]=value;}
        else if(value>score[1]){best[1]=i;score[1]=value;}
    }
    const Shader::UniformName positions[2]={Shader::U_WaterSceneryPosition0,Shader::U_WaterSceneryPosition1};
    const Shader::UniformName colours[2]={Shader::U_WaterSceneryColor0,Shader::U_WaterSceneryColor1};
    for(int i=0;i<2;++i)if(best[i]>=0) {
        const float* p=&sprintathon_scenery_light_pixels[size_t(best[i])*8];
        shader->setVector4(positions[i],p[0],p[1],p[2],p[3]/3.0f);
        shader->setVector4(colours[i],p[4],p[5],p[6],-std::max(p[7]<0 ? -p[7]-1.0f : p[7],32.0f));
    }
}

// Plane points into the emitter's owning polygon. Zero normal/+96 offset
// means unrestricted emission (scenery, liquids, flares, missing geometry).
static void sprintathon_emitter_plane(const SprintathonTextureEmitter* light,float plane[4])
{
    plane[0]=plane[1]=plane[2]=0;plane[3]=96;
    if(!light || light->scenery || light->liquid || !light->source_polygon)return;
    const auto* p=light->source_polygon;
    if(!light->source_wall_section) {
        plane[2]=light->source_sample==1?-1.0f:1.0f;
        plane[3]=-plane[2]*light->z;
        return;
    }
    for(short edge=0;edge<p->vertex_count;++edge) {
        if(p->side_indexes[edge]==NONE)continue;
        const auto* side=get_side_data(p->side_indexes[edge]);
        if(light->source_wall_section!=&side->primary_texture &&
           light->source_wall_section!=&side->secondary_texture &&
           light->source_wall_section!=&side->transparent_texture)continue;
        const auto& a=get_endpoint_data(p->endpoint_indexes[edge])->vertex;
        const auto& b=get_endpoint_data(p->endpoint_indexes[(edge+1)%p->vertex_count])->vertex;
        float nx=-(float(b.y)-a.y),ny=float(b.x)-a.x;
        const float length=std::sqrt(nx*nx+ny*ny);
        if(length<1.0f)return;
        nx/=length;ny/=length;
        double cx=0,cy=0;
        for(short i=0;i<p->vertex_count;++i){const auto& v=get_endpoint_data(p->endpoint_indexes[i])->vertex;cx+=v.x;cy+=v.y;}
        cx/=p->vertex_count;cy/=p->vertex_count;
        if(nx*(cx-a.x)+ny*(cy-a.y)<0){nx=-nx;ny=-ny;}
        plane[0]=nx;plane[1]=ny;plane[3]=-(nx*a.x+ny*a.y);
        return;
    }
}

template<class LightSink>
static void sprintathon_write_view_emitters(LightSink *shader,
    const SprintathonSurfaceLightBounds *bounds, bool surface_selection, bool baked_receiver)
{
    SprintathonSetupScope setupTime(SprintathonSetupTrace::Emitters);
    const Shader::UniformName positions[sprintathon_emitter_slots] = {
        Shader::U_SprintathonLightPosition2,
        Shader::U_SprintathonLightPosition3,
        Shader::U_SprintathonLightPosition4,
        Shader::U_SprintathonLightPosition5,
        Shader::U_SprintathonLightPosition6,
        Shader::U_SprintathonLightPosition7,
        Shader::U_SprintathonLightPosition8,
        Shader::U_SprintathonLightPosition9,
        Shader::U_SprintathonLightPosition10,
        Shader::U_SprintathonLightPosition11,
        Shader::U_SprintathonLightPosition12,
        Shader::U_SprintathonLightPosition13,
        Shader::U_SprintathonLightPosition14,
        Shader::U_SprintathonLightPosition15,
        Shader::U_SprintathonLightPosition16,
        Shader::U_SprintathonLightPosition17,
        Shader::U_SprintathonLightPosition18,
        Shader::U_SprintathonLightPosition19,
        Shader::U_SprintathonLightPosition20,
        Shader::U_SprintathonLightPosition21
    };
    const Shader::UniformName colors[sprintathon_emitter_slots] = {
        Shader::U_SprintathonLightColor2,
        Shader::U_SprintathonLightColor3,
        Shader::U_SprintathonLightColor4,
        Shader::U_SprintathonLightColor5,
        Shader::U_SprintathonLightColor6,
        Shader::U_SprintathonLightColor7,
        Shader::U_SprintathonLightColor8,
        Shader::U_SprintathonLightColor9,
        Shader::U_SprintathonLightColor10,
        Shader::U_SprintathonLightColor11,
        Shader::U_SprintathonLightColor12,
        Shader::U_SprintathonLightColor13,
        Shader::U_SprintathonLightColor14,
        Shader::U_SprintathonLightColor15,
        Shader::U_SprintathonLightColor16,
        Shader::U_SprintathonLightColor17,
        Shader::U_SprintathonLightColor18,
        Shader::U_SprintathonLightColor19,
        Shader::U_SprintathonLightColor20,
        Shader::U_SprintathonLightColor21
    };
    int scenery_first, scenery_end;
    sprintathon_scenery_surface_range(bounds, scenery_first, scenery_end);
    shader->setFloat(Shader::U_SprintathonSceneryFirst, float(scenery_first));
    shader->setFloat(Shader::U_SprintathonAllSceneryCount, float(scenery_end));
    static const bool compact_requested = [] {
        const char *setting = std::getenv("SPRINTATHON_COMPACT_SURFACE_LIGHTS");
        return !setting || setting[0] != '0';
    }();
    const int light_budget=sprintathon_world_light_budget();
    const bool compact=compact_requested || light_budget<sprintathon_emitter_slots;
    // Experimental opt-out for comparing the original steady liquid light.
    static const bool liquid_caustics = []() {
        const char *setting = std::getenv("SPRINTATHON_LIQUID_CAUSTICS");
        return !setting || setting[0] != '0';
    }();
    // Apply caustic modulation only to vertical receivers. Horizontal surfaces
    // retain the ordinary steady liquid light. Share the actual lava ripple
    // clock and speed setting, including its bullet-time rate.
    const bool vertical_receiver = bounds && bounds->valid && bounds->z1 > bounds->z0;
    // Independent steady fluorescence and additive wall caustic illumination.
    shader->setVector4(Shader::U_SprintathonLiquidLightSettings,
        graphics_preferences->liquid_fluorescence / 100.0f,
        liquid_caustics && graphics_preferences->liquid_caustics && vertical_receiver ?
            graphics_preferences->liquid_caustic_strength / 100.0f : 0.0f, 0, 0);
    // Each source uses its own liquid ripple speed.
    const float gain = graphics_preferences->colored_light_intensity / 100.0f;
    int active_count = 0;
    bool uploaded[sprintathon_emitter_slots] = {};
    static const Shader::UniformName planes[sprintathon_emitter_slots] = {
        Shader::U_SprintathonLightPlane2,
        Shader::U_SprintathonLightPlane3,
        Shader::U_SprintathonLightPlane4,
        Shader::U_SprintathonLightPlane5,
        Shader::U_SprintathonLightPlane6,
        Shader::U_SprintathonLightPlane7,
        Shader::U_SprintathonLightPlane8,
        Shader::U_SprintathonLightPlane9,
        Shader::U_SprintathonLightPlane10,
        Shader::U_SprintathonLightPlane11,
        Shader::U_SprintathonLightPlane12,
        Shader::U_SprintathonLightPlane13,
        Shader::U_SprintathonLightPlane14,
        Shader::U_SprintathonLightPlane15,
        Shader::U_SprintathonLightPlane16,
        Shader::U_SprintathonLightPlane17,
        Shader::U_SprintathonLightPlane18,
        Shader::U_SprintathonLightPlane19,
        Shader::U_SprintathonLightPlane20,
        Shader::U_SprintathonLightPlane21
    };
    auto upload = [&](int original_slot, float x, float y, float z, float radius,
                      float r, float g, float b, short liquid_type, const SprintathonTextureEmitter* source) {
        if (active_count>=light_budget || (r <= 0.0f && g <= 0.0f && b <= 0.0f) ||
            !sprintathon_light_reaches_surface(bounds, x, y, z, radius)) return;
        const int slot = compact ? active_count : original_slot;
        uploaded[slot] = true;
        float plane[4];sprintathon_emitter_plane(source,plane);
        shader->setVector4(planes[slot],plane[0],plane[1],plane[2],plane[3]);
        shader->setVector4(positions[slot], x, y, z, 1.0f / radius);
        shader->setVector4(colors[slot], r, g, b,
            liquid_type != NONE ? (liquid_type == _media_lava ? 10.0f : 2.0f) +
                sprintathon_liquid_ripple_phase(liquid_type, 0.5) : 1.0f);
        ++active_count;
    };

    if (surface_selection) {
        // Rank the registry against the RECEIVER, not the camera's short list.
        // Exclude baked contributors before spending any dynamic-light slots.
        struct Candidate { const SprintathonTextureEmitter* light; double score; };
        static std::vector<Candidate> candidates;
        candidates.clear();
        candidates.reserve(sprintathon_texture_lights.size());

        const std::vector<size_t>* nearby=SprintathonLightIndex::enabled() ?
            &SprintathonLightIndex::get().query(*bounds):nullptr;
        const size_t count=nearby?nearby->size():sprintathon_texture_lights.size();
        for (size_t candidateIndex=0;candidateIndex<count;++candidateIndex) {
            const auto& light=sprintathon_texture_lights[nearby?(*nearby)[candidateIndex]:candidateIndex];
            if(sprintathon_is_forced_scenery(light) || sprintathon_emitter_gain(light.scenery)<=0.0f)continue;
            if(baked_receiver && (light.baked || (light.scenery &&
                SprintathonSkyBake::scenerySourceBaked(light.scenery_object_index))))continue;
            if((light.scenery?graphics_preferences->scenery_light_limit:graphics_preferences->texture_light_limit)<=0)continue;
            const float radius=std::max(sprintathon_emitter_radius(light.scenery),1.0f);
            if(!sprintathon_light_reaches_surface(bounds,light.x,light.y,light.z,radius))continue;
            const double dx=std::max(0.0,std::max(double(bounds->x0)-light.x,double(light.x)-bounds->x1));
            const double dy=std::max(0.0,std::max(double(bounds->y0)-light.y,double(light.y)-bounds->y1));
            const double dz=std::max(0.0,std::max(double(bounds->z0)-light.z,double(light.z)-bounds->z1));
            // Do not rank by instantaneous brightness: a flicker's dark phase
            // must not churn its slot or restart a camera-selection fade.
            const double distance2=(dx*dx+dy*dy+dz*dz)/(double(radius)*radius);
            const double falloff=std::max(0.0,1.0-distance2);
            const double contribution=std::max(light.r,std::max(light.g,light.b))*
                sprintathon_emitter_gain(light.scenery)*falloff*falloff/(1.0+16.0*distance2);
            candidates.push_back({&light,light_budget<20?-contribution:distance2});
        }
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
            if(a.score!=b.score)return a.score<b.score;
            const auto& x=*a.light;const auto& y=*b.light;
            if(x.x!=y.x)return x.x<y.x;
            if(x.y!=y.y)return x.y<y.y;
            if(x.z!=y.z)return x.z<y.z;
            // All candidates belong to the same vector: this is a stable tie
            // break while its source population remains unchanged.
            return a.light<b.light;
        });
        const int capacity=light_budget-int(sprintathon_dropped_flares.size());
        int selected=0,texture_count=0,scenery_count=0;
        for(const auto& candidate:candidates) {
            const auto& light=*candidate.light;
            int& count=light.scenery?scenery_count:texture_count;
            const int limit=light.scenery?graphics_preferences->scenery_light_limit:graphics_preferences->texture_light_limit;
            if(count>=limit)continue;
            if(selected>=capacity)break;
            ++count;
            const float strength=sprintathon_emitter_gain(light.scenery)*light.shade_gain;
            upload(selected++,light.x,light.y,light.z,std::max(sprintathon_emitter_radius(light.scenery),1.0f),
                light.r*strength,light.g*strength,light.b*strength,light.liquid_type,&light);
        }
    } else {
    for (int i = 0; i < sprintathon_emitter_slots; ++i) {
        if (!sprintathon_previous_valid[i] ||
            (!sprintathon_dropped_flares.empty() &&
             i >= sprintathon_emitter_slots - sprintathon_flare_capacity)) continue;
        if(active_count>=light_budget-int(sprintathon_dropped_flares.size()))break;
        const auto& light = sprintathon_previous_emitters[i];
        if (sprintathon_is_forced_scenery(light)) continue;
        if((light.baked || (light.scenery && SprintathonSkyBake::scenerySourceBaked(light.scenery_object_index))) && baked_receiver)continue;
        const float radius = std::max(sprintathon_emitter_radius(light.scenery), 1.0f);
        const float fade = sprintathon_emitter_fade[i] * sprintathon_emitter_gain(light.scenery) * light.shade_gain;
        upload(i, light.x, light.y, light.z, radius,
               light.r * fade, light.g * fade, light.b * fade, light.liquid_type, &light);
    }
    }
    for (size_t i = 0; i < sprintathon_dropped_flares.size(); ++i) {
        const auto& flare = sprintathon_dropped_flares[i];
        const float strength = sprintathon_flare_strength(flare) * gain;
        upload(sprintathon_emitter_slots - sprintathon_flare_capacity + int(i),
               flare.x, flare.y, flare.z, 5.5f * WORLD_ONE,
               strength, strength * 0.12f, strength * 0.04f, NONE, nullptr);
    }
    if (!compact) {
        for (int i = 0; i < sprintathon_emitter_slots; ++i)
            if (!uploaded[i]) shader->setVector4(colors[i], 0, 0, 0, 0);
    }
    // Clear only the tail of the final group. Entire later groups are skipped
    // uniformly in the shader, so stale inputs there are never evaluated.
    if (compact) {
        const int group_end = std::min(sprintathon_emitter_slots, (active_count + 3) / 4 * 4);
        for (int i = active_count; i < group_end; ++i)
            shader->setVector4(colors[i], 0, 0, 0, 0);
    }
    shader->setFloat(Shader::U_SprintathonSurfaceLightCount,
                     float(compact ? active_count : sprintathon_emitter_slots));

}

static void sprintathon_set_view_emitters(Shader *shader,
    const SprintathonSurfaceLightBounds *bounds = nullptr)
{
    const bool wall=shader==Shader::get(Shader::S_Wall) || shader==Shader::get(Shader::S_Bump);
    sprintathon_write_view_emitters(shader,bounds,bounds && bounds->valid && wall,
        wall && SprintathonSkyBake::textureReceiver && SprintathonSkyBake::textureBakeActive());
}

static void sprintathon_clear_pixel_light(RenderStep step, short texture_type,
                                          const SprintathonSurfaceLightBounds *bounds = nullptr)
{
    // Built-in wall/bump bloom shaders do not consume dynamic lighting.
    // Sprite bloom has its own lighting path and is unaffected.
    if (step == kGlow) return;
    Shader *shader = sprintathon_surface_shader(step, texture_type);
    if (shader) {
        shader->setVector4(Shader::U_SprintathonLightColor, 0, 0, 0, 0);
        sprintathon_set_view_emitters(shader, bounds);
    }
}

template<class LightSink>
static void sprintathon_write_projectile_light(LightSink* shader,float x,float y,float z,
    const SprintathonSurfaceLightBounds* bounds)
{
    if (!sprintathon_has_active_lighting) {
        shader->setVector4(Shader::U_SprintathonLightColor,0,0,0,0);return;
    }
    auto surface_distance_squared = [&](float lx, float ly, float lz) {
        // Surface centers can lie far outside a light that reaches an edge.
        const float px = bounds && bounds->valid ? std::max(bounds->x0, std::min(lx, bounds->x1)) : x;
        const float py = bounds && bounds->valid ? std::max(bounds->y0, std::min(ly, bounds->y1)) : y;
        const float pz = bounds && bounds->valid ? std::max(bounds->z0, std::min(lz, bounds->z1)) : z;
        const float dx = px-lx, dy = py-ly, dz = pz-lz;
        return dx*dx+dy*dy+dz*dz;
    };
    float radius = 4.5f * WORLD_ONE;
    float nearest = radius * radius;
    const object_data *selected = nullptr;
    float selected_z = 0;
    float color[3] = {0, 0, 0};
    for (const projectile_data& projectile : ProjectileList) {
        if (!SLOT_IS_USED(&projectile)) continue;
        float r = 0, g = 0, b = 0;
        switch (projectile.type) {
            case _projectile_minor_fusion_dispersal:
            case _projectile_fusion_bolt_minor: r=.18f; g=.24f; b=.48f; break;
            case _projectile_major_fusion_dispersal:
            case _projectile_fusion_bolt_major: r=.28f; g=.22f; b=.60f; break;
            case _projectile_compiler_bolt_minor:
            case _projectile_compiler_bolt_major: r=.48f; g=.13f; b=.30f; break;
            case _projectile_staff_bolt: r=.19f; g=.48f; b=.22f; break;
            case _projectile_alien_weapon: r=.60f; g=.27f; b=.08f; break;
            case _projectile_minor_defender:
            case _projectile_major_defender: r=.20f; g=.36f; b=.50f; break;
            case _projectile_minor_hummer:
            case _projectile_major_hummer:
            case _projectile_durandal_hummer: r=.44f; g=.20f; b=.52f; break;
            case _projectile_rocket:
            case _projectile_juggernaut_rocket:
            case _projectile_juggernaut_missile:
            case _projectile_flamethrower_burst: r=.70f; g=.32f; b=.09f; break;
            case _projectile_armageddon_sphere:
            case _projectile_overloaded_fusion_dispersal: r=.48f; g=.40f; b=.70f; break;
            default: continue;
        }
        float sampled[3];
        if (sprintathon_projectile_visual_color(projectile, sampled)) {
            const float strength = std::max(r, std::max(g, b));
            r = strength * sampled[0]; g = strength * sampled[1]; b = strength * sampled[2];
        }
        const object_data *object = get_object_data(projectile.object_index);
        if (!object) continue;
        const float distance_squared = surface_distance_squared(
            object->location.x, object->location.y, object->location.z);
        if (distance_squared < nearest) {
            nearest = distance_squared; selected = object;
            selected_z = object->location.z;
            color[0] = r; color[1] = g; color[2] = b;
        }
    }

    // A nearby flaming monster competes for the same per-pixel light slot.
    const float flame_radius = 7.0f*WORLD_ONE;
    for (const monster_data *monster : sprintathon_burning_monsters) {
        const object_data *object = get_object_data(monster->object_index);
        const float flame_z = object->location.z+WORLD_ONE/2;
        const float distance_squared = surface_distance_squared(
            object->location.x, object->location.y, flame_z);
        const float relative = distance_squared/(flame_radius*flame_radius);
        if (relative < 1.0f && relative < nearest/(radius*radius)) {
            nearest = distance_squared;
            radius = flame_radius;
            selected = object;
            selected_z = flame_z;
            color[0] = 1.3f;
            color[1] = 0.52f;
            color[2] = 0.12f;
        }
    }

    // Prefer the nearest active blast over a projectile while it is visible.
    float nearest_explosion = 1000000000.0f;
    for (const effect_data& effect : EffectList) {
        if (!SLOT_IS_USED(&effect) || effect.delay > 0) continue;
        float blast_radius = 0.0f, strength = 0.0f;
        switch (effect.type) {
            case _effect_rocket_explosion:
                blast_radius = 9.0f * WORLD_ONE; strength = 1.0f; break;
            case _effect_grenade_explosion:
                blast_radius = 7.0f * WORLD_ONE; strength = 0.82f; break;
            default: continue;
        }
        const object_data *object = get_object_data(effect.object_index);
        if (!object) continue;
        const float distance_squared = surface_distance_squared(
            object->location.x, object->location.y, object->location.z);
        if (distance_squared < blast_radius * blast_radius &&
            distance_squared < nearest_explosion) {
            nearest_explosion = distance_squared;
            selected = object;
            radius = blast_radius;
            selected_z = object->location.z;
            color[0] = strength;
            color[1] = strength * 0.46f;
            color[2] = strength * 0.13f;
        }
    }
    if (selected) {
        shader->setVector4(Shader::U_SprintathonLightPosition,
                           selected->location.x,
                           selected->location.y,
                           selected_z, 1.0f / std::max(radius, 1.0f));
        shader->setVector4(Shader::U_SprintathonLightColor, color[0] * graphics_preferences->colored_light_intensity / 100.0f,
                           color[1] * graphics_preferences->colored_light_intensity / 100.0f,
                           color[2] * graphics_preferences->colored_light_intensity / 100.0f, 1.0f);
    } else {
        shader->setVector4(Shader::U_SprintathonLightColor, 0, 0, 0, 0);
    }
}

static void sprintathon_set_pixel_light(float x,float y,float z,RenderStep step,short texture_type,
    const SprintathonSurfaceLightBounds* bounds=nullptr)
{
    if(step==kGlow)return;
    SprintathonSetupScope setupTime(SprintathonSetupTrace::PixelLight);
    Shader* shader=sprintathon_surface_shader(step,texture_type);if(!shader)return;
    sprintathon_set_view_emitters(shader,bounds);
    sprintathon_write_projectile_light(shader,x,y,z,bounds);
}

static bool sprintathon_flame_scene_captured = false;

struct SprintathonCorona {
    int kind, id;
    short polygon;
    float x,y,z,r,g,b,size,distance2,visibility;
};
static std::vector<SprintathonCorona> sprintathon_corona_history;
// Trace source visibility through the actual vertical portal openings. The
// corona is an optical halo, not a disc that should intersect world geometry.
static bool sprintathon_corona_visible(const view_data *camera, const SprintathonCorona& source)
{
    world_point2d from={camera->origin.x,camera->origin.y};
    world_point2d to={static_cast<world_distance>(source.x),static_cast<world_distance>(source.y)};
    if(line_is_obstructed(camera->origin_polygon_index,&from,source.polygon,&to))return false;
    short polygon_index=camera->origin_polygon_index;
    const double dx=source.x-from.x,dy=source.y-from.y;
    for(int step=0;step<256 && polygon_index!=NONE;++step) {
        if(polygon_index==source.polygon)return true;
        const short line_index=find_line_crossed_leaving_polygon(polygon_index,&from,&to);
        if(line_index==NONE)return false;
        const short next=find_adjacent_polygon(polygon_index,line_index);
        if(next==NONE)return false;
        const auto* line=get_line_data(line_index);
        const auto& a=get_endpoint_data(line->endpoint_indexes[0])->vertex;
        const auto& b=get_endpoint_data(line->endpoint_indexes[1])->vertex;
        const double ex=double(b.x)-a.x,ey=double(b.y)-a.y;
        const double denom=dx*ey-dy*ex;
        if(std::abs(denom)<0.000001)return false;
        const double t=((a.x-from.x)*ey-(a.y-from.y)*ex)/denom;
        const double z=camera->origin.z+t*(source.z-camera->origin.z);
        const auto* current=get_polygon_data(polygon_index);
        const auto* neighbor=get_polygon_data(next);
        if(z<=std::max(current->floor_height,neighbor->floor_height) ||
           z>=std::min(current->ceiling_height,neighbor->ceiling_height))return false;
        polygon_index=next;
    }
    return false;
}

static void sprintathon_draw_fog_coronas(const view_data *camera)
{
    static int level=NONE;
    static int32 last_tick=-1;
    if(level!=dynamic_world->current_level_number || dynamic_world->tick_count<last_tick) {
        sprintathon_corona_history.clear(); level=dynamic_world->current_level_number;
    }
    const int32 elapsed=last_tick<0 ? 1 : std::max(int32(0),dynamic_world->tick_count-last_tick);
    last_tick=dynamic_world->tick_count;
    const auto fog=OGL_GetCurrFogData();
    if(!graphics_preferences->fog_light_coronas || !graphics_preferences->fog_corona_strength ||
       !TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_Fog) ||
       ((!fog || !fog->IsPresent) && !TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_ForceFog))) {
        sprintathon_corona_history.clear();return;
    }
    std::vector<SprintathonCorona> sources;
    auto add=[&](int kind,int id,short polygon,float x,float y,float z,float r,float g,float b,float size) {
        if(polygon==NONE)return;
        const float dx=x-camera->origin.x,dy=y-camera->origin.y,dz=z-camera->origin.z;
        const float d2=dx*dx+dy*dy+dz*dz;
        if(d2<16 || d2>32.0f*32.0f*WORLD_ONE*WORLD_ONE)return;
        for(const auto& c:sources)if(c.kind==kind && c.id==id)return;
        sources.push_back({kind,id,polygon,x,y,z,r,g,b,size,d2,0});
    };
    if(graphics_preferences->projectile_lights_per_pixel && graphics_preferences->fog_projectile_coronas) {
        for(const auto& projectile:ProjectileList) {
            if(!SLOT_IS_USED(&projectile) || projectile.object_index==NONE)continue;
            float strength=0,red=1,green=1,blue=1;
        switch (projectile.type) {
            case _projectile_minor_fusion_dispersal:
            case _projectile_fusion_bolt_minor:
                strength = 0.36f; red = 0.40f; green = 0.65f; blue = 1.0f; break;
            case _projectile_major_fusion_dispersal:
            case _projectile_fusion_bolt_major:
                strength = 0.55f; red = 0.50f; green = 0.45f; blue = 1.0f; break;
            case _projectile_compiler_bolt_minor:
            case _projectile_compiler_bolt_major:
                strength = 0.50f; red = 0.95f; green = 0.30f; blue = 0.65f; break;
            case _projectile_staff_bolt:
                strength = 0.48f; red = 0.45f; green = 1.0f; blue = 0.50f; break;
            case _projectile_alien_weapon:
                strength = 0.55f; red = 1.0f; green = 0.48f; blue = 0.16f; break;
            case _projectile_minor_defender:
            case _projectile_major_defender:
                strength = 0.48f; red = 0.40f; green = 0.80f; blue = 1.0f; break;
            case _projectile_minor_hummer:
            case _projectile_major_hummer:
            case _projectile_durandal_hummer:
                strength = 0.55f; red = 0.85f; green = 0.45f; blue = 1.0f; break;
            case _projectile_rocket:
            case _projectile_juggernaut_rocket:
            case _projectile_juggernaut_missile:
            case _projectile_flamethrower_burst:
                strength = 0.68f; red = 1.0f; green = 0.50f; blue = 0.16f; break;
            case _projectile_armageddon_sphere:
            case _projectile_overloaded_fusion_dispersal:
                strength = 0.68f; red = 0.70f; green = 0.65f; blue = 1.0f; break;
            default: continue;
        }

            float sampled[3];
            if(sprintathon_projectile_visual_color(projectile,sampled)) {
                red=sampled[0];green=sampled[1];blue=sampled[2];
            }
            const auto* object=get_object_data(projectile.object_index);
            add(0,projectile.object_index,object->polygon,object->location.x,object->location.y,object->location.z,
                red,green,blue,WORLD_ONE*0.4f);
        }
    }
    if(graphics_preferences->bright_scenery_lights && graphics_preferences->fog_scenery_coronas) {
        for(const auto& light:sprintathon_texture_lights) {
            if(!light.scenery || light.scenery_object_index<0 || size_t(light.scenery_object_index)>=ObjectList.size())continue;
            const auto* object=&ObjectList[light.scenery_object_index];
            if(!SLOT_IS_USED(object) || GET_OBJECT_OWNER(object)!=_object_is_scenery)continue;
            add(1,light.scenery_object_index,object->polygon,light.x,light.y,light.z,
                light.r,light.g,light.b,WORLD_ONE*0.45f);
        }
    }
    for(size_t i=0;i<sprintathon_dropped_flares.size();++i) {
        const auto& flare=sprintathon_dropped_flares[i];
        const float strength=sprintathon_flare_strength(flare);
        add(2,flare.placed_tick,flare.polygon_index,flare.x,flare.y,flare.z,
            strength,strength*0.08f,strength*0.025f,WORLD_ONE*0.35f);
    }
    std::sort(sources.begin(),sources.end(),[](const SprintathonCorona& a,const SprintathonCorona& b) {
        if(a.distance2!=b.distance2)return a.distance2<b.distance2;
        return a.kind!=b.kind ? a.kind<b.kind : a.id<b.id;
    });
    if(sources.size()>24)sources.resize(24);
    const float response=1-std::exp(-std::min(float(elapsed)/TICKS_PER_SECOND,0.25f)/0.12f);
    for(auto& c:sources) {
        const bool visible=sprintathon_corona_visible(camera,c);
        float previous=0;
        for(const auto& old:sprintathon_corona_history)
            if(old.kind==c.kind && old.id==c.id && std::abs(old.x-c.x)+std::abs(old.y-c.y)<2*WORLD_ONE) {
                previous=old.visibility;break;
            }
        c.visibility=previous+((visible?1.0f:0.0f)-previous)*response;
    }
    sprintathon_corona_history=sources;
    Shader::disable();
    GLint active_texture;glGetIntegerv(GL_ACTIVE_TEXTURE,&active_texture);
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_CURRENT_BIT|GL_TEXTURE_BIT);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE_ARB);glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_FOG);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    // Screen blending is bounded; overlapping coronas no longer sum unchecked.
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_COLOR);
    const float depth=fog && fog->IsPresent?std::max(1.0f,fog->Depth):12.0f;
    for(const auto& c:sources) {
        const float distance=std::sqrt(c.distance2);
        const float haze=std::min(1.0f, 0.25f+1.5f*(1-std::exp(-distance/(WORLD_ONE*depth))));
        const float gain=c.visibility*haze*std::max(0.0f,1-distance/(32*WORLD_ONE))*
            graphics_preferences->fog_corona_strength/100.0f;
        if(gain<0.001f)continue;
        const float dx=(camera->origin.x-c.x)/distance,dy=(camera->origin.y-c.y)/distance,dz=(camera->origin.z-c.z)/distance;
        const float horizontal=std::sqrt(dx*dx+dy*dy);
        const float rx=horizontal>0.0001f?-dy/horizontal:1,ry=horizontal>0.0001f?dx/horizontal:0;
        const float ux=-dz*ry,uy=dz*rx,uz=horizontal;
        // Separate broad halo from the compact core. A single steep Gaussian
        // previously put almost all visible brightness inside the source sprite.
        for(int layer=0;layer<2;++layer) {
        const float size=c.size*(layer==0?4.0f:1.25f);
        const float opacity=layer==0?1.1f:1.0f;
        for(int ring=0;ring<48;++ring) {
            glBegin(GL_TRIANGLE_STRIP);
            for(int i=0;i<=32;++i)for(int k=0;k<2;++k) {
                const float radius=float(ring+k)/48;
                const float angle=i*(6.28318530718f/32);
                const float edge=1-radius*radius;
                // A smooth broad halo, slightly stronger than the original profile.
                const float profile=layer==0?1.15f*edge*std::sqrt(edge):std::exp(-radius*radius*5.0f)*edge;
                const float u=std::cos(angle)*radius*size,v=std::sin(angle)*radius*size;
                const float glow=std::min(0.22f,gain*profile*opacity*0.35f);
                float red=std::min(1.0f,c.r), green=std::min(1.0f,c.g), blue=std::min(1.0f,c.b);
                if(layer==0) {
                    // A faint radial spectral tint, without adding bright rings.
                    // Preserve the source colour at the centre and the soft edge fade.
                    // Keep the spectrum inside the visible halo rather than its
                    // nearly transparent rim. The inner core remains untouched.
                    const float t=std::max(0.0f,std::min(1.0f,(radius-0.18f)/0.60f));
                    const float ramp=std::max(0.0f,std::min(1.0f,(radius-0.12f)/0.18f));
                    const float blend=0.35f*ramp*ramp*(3.0f-2.0f*ramp);
                    const float phase=(1.0f-t)*4.1887902f;
                    const float sr=0.5f+0.5f*std::cos(phase);
                    const float sg=0.5f+0.5f*std::cos(phase-2.0943951f);
                    const float sb=0.5f+0.5f*std::cos(phase+2.0943951f);
                    const float brightness=std::max(red,std::max(green,blue));
                    // Preserve perceived brightness as hue changes, so green
                    // does not dominate and the blue portion stays visible.
                    const float source_luma=0.2126f*red+0.7152f*green+0.0722f*blue;
                    const float spectral_luma=0.2126f*sr+0.7152f*sg+0.0722f*sb;
                    const float spectral_gain=std::min(brightness*1.5f,
                        source_luma/std::max(0.15f,spectral_luma));
                    red+=(spectral_gain*sr-red)*blend;
                    green+=(spectral_gain*sg-green)*blend;
                    blue+=(spectral_gain*sb-blue)*blend;
                }
                glColor4f(red*glow,green*glow,blue*glow,0);
                glVertex3f(c.x+rx*u+ux*v,c.y+ry*u+uy*v,c.z+uz*v);
            }
            glEnd();
        }
    }
    }
    glPopAttrib();glActiveTextureARB(active_texture);
}

// Experimental local volumetric banks. Included inside the OpenGL renderer.
static bool sprintathon_draw_volume_fog(const view_data* camera, GLuint scene,
    GLuint depth, int width, int height, const GLfloat* projection, const GLfloat* modelview)
{
    if (!FBO_Allowed || !SDL_GL_ExtensionSupported("GL_ARB_texture_float") ||
        camera->origin_polygon_index == NONE || camera->under_media_boundary) return false;
    // Stable indices for the whole level: camera crossings and moving doors
    // must not swap a fixed-size neighbourhood in and out of the fog texture.
    SprintathonStartupPhase fog_prepare(SprintathonStartupTiming::get().fog_prepare_ms);
    const int room_count=dynamic_world->polygon_count;
    GLint max_texture_size=0;glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE_ARB,&max_texture_size);
    if(room_count<=0 || room_count>max_texture_size) return false;
    const int room_texels=18;
    std::vector<GLfloat> data(size_t(room_count)*room_texels*4,0);
    auto room_index=[&](short polygon)->int {return polygon>=0 && polygon<room_count ? polygon : -1;};
    auto static_layer=[&](short polygon)->float {
        const auto* room=get_polygon_data(polygon);
        float base=room->floor_height;
        if(room->type==_polygon_is_platform) {
            float sum=0;int count=0;
            for(int edge=0;edge<room->vertex_count;++edge) {
                short adjacent=find_adjacent_polygon(polygon,room->line_indexes[edge]);
                if(adjacent==NONE) continue;
                const auto* neighbor=get_polygon_data(adjacent);
                if(neighbor->type==_polygon_is_platform) continue;
                sum+=neighbor->floor_height;++count;
            }
            if(count) base=sum/count;
        }
        if(room->media_index!=NONE) {
            const auto* media=get_media_data(room->media_index);
            if(media) base=std::max(base,float(media->height));
        }
        return base/WORLD_ONE+0.15f;
    };
    // A shared height at each topologically connected corner makes the fog
    // field continuous across portals, including narrow stair polygons.
    // Never merge corners merely because their XY coordinates overlap.
    std::vector<int> corner_parent(size_t(room_count)*8);
    std::vector<float> layer_height(room_count),corner_sum(size_t(room_count)*8,0);
    std::vector<int> corner_count(size_t(room_count)*8,0);
    for(int i=0;i<room_count;++i) {
        if(get_polygon_data(i)->vertex_count>8) return false;
        layer_height[i]=static_layer(i);
        for(int e=0;e<8;++e) corner_parent[i*8+e]=i*8+e;
    }
    auto corner_root=[&](int corner)->int {
        while(corner_parent[corner]!=corner) {
            corner_parent[corner]=corner_parent[corner_parent[corner]];
            corner=corner_parent[corner];
        }
        return corner;
    };
    for(int i=0;i<room_count;++i) {
        const auto* room=get_polygon_data(i);
        for(int e=0;e<room->vertex_count;++e) {
            const short line_index=room->line_indexes[e];
            const int adjacent=room_index(find_adjacent_polygon(i,line_index));
            if(adjacent<=i) continue;
            const auto* neighbor=get_polygon_data(adjacent);
            const auto* line=get_line_data(line_index);
            // Use static topology here: opening a door must not rebuild the
            // density field. Actual visibility is still clipped by traversal.
            for(int end=0;end<2;++end) {
                const short endpoint=line->endpoint_indexes[end];
                for(int a=0;a<room->vertex_count;++a) {
                    if(room->endpoint_indexes[a]!=endpoint) continue;
                    for(int b=0;b<neighbor->vertex_count;++b) {
                        if(neighbor->endpoint_indexes[b]==endpoint) {
                            const int first=corner_root(i*8+a);
                            const int second=corner_root(adjacent*8+b);
                            corner_parent[first]=second;
                        }
                    }
                }
            }
        }
    }
    for(int i=0;i<room_count;++i) {
        const auto* room=get_polygon_data(i);
        for(int e=0;e<room->vertex_count;++e) {
            const int root=corner_root(i*8+e);
            corner_sum[root]+=layer_height[i];++corner_count[root];
        }
    }
    for (int i=0;i<room_count;++i) {
        const auto* room=get_polygon_data(i);
        if (room->vertex_count>8) return false;
        GLfloat* row=data.data()+size_t(i)*room_texels*4;
        row[0]=room->floor_height/float(WORLD_ONE);row[1]=room->ceiling_height/float(WORLD_ONE);
        row[2]=layer_height[i];row[3]=room->vertex_count;
        GLfloat* center=row+17*4;
        for(int e=0;e<room->vertex_count;++e) {
            const auto& vertex=get_endpoint_data(room->endpoint_indexes[e])->vertex;
            const int root=corner_root(i*8+e);
            GLfloat* corner=row+(9+e)*4;
            corner[0]=vertex.x/float(WORLD_ONE);corner[1]=vertex.y/float(WORLD_ONE);
            corner[2]=corner_sum[root]/std::max(1,corner_count[root]);
            for(int axis=0;axis<3;++axis) center[axis]+=corner[axis]/room->vertex_count;
        }
        for (int e=0;e<room->vertex_count;++e) {
            short edge=room->line_indexes[e];const auto* line=get_line_data(edge);
            const auto& a=get_endpoint_data(line->endpoint_indexes[0])->vertex;
            const auto& b=get_endpoint_data(line->endpoint_indexes[1])->vertex;
            float nx=-(float(b.y)-a.y),ny=float(b.x)-a.x;
            float length=std::sqrt(nx*nx+ny*ny);if(length<1) return false;
            nx/=length;ny/=length;
            float c=-(nx*a.x+ny*a.y)/WORLD_ONE;
            if(nx*room->center.x/WORLD_ONE+ny*room->center.y/WORLD_ONE+c<0) {nx=-nx;ny=-ny;c=-c;}
            GLfloat* plane=row+4*(e+1);plane[0]=nx;plane[1]=ny;plane[2]=c;
            // Match render visibility, not movement collision: solid windows
            // and grates can still expose the neighbouring polygon.
            plane[3]=LINE_IS_TRANSPARENT(line) ? room_index(find_adjacent_polygon(i,edge)) : -1;
        }
    }
    struct Light {float x,y,z,r,g,b,strength,radius,distance;int room;};
    std::vector<Light> volume_lights;
    auto add_light=[&](float x,float y,float z,float r,float g,float b,float strength,float radius,short polygon) {
        int room=room_index(polygon);if(room<0 || strength<=0) return;
        float dx=x-camera->origin.x,dy=y-camera->origin.y,dz=z-camera->origin.z;
        volume_lights.push_back({x/WORLD_ONE,y/WORLD_ONE,z/WORLD_ONE,r,g,b,strength,radius,dx*dx+dy*dy+dz*dz,room});
    };
    for(const auto& flare:sprintathon_dropped_flares)
        add_light(flare.x,flare.y,flare.z,1.0f,0.08f,0.025f,sprintathon_flare_strength(flare),3.0f,flare.polygon_index);
    for(const auto& projectile:ProjectileList) {
        if(!SLOT_IS_USED(&projectile)) continue;
        float rgb[3];
        if(!sprintathon_projectile_visual_color(projectile,rgb)) continue;
        const auto* object=get_object_data(projectile.object_index);
        add_light(object->location.x,object->location.y,object->location.z,rgb[0],rgb[1],rgb[2],0.65f,2.5f,object->polygon);
    }
    std::stable_sort(volume_lights.begin(),volume_lights.end(),[](const Light& a,const Light& b){return a.distance<b.distance;});
    // Subtle fixture scattering uses only spare slots in the existing fog
    // shader. Projectiles/flares retain priority; no new pass or sample loop.
    // Set SPRINTATHON_FOG_FIXTURE_LIGHTS=0 for a direct comparison.
    const char* fixture_env=std::getenv("SPRINTATHON_FOG_FIXTURE_LIGHTS");
    if((!fixture_env || fixture_env[0]!='0') &&
       graphics_preferences->projectile_lights_per_pixel && volume_lights.size()<4) {
        struct Candidate {const SprintathonTextureEmitter* source;float score;};
        std::vector<Candidate> fixtures;
        for(const auto& source:sprintathon_texture_lights) {
            if(source.liquid || !(source.scenery ? graphics_preferences->bright_scenery_lights :
                                                   graphics_preferences->bright_texture_lights))continue;
            if((source.scenery ? graphics_preferences->scenery_light_limit :
                                 graphics_preferences->texture_light_limit)<=0)continue;
            const float gain=std::max(0.0f,sprintathon_emitter_gain(source.scenery));
            const float peak=std::max(source.r,std::max(source.g,source.b));
            if(gain<=0 || peak<=0)continue;
            const float radius=std::min(4.0f,sprintathon_emitter_radius(source.scenery)/WORLD_ONE);
            const float dx=(source.x-camera->origin.x)/WORLD_ONE;
            const float dy=(source.y-camera->origin.y)/WORLD_ONE;
            const float dz=(source.z-camera->origin.z)/WORLD_ONE;
            const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
            // Fade at the selection horizon; do not select by flicker phase.
            const float horizon=std::max(0.0f,std::min(1.0f,(12.0f+radius-distance)/4.0f));
            const float score=std::min(1.0f,peak*gain)*horizon/(1.0f+distance*distance/(radius*radius));
            if(score>0)fixtures.push_back({&source,score});
        }
        std::stable_sort(fixtures.begin(),fixtures.end(),[](const Candidate& a,const Candidate& b){return a.score>b.score;});
        const size_t available=4-volume_lights.size();
        const float cutoff=fixtures.size()>available ? fixtures[available].score : 0.0f;
        for(size_t i=0;i<std::min(available,fixtures.size());++i) {
            const auto& source=*fixtures[i].source;
            short polygon=NONE;
            if(source.scenery) {
                if(source.scenery_object_index<0 || size_t(source.scenery_object_index)>=ObjectList.size())continue;
                const auto* object=&ObjectList[source.scenery_object_index];
                if(!SLOT_IS_USED(object) || GET_OBJECT_OWNER(object)!=_object_is_scenery)continue;
                polygon=object->polygon;
            } else {
                for(int room=0;room<room_count;++room)
                    if(get_polygon_data(room)==source.source_polygon){polygon=short(room);break;}
            }
            if(room_index(polygon)<0)continue;
            const auto* room=get_polygon_data(polygon);
            if(room->ceiling_height-room->floor_height<2)continue;
            float plane[4];sprintathon_emitter_plane(&source,plane);
            // Put wall/floor/ceiling emitters just inside their owning room.
            const float x=source.x+plane[0]*2.0f,y=source.y+plane[1]*2.0f;
            const float z=std::max(float(room->floor_height)+1.0f,
                std::min(float(room->ceiling_height)-1.0f,source.z+plane[2]*2.0f));
            const float peak=std::max(source.r,std::max(source.g,source.b));
            const float gain=std::max(0.0f,sprintathon_emitter_gain(source.scenery));
            // Smooth slot competition: an emitter approaches zero as another
            // catches up, rather than abruptly exchanging a full-strength glow.
            const float selection=std::min(1.0f,4.0f*std::max(0.0f,fixtures[i].score-cutoff));
            const float strength=0.12f*std::min(1.0f,peak*gain)*
                std::max(0.0f,std::min(1.0f,source.shade_gain))*selection;
            add_light(x,y,z,source.r/peak,source.g/peak,source.b/peak,strength,
                std::min(4.0f,sprintathon_emitter_radius(source.scenery)/WORLD_ONE),polygon);
        }
    }
    GLfloat positions[16]={},colors[16]={};
    for(int i=0;i<4;++i) {
        colors[4*i+3]=-1;
        if(size_t(i)>=volume_lights.size()) continue;
        const auto& l=volume_lights[i];positions[4*i]=l.x;positions[4*i+1]=l.y;positions[4*i+2]=l.z;positions[4*i+3]=l.radius;
        colors[4*i]=l.r*l.strength;colors[4*i+1]=l.g*l.strength;colors[4*i+2]=l.b*l.strength;colors[4*i+3]=l.room;
    }
    fog_prepare.finish();
    const char* fog_debug_env=std::getenv("SPRINTATHON_FOG_DEBUG");
    const float fog_debug=(fog_debug_env && fog_debug_env[0]=='1') ? 1.0f : 0.0f;
    auto& target=sprintathon_volume_target;
    GLint previous_fbo,previous_viewport[4];glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&previous_fbo);
    glGetIntegerv(GL_VIEWPORT,previous_viewport);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);
    glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const int half_width=std::max(1,(width+3)/4),half_height=std::max(1,(height+3)/4);
    sprintathon_prepare_volume_buffer(half_width,half_height);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,target.fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,target.color,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,previous_fbo);glPopAttrib();return false;
    }
    glViewport(0,0,half_width,half_height);
    glActiveTextureARB(GL_TEXTURE3_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,target.rooms);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    if(target.room_height!=room_count) {
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA32F_ARB,room_texels,room_count,0,GL_RGBA,GL_FLOAT,data.data());
        target.room_height=room_count;target.room_data=data;
    } else if(target.room_data!=data) {
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,room_texels,room_count,GL_RGBA,GL_FLOAT,data.data());
        target.room_data=data;
    }
    glActiveTextureARB(GL_TEXTURE2_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,depth);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    Shader* shader=Shader::get(Shader::S_VolumeFog);shader->enable();
    GLhandleARB program=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    auto uniform=[&](const char* name){return glGetUniformLocationARB(program,name);};
    glUniform1iARB(uniform("texture2"),2);glUniform1iARB(uniform("texture3"),3);
    // Inverse of the rigid camera rotation: game-world coordinates, including roll.
    GLfloat rotation[16]={};rotation[15]=1;
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) rotation[c*4+r]=modelview[r*4+c];
    glUniformMatrix4fvARB(uniform("vfViewRotation"),1,GL_FALSE,rotation);
    glUniform4fARB(uniform("vfProjection"),projection[0],projection[5],projection[8],projection[9]);
    glUniform4fARB(uniform("vfCamera"),camera->origin.x/float(WORLD_ONE),camera->origin.y/float(WORLD_ONE),camera->origin.z/float(WORLD_ONE),projection[14]);
    glUniform4fARB(uniform("vfSettings"),width,height,projection[10],float(dynamic_world->tick_count)/TICKS_PER_SECOND);
    glUniform4fvARB(uniform("vfLights"),4,positions);glUniform4fvARB(uniform("vfColors"),4,colors);
    glUniform1fARB(uniform("vfDebug"),fog_debug);
    glUniform1fARB(uniform("vfStartRoom"),camera->origin_polygon_index);
    glUniform1fARB(uniform("vfStrength"),Get_OGL_ConfigureData().DriftingFogIntensity/100.0f);
    shader->setFloat(Shader::U_FogMode,OGL_GetCurrFogData()->Mode);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    auto quad=[](){glBegin(GL_QUADS);
        glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
        glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();};
    quad();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,previous_fbo);
    glViewport(previous_viewport[0],previous_viewport[1],previous_viewport[2],previous_viewport[3]);
    shader=Shader::get(Shader::S_VolumeFogComposite);shader->enable();
    program=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glUniform1iARB(uniform("texture0"),0);glUniform1iARB(uniform("texture1"),1);glUniform1iARB(uniform("texture2"),2);
    glUniform1fARB(uniform("vfDebug"),fog_debug);
    glUniform4fARB(uniform("vfSize"),width,height,half_width,half_height);
    glUniform4fARB(uniform("vfDepth"),projection[10],projection[14],0,0);
    glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,target.color);
    glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,scene);
    quad();Shader::disable();
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
    glPopAttrib();glActiveTextureARB(GL_TEXTURE0_ARB);
    return true;
}

// Level-stable palette: no landscape setup or polygon search per water draw.
struct SprintathonLiquidPalette {
    bool ready=false, valid=false;
    int level=-1, polygons=-1;
    float colour[3]={0,0,0};
};
static SprintathonLiquidPalette sprintathon_liquid_palette;
static void sprintathon_prepare_liquid_palette();

void sprintathon_preload_post_buffers(){
    // Prepare even when disabled, so toggling speculars needs no new texture.
    GLint waterActive=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&waterActive);
    glPushAttrib(GL_TEXTURE_BIT);
    SprintathonWaterNormals::bind();
    sprintathon_prepare_liquid_palette();
    glPopAttrib();glActiveTextureARB(waterActive);
    const SDL_Rect rect=alephone::Screen::instance()->view_rect();
    const GLsizei width=SprintathonRenderPixels(rect.w),height=SprintathonRenderPixels(rect.h);
    if(width<=0 || height<=0)return;
    const auto& config=Get_OGL_ConfigureData();
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    const bool fog=TEST_FLAG(config.Flags,OGL_Flag_Fog);
    if(config.AmbientOcclusion || config.LandscapeLightShafts || config.AnamorphicLensFlares || fog){
        sprintathon_capture_scene_depth(width,height,false);
        sprintathon_capture_scene_color(width,height,false);
    }
    if(config.AnamorphicLensFlares && config.AnamorphicLensFlareStrength>0)
        sprintathon_prepare_flare_buffers(width,height);
    if(config.RefractiveInvisibility)setup_invisibility_refraction(nullptr,width,height,1);
    if(config.AnimatedMediaRipples && TEST_FLAG(config.Flags,OGL_Flag_LiqSeeThru))
        sprintathon_prepare_media_scene(width,height);
    if(config.AmbientOcclusion)sprintathon_prepare_ao(std::max(1,(width+1)/2),std::max(1,(height+1)/2));
    if(config.LandscapeLightShafts){
        sprintathon_begin_shaft_source(width,height,nullptr,true);
        sprintathon_prepare_shaft_scattering(width,height);
    }
    if(fog && graphics_preferences->sprintathon_volume_fog && FBO_Allowed &&
        SDL_GL_ExtensionSupported("GL_ARB_texture_float")){
        glActiveTextureARB(GL_TEXTURE0_ARB);
        sprintathon_prepare_volume_buffer(std::max(1,(width+3)/4),std::max(1,(height+3)/4));
    }
    glPopAttrib();glActiveTextureARB(active);
}

// Reset before texture preparation so the palette captured there survives baking.
void sprintathon_begin_sky_loading() {
    sprintathon_map_emitters.clear();
    SprintathonLuaSun::state().locked=true;
    sprintathon_liquid_palette=SprintathonLiquidPalette{};
    SprintathonSkyBake::reset();
    SprintathonSkyBake::prepare();
}
void sprintathon_preload_sky_palette(const float ambient[3],const float sunlight[3]) {
    if(SprintathonSkyBake::enabled()) SprintathonSkyBake::landscapeColor(ambient,sunlight);
}

// Called while the level-loading progress screen is still active.
void sprintathon_prebake_sky_lighting(void (*progress)()) {
    auto emission=[](shape_descriptor shape,float* rgb)->bool{
        TextureManager texture;texture.ShapeDesc=shape;
        get_shape_bitmap_and_shading_table(shape,&texture.Texture,&texture.ShadingTables,_shading_normal);
        if(!texture.Texture)return false;
        texture.IsShadeless=false;texture.TransferMode=_textured_transfer;texture.TransferData=0;texture.TextureType=OGL_Txtr_Wall;
        if(!texture.Setup())return false;
        float u,v;return texture.GetBrightEmission(u,v,rgb);
    };
    auto sceneryEmission=[](short index,SprintathonSkyBake::V& position,SprintathonSkyBake::V& color)->bool{
        const auto* object=&ObjectList[index];short damaged=NONE;
        if(object->permutation<0 || object->permutation>=scenery_type_count() ||
           get_damaged_scenery_collection(object->permutation,damaged) ||
           OBJECT_IS_INVISIBLE(object) || (object->transfer_mode!=NONE && object->transfer_mode!=_xfer_normal))return false;
        if(object->shape==UNONE || !is_collection_present(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(object->shape))))return false;
        const auto* animation=get_shape_animation_data(object->shape);
        // A fixed frame and view guarantee stable emission and hotspot height.
        if(!animation || animation->frames_per_view!=1 || (animation->number_of_views!=_unanimated && animation->number_of_views!=_animated1))return false;
        if(object->transfer_mode==NONE && animation->transfer_mode!=_xfer_normal)return false;
        const short collection=GET_DESCRIPTOR_COLLECTION(object->shape);
        const short frame=animation->low_level_shape_indexes[0];
        if(frame<0 || frame>=get_number_of_collection_frames(GET_COLLECTION(collection)))return false;
        short modelSequence=0;
        if(OGL_GetModelData(GET_COLLECTION(collection),GET_DESCRIPTOR_SHAPE(object->shape),modelSequence))return false;
        const auto* info=extended_get_shape_information(collection,frame);
        if(!info)return false;
        TextureManager texture;texture.ShapeDesc=object->shape;texture.LowLevelShape=frame;
        extended_get_shape_bitmap_and_shading_table(collection,frame,&texture.Texture,&texture.ShadingTables,_shading_normal);
        if(!texture.Texture)return false;
        texture.IsShadeless=false;texture.TransferMode=_textured_transfer;texture.TransferData=0;
        texture.TextureType=OGL_Txtr_Inhabitant;if(!texture.Setup())return false;
        float u,v,rgb[3];if(!texture.GetBrightEmission(u,v,rgb))return false;
        const auto scaled=[&](int value){
            if(object->flags&_object_is_enlarged)return value+(value>>2);
            if(object->flags&_object_is_tiny)return value>>1;
            return value;
        };
        const int bottom=scaled(info->world_bottom),top=scaled(info->world_top);
        position=SprintathonSkyBake::V(object->location.x,object->location.y,
            object->location.z+bottom+(top-bottom)*(1.0f-v));
        color=SprintathonSkyBake::V(rgb[0],rgb[1],rgb[2]);return true;
    };
    sprintathon_preload_map_emitters(emission,progress);
    SprintathonSkyBake::preloadTextureSources(emission,sceneryEmission,progress);
    SprintathonSkyBake::preload(progress);
}



// Resolve metadata before touching drawing state. A scoped handoff lets the
// existing binder consume that exact manager without repeating Setup().
static std::unique_ptr<TextureManager>* sprintathon_resolved_wall=nullptr;
struct SprintathonResolvedWallScope {
    std::unique_ptr<TextureManager>* previous;
    explicit SprintathonResolvedWallScope(std::unique_ptr<TextureManager>& manager)
        :previous(sprintathon_resolved_wall){sprintathon_resolved_wall=&manager;}
    ~SprintathonResolvedWallScope(){sprintathon_resolved_wall=previous;}
};
struct SprintathonMaterialTrace {
    unsigned resolved=0, skippedOpaque=0, skippedRemaining=0, bound=0;
    static SprintathonMaterialTrace& get(){static SprintathonMaterialTrace t;return t;}
};
static bool sprintathon_early_material_enabled(){
    static const bool enabled=[](){const char* v=std::getenv("SPRINTATHON_EARLY_MATERIAL");return !v || v[0]!='0';}();
    return enabled;
}
struct SprintathonMaterialFrame {
    SprintathonMaterialFrame(){SprintathonMaterialTrace::get()=SprintathonMaterialTrace{};}
    ~SprintathonMaterialFrame(){
        if(sprintathon_detail_frame()){
            const auto& t=SprintathonMaterialTrace::get();
            std::fprintf(stderr,"Early material frame %llu: enabled %d resolved %u bound %u avoided-opaque %u avoided-remaining %u\n",
                (unsigned long long)SprintathonStartupTiming::get().frame_id,
                int(sprintathon_early_material_enabled()),t.resolved,t.bound,t.skippedOpaque,t.skippedRemaining);
        }
    }
};
static std::unique_ptr<TextureManager> sprintathon_resolve_wall(shape_descriptor texture){
    SprintathonSetupScope preparationTime(SprintathonSetupTrace::MaterialResolve);
    ++SprintathonMaterialTrace::get().resolved;
    auto manager=std::make_unique<TextureManager>();
    manager->ShapeDesc=texture;
    if(texture==UNONE)return manager;
    get_shape_bitmap_and_shading_table(texture,&manager->Texture,&manager->ShadingTables,_shading_normal);
    manager->TextureType=OGL_Txtr_Wall;
    manager->TransferMode=_textured_transfer;
    manager->TransferData=0;manager->IsShadeless=false;
    if(!manager->Setup())manager->ShapeDesc=UNONE;
    return manager;
}

// Opt-in, frame-local prepared surface packets. Never retain map data across
// frames: platforms, editor changes and interpolated camera positions can move.
static bool sprintathon_surface_commands_requested() {
    static const bool enabled=[](){const char* v=std::getenv("SPRINTATHON_SURFACE_COMMANDS");return v && v[0]=='1';}();
    return enabled;
}
struct SprintathonBloomPacket {
    std::unique_ptr<TextureManager> material;
    std::vector<GLfloat> vertices, uv;
    GLfloat normal[3], tangent[4], color[3];
    float intensity=0, offset=0;
    bool voidPresent=false;
    bool commandOpaque=false;
    clipping_window_data* window=nullptr; // frame-local visibility allocation
    GLenum mode=GL_POLYGON;
};
struct SprintathonBloomReuse {
    using Key=std::vector<int64_t>;
    std::map<Key,SprintathonBloomPacket> packets;
    bool active=false, commands=false;
    bool shaftReplayed=false, bloomReplayed=false;
    std::vector<SprintathonBloomPacket*> commandList;
    unsigned shaftCommands=0,bloomCommands=0,shaftSkips=0,bloomSkips=0;
    double shaftReplayMs=0,bloomReplayMs=0;
    unsigned hits=0, misses=0;
    static SprintathonBloomReuse& get(){static SprintathonBloomReuse r;return r;}
    void begin(){
        static const bool requested=[](){const char* v=std::getenv("SPRINTATHON_BLOOM_REUSE");return v && v[0]=='1';}();
        commandList.clear();packets.clear();hits=misses=0;
        shaftReplayed=bloomReplayed=false;
        shaftCommands=bloomCommands=shaftSkips=bloomSkips=0;
        shaftReplayMs=bloomReplayMs=0;
        const char* opaqueOrder=std::getenv("SPRINTATHON_OPAQUE_FIRST");
        // Match the established main-world opaque-first ordering. Custom/order-
        // sensitive configurations retain the original traversal in every pass.
        commands=sprintathon_surface_commands_requested() &&
            graphics_preferences->projectile_lights_per_pixel &&
            (!opaqueOrder || opaqueOrder[0]!='0') && !current_player->infravision_duration;
        active=!current_player->infravision_duration &&
            ((commands && SprintathonPassTest::get().shafts() && Get_OGL_ConfigureData().LandscapeLightShafts) ||
             ((requested || commands) && SprintathonPassTest::get().bloom() &&
              TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_Blur)));
    }
    SprintathonBloomPacket* find(const Key& key){
        auto i=packets.find(key);
        if(i==packets.end()){++misses;return nullptr;}
        ++hits;return &i->second;
    }
    void save(const Key& key,std::unique_ptr<TextureManager>& material,GLenum mode,int count,
              const GLfloat* vertices,const GLfloat* uv,float nx,float ny,float nz,
              float tx,float ty,float tz,float sign,float intensity,float offset,
              const float* projectile,bool voidPresent,clipping_window_data* window,bool opaque){
        if(!active || packets.size()>=8192 || !material || count<=0)return;
        const bool inserted=packets.find(key)==packets.end();
        auto& p=packets[key];
        p.window=window;
        // Blended/void-backed materials and glow overlays stay in traversal order.
        // Alpha-cutout solids are safe here: their mask is still sampled.
        p.commandOpaque=commands && opaque && !material->IsBlended() && !material->IsGlowMapped();
        if(inserted && p.commandOpaque)commandList.push_back(&p);
        p.vertices.assign(vertices,vertices+count*3);p.uv.assign(uv,uv+count*2);
        p.normal[0]=nx;p.normal[1]=ny;p.normal[2]=nz;
        p.tangent[0]=tx;p.tangent[1]=ty;p.tangent[2]=tz;p.tangent[3]=sign;
        for(int i=0;i<3;++i)p.color[i]=graphics_preferences->projectile_lights_per_pixel ?
            intensity:std::min(1.0f,intensity+projectile[i]);
        p.intensity=intensity;p.offset=offset;p.voidPresent=voidPresent;p.mode=mode;
        p.material=std::move(material);
    }
    void end(){
        if(active && sprintathon_detail_frame())
            std::fprintf(stderr,"Bloom reuse frame %llu: prepared %u hits %u misses %u; walls/floors only\n",
                (unsigned long long)SprintathonStartupTiming::get().frame_id,
                unsigned(packets.size()),hits,misses);
        if(commands && sprintathon_detail_frame())
            std::fprintf(stderr,"Surface commands frame %llu: captured %u; shaft replay %u skipped %u CPU %.3f ms; bloom replay %u skipped %u CPU %.3f ms; other surfaces use traversal\n",
                (unsigned long long)SprintathonStartupTiming::get().frame_id,unsigned(commandList.size()),
                shaftCommands,shaftSkips,shaftReplayMs,bloomCommands,bloomSkips,bloomReplayMs);
        commandList.clear();packets.clear();active=commands=false;
    }
};
struct SprintathonBloomReuseFrame {
    SprintathonBloomReuseFrame(){SprintathonBloomReuse::get().begin();}
    ~SprintathonBloomReuseFrame(){SprintathonBloomReuse::get().end();}
};

void RenderRasterize_Shader::render_tree() {
    const auto& passTest=SprintathonPassTest::get();
    passTest.frame();
    SprintathonSkyBake::prepare();
    SprintathonStartupFrame startup_frame;
    SprintathonBloomReuseFrame bloomReuseFrame;
    SprintathonMaterialFrame materialFrame;
    SprintathonLightIndexFrame lightIndexFrame;
    if(SprintathonStartupTiming::get().active())sprintathon_light_change_trace=SprintathonLightChangeTrace{};
    SprintathonStartupPhase light_prepare(SprintathonStartupTiming::get().light_prepare_ms);
    sprintathon_flame_scene_captured = false;
    sprintathon_update_flares();
    if (sprintathon_visual_level != dynamic_world->current_level_number) {
        sprintathon_projectile_visuals.clear();
        sprintathon_visual_level = dynamic_world->current_level_number;
    }
    // Static texture lights survive occlusion while in discovery range. Reset
    // the registry on level transitions, timeline rewinds, or disabled lighting.
    static int16 texture_light_level = NONE;
    static int32 texture_light_game_tick = -1;
    const int16 current_level = dynamic_world->current_level_number;
    if (texture_light_level != current_level ||
        dynamic_world->tick_count < texture_light_game_tick) {
        sprintathon_texture_lights.clear();
        sprintathon_texture_lights_next.clear();
        for (int i = 0; i < sprintathon_emitter_slots; ++i) {
            sprintathon_previous_valid[i] = false;
            sprintathon_emitter_fade[i] = 0;
        }
        texture_light_level = current_level;
    }
    texture_light_game_tick = dynamic_world->tick_count;
    if (graphics_preferences->projectile_lights_per_pixel &&
        (graphics_preferences->bright_texture_lights ||
         graphics_preferences->bright_scenery_lights)) {
        sprintathon_refresh_texture_lights();
    } else {
        sprintathon_texture_lights.clear();
        sprintathon_texture_lights_next.clear();
    }

    static int cone_pool_level = NONE;
    static int32 cone_pool_tick = -1;
    const bool reset_cones = cone_pool_level != dynamic_world->current_level_number ||
        dynamic_world->tick_count < cone_pool_tick;
    if (reset_cones || !graphics_preferences->ceiling_fog_cones) {
        sprintathon_fog_cones.clear();
        sprintathon_floor_cones.clear();
    }
    // Retain discovered fixtures even when their sprites leave the view.
    // Rebuild geometry once per simulation tick, including moving platforms.
    if (reset_cones || cone_pool_tick != dynamic_world->tick_count) {
        std::vector<SprintathonFogCone> known;
        known.swap(sprintathon_fog_cones);
        for (const auto& cone : known) {
            if (cone.object_index < 0 || size_t(cone.object_index) >= ObjectList.size()) continue;
            const auto* object = &ObjectList[cone.object_index];
            if (!SLOT_IS_USED(object) || GET_OBJECT_OWNER(object) != _object_is_scenery ||
                object->polygon != cone.polygon_index || int(object->shape) != cone.source_shape ||
                object->location.x != cone.x || object->location.y != cone.y) continue;
            const float rgb[] = {cone.red, cone.green, cone.blue};
            sprintathon_collect_fog_cone(cone.object_index,
                cone.z + float(object->location.z) - cone.anchor_z, rgb);
        }
    }
    // Validate also while paused: editor deletion must not leave ghost lights.
    sprintathon_fog_cones.erase(std::remove_if(sprintathon_fog_cones.begin(),
        sprintathon_fog_cones.end(), [](const SprintathonFogCone& cone) {
            if (cone.object_index < 0 || size_t(cone.object_index) >= ObjectList.size()) return true;
            const auto* object = &ObjectList[cone.object_index];
            return !SLOT_IS_USED(object) || GET_OBJECT_OWNER(object) != _object_is_scenery ||
                object->polygon != cone.polygon_index || int(object->shape) != cone.source_shape ||
                object->location.x != cone.x || object->location.y != cone.y;
        }), sprintathon_fog_cones.end());
    // Stable pool slots: camera motion must not change which fixtures light a floor.
    std::sort(sprintathon_fog_cones.begin(), sprintathon_fog_cones.end(),
        [](const SprintathonFogCone& a, const SprintathonFogCone& b) {
            return a.object_index < b.object_index;
        });
    cone_pool_level = dynamic_world->current_level_number;
    cone_pool_tick = dynamic_world->tick_count;
    sprintathon_floor_cones = sprintathon_fog_cones;
    sprintathon_visible_scenery.assign(ObjectList.size(), false);
    if (sprintathon_all_scenery_enabled()) {
        for (const auto& node : RSPtr->SortedNodes) {
            for (auto first : {node.interior_objects, node.exterior_objects}) {
                for (auto object = first; object; object = object->next_object) {
                    const short index = object->scenery_object_index;
                    if (object->is_scenery && index >= 0 &&
                        size_t(index) < sprintathon_visible_scenery.size())
                        sprintathon_visible_scenery[index] = true;
                }
            }
        }
    }
    sprintathon_upload_all_scenery_lights();
    sprintathon_select_view_emitters(view);
    light_prepare.finish();
    if(SprintathonStartupTiming::get().active()) {
        const auto& t=sprintathon_light_change_trace;
        const auto frame=SprintathonStartupTiming::get().frame_id;
        if(frame==1 || frame%120==0 || t.added || t.removed || t.recolored || t.brightened ||
           t.activated || t.extinguished || t.allocations || t.uploads) {
            unsigned textures=0,scenery=0,lit=0,selected=0;
            for(const auto& source:sprintathon_texture_lights) {
                if(source.scenery)++scenery;else ++textures;
                if(source.shade_gain>0.001f)++lit;
            }
            for(int i=0;i<sprintathon_emitter_slots;++i)
                if(sprintathon_previous_valid[i] && sprintathon_emitter_fade[i]>0.001f)++selected;
            fprintf(stderr,"Light changes frame %llu tick %ld: sources texture=%u scenery=%u lit=%u selected=%u all-scenery=%d; observations=%u new-candidates=%u removed=%u recolored=%u brightened=%u on=%u off=%u; refresh=%.3f scenery-upload=%.3f ms; alloc=%u subupload=%u bytes=%llu; bake-active=%d bake-valid=%d\n",
                static_cast<unsigned long long>(frame),long(dynamic_world->tick_count),
                textures,scenery,lit,selected,sprintathon_all_scenery_count,t.observations,t.added,t.removed,t.recolored,t.brightened,t.activated,t.extinguished,
                t.refresh_ms,t.upload_ms,t.allocations,t.uploads,static_cast<unsigned long long>(t.bytes),
                int(SprintathonSkyBake::textureBakeActive()),int(SprintathonSkyBake::textureBakeValid));
        }
    }
    sprintathon_has_active_lighting = false;
    for (const projectile_data& projectile : ProjectileList) {
        if (SLOT_IS_USED(&projectile)) {
            sprintathon_has_active_lighting = true;
            break;
        }
    }
    if (!sprintathon_has_active_lighting) {
        for (const effect_data& effect : EffectList) {
            if (SLOT_IS_USED(&effect) && effect.delay == 0 &&
                (effect.type == _effect_rocket_explosion || effect.type == _effect_grenade_explosion)) {
                sprintathon_has_active_lighting = true;
                break;
            }
        }
    }
    sprintathon_burning_monsters.clear();
    if (graphics_preferences->projectile_lights_per_pixel) {
        for (const monster_data& monster : MonsterList) {
            if (SLOT_IS_USED(&monster) && monster.action == _monster_is_dying_flaming &&
                monster.object_index != NONE) {
                sprintathon_burning_monsters.push_back(&monster);
            }
        }
        if (!sprintathon_burning_monsters.empty())
            sprintathon_has_active_lighting = true;
    }

	GLfloat scene_projection[16];
	glGetFloatv(GL_PROJECTION_MATRIX, scene_projection);
    GLfloat volume_modelview[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, volume_modelview);

	weaponFlare = PIN(view->maximum_depth_intensity - NATURAL_LIGHT_INTENSITY, 0, FIXED_ONE)/float(FIXED_ONE);
	selfLuminosity = PIN(NATURAL_LIGHT_INTENSITY, 0, FIXED_ONE)/float(FIXED_ONE);
    // World surfaces receive a spatial muzzle flash in the shader.
    if (graphics_preferences->projectile_lights_per_pixel ||
        !graphics_preferences->player_light_circle)
        weaponFlare = 0.0f;
    if (!graphics_preferences->player_light_circle)
        selfLuminosity = 0.0f;

	Shader* s = Shader::get(Shader::S_Invincible);
	s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
	s->setFloat(Shader::U_Time, view->tick_count);
	s->setFloat(Shader::U_LogicalWidth, view->screen_width);
	s->setFloat(Shader::U_LogicalHeight, view->screen_height);
	s->setFloat(Shader::U_PixelWidth, SprintathonRenderPixels(view->screen_width));
	s->setFloat(Shader::U_PixelHeight, SprintathonRenderPixels(view->screen_height));
	if (blur.get()) {
		s = Shader::get(Shader::S_InvincibleBloom);
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
		s->setFloat(Shader::U_Time, view->tick_count);
		s->setFloat(Shader::U_LogicalWidth, view->screen_width);
		s->setFloat(Shader::U_LogicalHeight, view->screen_height);
		s->setFloat(Shader::U_PixelWidth, blur->width());
		s->setFloat(Shader::U_PixelHeight, blur->height());
	}

	short leftmost = INT16_MAX;
	short rightmost = INT16_MIN;
	vector<clipping_window_data>& windows = RSPtr->RVPtr->ClippingWindows;
	for (vector<clipping_window_data>::const_iterator it = windows.begin(); it != windows.end(); ++it) {
		if (it->x0 < leftmost) {
			leftmost = it->x0;
			leftmost_clip = it->left;
		}
		if (it->x1 > rightmost) {
			rightmost = it->x1;
			rightmost_clip = it->right;
		}
	}
	
	float fogMix = 0.0;
	auto fogdata = OGL_GetCurrFogData();
	if (fogdata && fogdata->IsPresent && fogdata->AffectsLandscapes) {
		fogMix = fogdata->LandscapeMix;
	}

	float fogmode = -1.0;
	if (fogdata) {
		fogmode = fogdata->Mode;
	}

	float media_fog_enabled = 0.0f;
	float media_fog_top = 0.0f;
	float media_fog_softness = static_cast<float>(WORLD_ONE) * 0.5f;
	const bool any_forced_fallback_fog = fogdata && !fogdata->IsPresent &&
		TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_ForceFog);
	const bool forced_fallback_fog = any_forced_fallback_fog &&
		Get_OGL_ConfigureData().ForceFogMediaRelative &&
		!(current_player->variables.flags & _HEAD_BELOW_MEDIA_BIT);
	if (forced_fallback_fog)
	{
		const media_data* selected_media = nullptr;
		// Use the highest media surface as one stable plane for the whole level.
		// A camera-relative choice would follow the player between vertically
		// stacked pools and prevent the player from ever entering the fog.
		for (size_t media_index = 0;
			media_index < MediaList.size();
			++media_index)
		{
			const media_data* candidate = get_media_data(media_index);
			if (candidate &&
				(!selected_media || candidate->height > selected_media->height))
			{
				selected_media = candidate;
			}
		}

		if (selected_media)
		{
			media_fog_top = static_cast<float>(
				selected_media->height + WORLD_ONE / 2);
			// Blend between global fog while immersed and height-limited fog
			// above the layer, rather than popping at the surface.
			const float camera_z = static_cast<float>(
				current_player->camera_location.z);
			const float transition_start =
				media_fog_top - media_fog_softness * 0.5f;
			float transition = A1_PIN(
				(camera_z - transition_start) / media_fog_softness,
				0.0f, 1.0f);
			transition = transition * transition *
				(3.0f - 2.0f * transition);
			media_fog_enabled = transition;
		}
	}

	// Landscapes cannot intersect a local height band. Mix them only while the
	// viewer is inside global fallback fog, or when media-relative fog is off.
	if (any_forced_fallback_fog)
	{
		static const float preset_landscape_mix[] = {0.20f, 0.55f, 0.35f, 0.40f};
		const int preset = std::max(0, std::min(3,
			static_cast<int>(Get_OGL_ConfigureData().ForceFogWeatherPreset)));
		const float global_fog_blend =
			Get_OGL_ConfigureData().ForceFogMediaRelative ?
				1.0f - media_fog_enabled : 1.0f;
		fogMix = preset_landscape_mix[preset] * global_fog_blend;
	}

	const float virtual_yaw = view->virtual_yaw * FixedAngleToRadians;
	const float virtual_pitch = view->virtual_pitch * FixedAngleToRadians;

	Shader* landscape_shaders[] = {
		Shader::get(Shader::S_Landscape),
		Shader::get(Shader::S_LandscapeBloom),
		Shader::get(Shader::S_LandscapeInfravision),
		Shader::get(Shader::S_LandscapeSphere),
		Shader::get(Shader::S_LandscapeSphereBloom),
		Shader::get(Shader::S_LandscapeSphereInfravision)
	};

	for (auto s : landscape_shaders) {
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
		s->setFloat(Shader::U_FogMix, fogMix);
		sprintathon_set_visible_sun(s,view);
		s->setFloat(Shader::U_Yaw, virtual_yaw);
		s->setFloat(Shader::U_Pitch, view->mimic_sw_perspective ? 0.0 : virtual_pitch);
	}

	Shader* fog_mode_shaders[] = {
		Shader::get(Shader::S_Bump),
		Shader::get(Shader::S_BumpBloom),
		Shader::get(Shader::S_Invincible),
		Shader::get(Shader::S_InvincibleBloom),
		Shader::get(Shader::S_Invisible),
		Shader::get(Shader::S_InvisibleBloom),
		Shader::get(Shader::S_Wall),
		Shader::get(Shader::S_WallBloom),
		Shader::get(Shader::S_WallInfravision),
		Shader::get(Shader::S_Sprite),
		Shader::get(Shader::S_SpriteBloom),
		Shader::get(Shader::S_SpriteInfravision),
		Shader::get(Shader::S_SpriteShadow),
        Shader::get(Shader::S_WaterSpriteReflection)
	};
	
	for (auto s : fog_mode_shaders) {
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
		s->setFloat(Shader::U_FogMode, fogmode);
		s->setFloat(Shader::U_MediaFogEnabled, media_fog_enabled);
		s->setFloat(Shader::U_MediaFogTop, media_fog_top);
		s->setFloat(Shader::U_MediaFogSoftness, media_fog_softness);
	}
	
	Shader::disable();

    SprintathonGpuProfileFrame gpu_profile;
    gpu_profile.pass(0); startup_frame.pass(1);
    sprintathon_world_detail.begin();
    SprintathonOpaqueWorld::fogSettings[0]=fogmode;
    SprintathonOpaqueWorld::fogSettings[1]=media_fog_enabled;
    SprintathonOpaqueWorld::fogSettings[2]=media_fog_top;
    SprintathonOpaqueWorld::fogSettings[3]=media_fog_softness;
    SprintathonOpaqueWorld::playerLight=selfLuminosity+weaponFlare;
	render_world_diffuse();
    sprintathon_world_detail.end();
    gpu_profile.pass(1); startup_frame.pass(2);

	const GLsizei framebuffer_width =
		SprintathonRenderPixels(view->screen_width);
	const GLsizei framebuffer_height =
		SprintathonRenderPixels(view->screen_height);
	GLuint scene_depth = 0;
	GLuint ambient_occlusion_color = 0;
	const OGL_ConfigureData& ogl_config = Get_OGL_ConfigureData();
	const bool fog_effect_available = fogdata &&
		(fogdata->IsPresent || any_forced_fallback_fog) && fogmode >= 0.0f;
	const bool fog_haze_enabled = fog_effect_available &&
		ogl_config.DeepFogHaze;
	const bool rising_fog_clouds_enabled = fog_haze_enabled &&
		ogl_config.ForceFogAnimatedDensity;
    const bool volume_fog_enabled = graphics_preferences->sprintathon_volume_fog &&
        fog_effect_available && TEST_FLAG(ogl_config.Flags,OGL_Flag_Fog) && !view->under_media_boundary;
	const bool fog_post_effects_enabled = passTest.other() &&
        (fog_haze_enabled || rising_fog_clouds_enabled || volume_fog_enabled);
	const bool shaft_source_ready = passTest.shafts() && ogl_config.LandscapeLightShafts &&
		sprintathon_begin_shaft_source(framebuffer_width, framebuffer_height,
			scene_projection);
	if (shaft_source_ready)
	{
		render_world_diffuse();
		sprintathon_end_shaft_source();
	}
    gpu_profile.pass(2); startup_frame.pass(3);
    if(passTest.overlays()) {
        sprintathon_draw_fog_coronas(view);
        sprintathon_draw_fog_cones(view);
        sprintathon_draw_flare_smoke(view);
        sprintathon_draw_flare_stars(view);
    }
    gpu_profile.pass(3); startup_frame.pass(4);
	if ((passTest.other() && ogl_config.AmbientOcclusion) || (passTest.shafts() && ogl_config.LandscapeLightShafts) ||
		(passTest.other() && ogl_config.AnamorphicLensFlares) ||
		fog_post_effects_enabled)
		scene_depth = sprintathon_capture_scene_depth(
			framebuffer_width, framebuffer_height);
    gpu_profile.pass(4); startup_frame.pass(5);
	if ((passTest.other() && ogl_config.AmbientOcclusion))
	{
		// Capture only world geometry. The first-person layer is composited after
		// AO, so the weapon can never receive screen-space shading.
		ambient_occlusion_color = sprintathon_capture_scene_color(
			framebuffer_width, framebuffer_height);
		sprintathon_draw_ambient_occlusion(ambient_occlusion_color,
			scene_depth, framebuffer_width, framebuffer_height,
			scene_projection,
			ogl_config.AmbientOcclusionStrength / 100.0f,
			fogmode);
	}
    gpu_profile.pass(5); startup_frame.pass(6);
	if ((passTest.other() && ogl_config.AnamorphicLensFlares) && ogl_config.AnamorphicLensFlareStrength > 0)
	{
		// Detect emitters before deep-haze and drifting-cloud post effects. Fog
		// added by those passes can therefore cover a flare, but never create one.
		GLuint flare_color = sprintathon_capture_scene_color(
			framebuffer_width, framebuffer_height);
		sprintathon_draw_anamorphic_lens_flare(flare_color, scene_depth,
			framebuffer_width, framebuffer_height, scene_projection,
			ogl_config.AnamorphicLensFlareStrength / 100.0f, fogmode);
	}
    gpu_profile.pass(6); startup_frame.pass(7);
	if (fog_post_effects_enabled)
	{
		GLuint fog_haze_color = sprintathon_capture_scene_color(
			framebuffer_width, framebuffer_height);
        const bool volume_drawn = volume_fog_enabled && sprintathon_draw_volume_fog(view,
            fog_haze_color,scene_depth,framebuffer_width,framebuffer_height,scene_projection,volume_modelview);
        if (!volume_drawn)
		sprintathon_draw_fog_haze(fog_haze_color, scene_depth,
			framebuffer_width, framebuffer_height, scene_projection,
			fogmode, 1.0f - media_fog_enabled, fog_haze_enabled,
			rising_fog_clouds_enabled,
			ogl_config.DriftingFogIntensity / 100.0f);
	}
    gpu_profile.pass(7); startup_frame.pass(8);
	if (passTest.shafts() && ogl_config.LandscapeLightShafts && shaft_source_ready)
	{
		// Recapture after AO so the shaft pass preserves its shaded world image.
		GLuint shaft_color = sprintathon_capture_scene_color(
			framebuffer_width, framebuffer_height);
		sprintathon_draw_landscape_light_shafts(shaft_color, scene_depth,
			sprintathon_shaft_source.color, sprintathon_shaft_source.depth,
			framebuffer_width, framebuffer_height,
			sprintathon_shaft_source.width, sprintathon_shaft_source.height,
			scene_projection, volume_modelview,
			virtual_yaw, virtual_pitch,
			float(SprintathonLuaSun::get(SprintathonLuaSun::Shafts)),
			ogl_config.LandscapeLightShaftLength / 100.0f,
			(SprintathonLuaSun::get(SprintathonLuaSun::Azimuth) - 90.0f) *
				0.017453292519943295f,
			SprintathonLuaSun::get(SprintathonLuaSun::Elevation) * 0.017453292519943295f);
	}
	// Draw the view weapon after world-only AO but before other whole-scene
	// effects, preserving underwater refraction and bloom behavior.
    gpu_profile.pass(8); startup_frame.pass(9);
	render_viewer_sprite_layer(kDiffuse);
    gpu_profile.pass(9); startup_frame.pass(10);

	if (passTest.bloom() && current_player->infravision_duration == 0 &&
		TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Blur) &&
		blur.get())
	{
		prepare_world_frustum();
		blur->begin();
        replay_surface_commands(kGlow);
		RenderRasterizerClass::render_tree(kGlow);
                render_viewer_sprite_layer(kGlow);
		blur->end();
		RasPtr->swapper->deactivate();
		gpu_profile.pass(10); startup_frame.pass(11);
		blur->draw(*RasPtr->swapper);
		RasPtr->swapper->activate();
	}

    else { gpu_profile.pass(10); startup_frame.pass(11); }
    gpu_profile.pass(11); startup_frame.pass(12);

	// Refract the completed 3D view while submerged. This runs before the HUD
	// is composited, so interface text and meters remain crisp.
	if (passTest.other() && Get_OGL_ConfigureData().UnderwaterDistortion &&
		view->under_media_boundary && view->origin_polygon_index != NONE)
	{
		polygon_data *polygon = get_polygon_data(view->origin_polygon_index);
		if (polygon && polygon->media_index != NONE)
		{
			media_data *media = get_media_data(polygon->media_index);
			if (media)
			{
				Shader *underwater = Shader::get(Shader::S_UnderwaterRipple);
				// Finish the scene target before filtering it. At this point the
				// freshly rendered frame is still the swapper's draw target;
				// filter() samples current_contents(), which otherwise refers to
				// the previous (often black) buffer.
				RasPtr->swapper->swap();
				underwater->enable();
				underwater->setFloat(Shader::U_Time,
					sprintathon_underwater_phase(media->type));
				underwater->setFloat(Shader::U_PixelWidth,
					SprintathonRenderPixels(view->screen_width));
				underwater->setFloat(Shader::U_PixelHeight,
					SprintathonRenderPixels(view->screen_height));
				RasPtr->swapper->filter(false);
				Shader::disable();

				// Preserve the orientation expected by Rasterizer_Shader::End().
				// This unfiltered copy makes both sides contain the refracted
				// frame; End() can perform its normal final swap and presentation.
				RasPtr->swapper->filter(false);
			}
		}
	}

	glAlphaFunc(GL_GREATER, 0.5);
    gpu_profile.pass(12); startup_frame.pass(13);
}

// Only opaque world surfaces change order. Texture-light collection still
// runs in the original traversal, as do sprites, liquids and blended surfaces.
// Independent CPU wall-clock measurement. It includes driver calls/waits;
// GPU elapsed queries may overlap it and must not be added to this value.
struct SprintathonWorldSubmissionOverlay {
    using Clock=std::chrono::steady_clock;
    bool active;
    Clock::time_point start;
    explicit SprintathonWorldSubmissionOverlay(bool active):active(active){if(active)start=Clock::now();}
    ~SprintathonWorldSubmissionOverlay(){
        if(!active)return;
        static double sum=0;
        static unsigned count=0;
        sum+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        ++count;
        if(count>=30 || !sprintathon_cpu_scene_timings.world_ready){
            sprintathon_cpu_scene_timings.world_submission_ms=sum/count;
            sprintathon_cpu_scene_timings.world_ready=true;
            sum=0;count=0;
        }
    }
};
void RenderRasterize_Shader::render_world_diffuse()
{
    SprintathonPortalWorld portalWorld(!sprintathon_fast_shaft_source());
    SprintathonObjectCapture objectCapture(!sprintathon_fast_shaft_source());
    SprintathonSetupCapture setupCapture(!sprintathon_fast_shaft_source());
    SprintathonWorldSubmissionOverlay submissionOverlay(!sprintathon_fast_shaft_source());
    if (sprintathon_fast_shaft_source()) {
        // Dedicated sky/occlusion traversal: one visit per visible surface.
        // Geometry, animation and portal clipping are shared; material setup
        // selects the minimal mask for solids and the landscape shader for sky.
        prepare_world_frustum();
        world_surface_pass=WorldSurfacePass::all;
        replay_surface_commands(kDiffuse);
        RenderRasterizerClass::render_tree(kDiffuse);
        return;
    }
    SprintathonInvisibilityView invisibilityView(!sprintathon_shaft_source.active);
    if (sprintathon_all_scenery_count > 0) {
        glActiveTextureARB(GL_TEXTURE3_ARB);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, sprintathon_scenery_light_texture);
        glActiveTextureARB(GL_TEXTURE0_ARB);
    }

    prepare_world_frustum();
    const char *override_order = std::getenv("SPRINTATHON_OPAQUE_FIRST");
    if (!graphics_preferences->projectile_lights_per_pixel ||
        (override_order && override_order[0] == '0')) {
        RenderRasterizerClass::render_tree(kDiffuse);
        return;
    }
    const bool see_through_liquids =
        TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_LiqSeeThru);
    world_surface_pass = WorldSurfacePass::opaque;
    SprintathonOpaqueWorld::outerLeft=leftmost_clip;SprintathonOpaqueWorld::outerRight=rightmost_clip;
    const bool opaqueWorld=!current_player->infravision_duration && SprintathonOpaqueWorld::begin();
    if(opaqueWorld)SprintathonWorldSplit::begin();
    if (!opaqueWorld && !current_player->infravision_duration) SprintathonOpaqueBatch::begin();
    for (auto node = RSPtr->SortedNodes.rbegin(); node != RSPtr->SortedNodes.rend(); ++node)
        render_node(&*node, see_through_liquids, kDiffuse);
    if(opaqueWorld)SprintathonWorldSplit::mark(1);
    SprintathonOpaqueWorld::finish([](){sprintathon_world_detail.category=0;sprintathon_world_detail.draw();});
    if(opaqueWorld)SprintathonWorldSplit::mark(2);
    flush_opaque_batch();
    SprintathonOpaqueBatch::end();
    world_surface_pass = WorldSurfacePass::remaining;
    RenderRasterizerClass::render_tree(kDiffuse);
    world_surface_pass = WorldSurfacePass::all;
    if(opaqueWorld)SprintathonWorldSplit::mark(3);
}

bool RenderRasterize_Shader::skip_world_surface(bool opaque)
{
    const bool skip = (world_surface_pass == WorldSurfacePass::opaque && !opaque) ||
        (world_surface_pass == WorldSurfacePass::remaining && opaque);
    if (skip) reset_skipped_world_surface();
    return skip;
}

void RenderRasterize_Shader::reset_skipped_world_surface()
{
    // setupWallTexture may have enabled a shader and changed texture matrices.
    Shader::disable();
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
}

void RenderRasterize_Shader::prepare_world_frustum()
{
    const char *override_lights = std::getenv("SPRINTATHON_SURFACE_LIGHT_BOUNDS");
    surface_light_bounds_active = !override_lights || override_lights[0] != '0';
    const char *override_culling = std::getenv("SPRINTATHON_PORTAL_FRUSTUM_CULL");
    world_frustum_active = RSPtr && RSPtr->RVPtr &&
        RSPtr->RVPtr->conservative_full_circle &&
        (!override_culling || override_culling[0] != '0');
    if (!world_frustum_active) return;

    GLfloat projection[16], modelview[16];
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);
    double clip[16] = {};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                clip[column*4 + row] += double(projection[k*4 + row]) *
                    modelview[column*4 + k];
    // Side planes only: the landscape shader changes depth, and the wall
    // shader offsets it. Neither changes the angular projection. Capture
    // again for the overscanned shaft-source pass instead of reusing main-view planes.
    for (int plane = 0; plane < 4; ++plane) {
        const int row = plane / 2;
        const double sign = (plane & 1) ? -1.0 : 1.0;
        double coefficients[4];
        for (int i = 0; i < 4; ++i)
            coefficients[i] = clip[i*4 + 3] + sign * clip[i*4 + row];
        const double length = std::sqrt(coefficients[0]*coefficients[0] +
            coefficients[1]*coefficients[1] + coefficients[2]*coefficients[2]);
        if (!std::isfinite(length) || length < 1e-12) {
            world_frustum_active = false;
            return;
        }
        for (int i = 0; i < 4; ++i) {
            world_frustum_planes[plane][i] = static_cast<float>(coefficients[i] / length);
            if (!std::isfinite(world_frustum_planes[plane][i])) {
                world_frustum_active = false;
                return;
            }
        }
    }
}

bool RenderRasterize_Shader::world_bounds_outside(float x0, float y0, float z0,
                                                 float x1, float y1, float z1) const
{
    if (!world_frustum_active) return false;
    for (const auto& plane : world_frustum_planes) {
        const float maximum_distance = plane[3] +
            plane[0] * (plane[0] >= 0 ? x1 : x0) +
            plane[1] * (plane[1] >= 0 ? y1 : y0) +
            plane[2] * (plane[2] >= 0 ? z1 : z0);
        // A small world-space margin keeps boundary surfaces conservative.
        if (maximum_distance < -32.0f) return true;
    }
    return false;
}

void RenderRasterize_Shader::render_node(sorted_node_data *node, bool SeeThruLiquids, RenderStep renderStep)
{
	// parasitic object detection
    objectCount = 0;
    objectY = 0;

    RenderRasterizerClass::render_node(node, SeeThruLiquids, renderStep);

	// turn off clipping planes
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
}

void RenderRasterize_Shader::apply_opaque_batch_portal(const std::array<int32_t,4>& portal)
{
    clipping_window_data window{};
    window.left.i=portal[0]; window.left.j=portal[1];
    window.right.i=portal[2]; window.right.j=portal[3];
    clip_to_window(&window);
}

void RenderRasterize_Shader::flush_opaque_batch()
{
    SprintathonOpaqueBatch::flush(
        [this](const std::array<int32_t,4>& portal) { apply_opaque_batch_portal(portal); },
        []() { sprintathon_world_detail.category=0; sprintathon_world_detail.draw(); });
}

int RenderRasterize_Shader::queue_opaque_batch(clipping_window_data* window,
    TextureManager* material, GLenum mode, GLsizei count, const GLfloat* vertices,
    const GLfloat* uv, SprintathonOpaqueGeometry::CpuEntry* prepared,
    float intensity, float nx, float ny, float nz, float tx, float ty, float tz, float sign)
{
    if (!SprintathonOpaqueBatch::active) return 0;
    SprintathonOpaqueBatch::State state;
    state.shader=sprintathon_surface_shader(kDiffuse,OGL_Txtr_Wall);
    if (!state.shader || !material->GetOpaqueBatchMaterial(state.texture,state.bump,state.swapped)) {
        ++SprintathonOpaqueBatch::fallback;
        flush_opaque_batch(); return -1;
    }
    if (!TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_BumpMap)) state.bump=0;
    state.uniforms=state.shader->uniformState();
    if (state.uniforms.value[Shader::U_SkyBakeEnabled][0]!=0)
        state.bake=SprintathonSkyBake::batchBoundTexture;
    state.mode=mode;
    state.attributes={{intensity,intensity,intensity,1,nx,ny,nz,tx,ty,tz,sign}};
    state.portal={{window->left.i,window->left.j,window->right.i,window->right.j}};
    size_t offset=prepared ? prepared->gpuOffset : size_t(-1);
    if (!state.valid() ||
        !((prepared && SprintathonOpaqueGeometry::bindPrepared(*prepared)) ||
          SprintathonOpaqueGeometry::bind(count,vertices,uv,&offset))) {
        ++SprintathonOpaqueBatch::fallback;
        flush_opaque_batch(); return -1;
    }
    if (prepared) prepared->gpuOffset=offset;
    SprintathonOpaqueBatch::submit(state,offset,count,
        [this](const std::array<int32_t,4>& portal) { apply_opaque_batch_portal(portal); },
        []() { sprintathon_world_detail.category=0; sprintathon_world_detail.draw(); });
    SprintathonOpaqueGeometry::restore(vertices,uv);
    return 1;
}

void RenderRasterize_Shader::clip_to_window(clipping_window_data *win)
{
    SprintathonSetupScope setupTime(SprintathonSetupTrace::Clip);
    SprintathonShaftCpuScope trace_time(sprintathon_shaft_trace.clip_ms);
    if(sprintathon_shaft_trace.enabled)++sprintathon_shaft_trace.clips;
    GLdouble clip[] = { 0., 0., 0., 0. };
        
    // recenter to player's orientation temporarily
    glPushMatrix();
    glTranslatef(view->origin.x, view->origin.y, 0.);
    glRotatef(view->yaw * (360/float(FULL_CIRCLE)) + 90., 0., 0., 1.);
    
    glRotatef(-0.1, 0., 0., 1.); // leave some excess to avoid artifacts at edges
	if (win->left.i != leftmost_clip.i || win->left.j != leftmost_clip.j) {
		clip[0] = win->left.i;
		clip[1] = win->left.j;
		glEnable(GL_CLIP_PLANE0);
		glClipPlane(GL_CLIP_PLANE0, clip);
	} else {
		glDisable(GL_CLIP_PLANE0);
	}
	
    glRotatef(0.2, 0., 0., 1.); // breathing room for right-hand clip
	if (win->right.i != rightmost_clip.i || win->right.j != rightmost_clip.j) {
		clip[0] = win->right.i;
		clip[1] = win->right.j;
		glEnable(GL_CLIP_PLANE1);
		glClipPlane(GL_CLIP_PLANE1, clip);
	} else {
		glDisable(GL_CLIP_PLANE1);
	}
    
    glPopMatrix();
}

void RenderRasterize_Shader::store_endpoint(
	endpoint_data *endpoint,
	long_vector2d& p)
{
	p.i = endpoint->vertex.x;
	p.j = endpoint->vertex.y;
}

std::unique_ptr<TextureManager> RenderRasterize_Shader::setupSpriteTexture(const rectangle_definition& rect, short type, float offset, RenderStep renderStep) {
    SprintathonObjectOps::Scope opsState(SprintathonObjectOps::ShaderState);
    SprintathonSetupScope setupTime(SprintathonSetupTrace::SpriteTexture);
    SprintathonShaftCpuScope trace_time(sprintathon_shaft_trace.sprite_ms);
    if(sprintathon_shaft_trace.enabled) {
        ++sprintathon_shaft_trace.sprite_setups;
        sprintathon_shaft_trace.material=0;
    }

	Shader *s = NULL;
	GLfloat color[3];
	GLdouble shade = PIN(static_cast<GLfloat>(rect.ambient_shade)/static_cast<GLfloat>(FIXED_ONE),0,1);
	color[0] = color[1] = color[2] = shade;

	auto TMgr = std::make_unique<TextureManager>();

	TMgr->ShapeDesc = rect.ShapeDesc;
	TMgr->LowLevelShape = rect.LowLevelShape;
	TMgr->ShadingTables = rect.shading_tables;
	TMgr->Texture = rect.texture;
	TMgr->TransferMode = rect.transfer_mode;
	TMgr->TransferData = rect.transfer_data;
	TMgr->IsShadeless = (rect.flags&_SHADELESS_BIT) != 0;
	TMgr->TextureType = type;

	if (current_player->infravision_duration) {
		struct bitmap_definition* dummy;
		// grab the normal shading tables, since the shader does the tinting
		extended_get_shape_bitmap_and_shading_table(GET_DESCRIPTOR_COLLECTION(TMgr->ShapeDesc), TMgr->LowLevelShape, &dummy, &TMgr->ShadingTables, _shading_normal);
	}

    if (sprintathon_fast_shaft_source() && renderStep == kDiffuse) {
        if (!SprintathonObjectOps::measure(SprintathonObjectOps::TextureSetup, [&](){return TMgr->Setup();})) { TMgr->ShapeDesc=UNONE; return TMgr; }
        SprintathonObjectOps::measure(SprintathonObjectOps::TextureBind, [&](){return TMgr->RenderNormal();});
        SprintathonObjectOps::measure(SprintathonObjectOps::TextureBind, [&](){return TMgr->SetupTextureMatrix();});
        Shader* mask=Shader::get(Shader::S_ShaftOcclusion);
        mask->enable();
        sprintathon_trace_mask_bound();
        mask->setFloat(Shader::U_Depth,0.0f);
        mask->setFloat(Shader::U_Pulsate,0.0f);
        mask->setFloat(Shader::U_Wobble,0.0f);
        mask->setFloat(Shader::U_Visibility,TMgr->TransferMode == _tinted_transfer ?
            std::max(0.0f,std::min(1.0f,1.0f-rect.transfer_data/32.0f)) : 1.0f);
        glEnable(GL_TEXTURE_2D); glColor4f(1,1,1,1);
        return TMgr;
    }

	float flare = weaponFlare;

	glEnable(GL_TEXTURE_2D);

	// priorities: static, infravision, tinted/solid, shadeless
	if (TMgr->TransferMode == _static_transfer) {
		TMgr->IsShadeless = 1;
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invincible);
		} else {
			s = Shader::get(Shader::S_InvincibleBloom);
		}
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
        s->setVector4(Shader::U_SkyBakeActor,0,0,0,0);
        s->setFloat(Shader::U_TransferFadeOut,((float)((uint16)rect.transfer_data))/(float)((int)FIXED_ONE));
	} else if (current_player->infravision_duration) {
		color[0] = color[1] = color[2] = 1;
		FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(rect.ShapeDesc)), color);
		s = Shader::get(Shader::S_SpriteInfravision);
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
        s->setVector4(Shader::U_SkyBakeActor,0,0,0,0);
	} else if (TMgr->TransferMode == _tinted_transfer) {
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invisible);
		} else {
			s = Shader::get(Shader::S_InvisibleBloom);
		}
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
        s->setVector4(Shader::U_SkyBakeActor,0,0,0,0);
		const float visibility = 1.0f - rect.transfer_data / 32.0f;
		if (renderStep == kDiffuse &&
            (Get_OGL_ConfigureData().RefractiveInvisibility ||
             (shell_options.zpc && GET_DESCRIPTOR_COLLECTION(rect.ShapeDesc) ==
              BUILD_COLLECTION(_collection_weapons_in_hand, 1))))
		{
			SprintathonObjectOps::measure(SprintathonObjectOps::Refraction, [&](){return setup_invisibility_refraction(s,
				static_cast<GLsizei>(SprintathonRenderPixels(view->screen_width)),
				static_cast<GLsizei>(SprintathonRenderPixels(view->screen_height)),
				visibility);});
		}
		else if (renderStep == kDiffuse)
		{
			setup_classic_invisibility(s, visibility);
		}
		else
		{
			s->setFloat(Shader::U_Visibility, visibility);
		}
	} else if (TMgr->TransferMode == _solid_transfer) {
		// is this ever used?
		color[0] = 0;
		color[1] = 1;
		color[2] = 0;
	} else if (TMgr->TransferMode == _textured_transfer) {
		if (TMgr->IsShadeless) {
			if (renderStep == kDiffuse) {
				color[0] = color[1] = color[2] = 1;
			} else {
				color[0] = color[1] = color[2] = 0;
			}
			flare = -1;
		}
	} else {
		// I've never seen this happen
		color[0] = 0;
		color[1] = 0;
		color[2] = 1;
	}

	if(s == NULL) {
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Sprite);
		} else {
			s = Shader::get(Shader::S_SpriteBloom);
		}
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
        s->setVector4(Shader::U_SkyBakeActor,0,0,0,0);
	}

	if(SprintathonObjectOps::measure(SprintathonObjectOps::TextureSetup, [&](){return TMgr->Setup();})) {
		SprintathonObjectOps::measure(SprintathonObjectOps::TextureBind, [&](){return TMgr->RenderNormal();});
	} else {
		TMgr->ShapeDesc = UNONE;
		return TMgr;
	}

	SprintathonObjectOps::measure(SprintathonObjectOps::TextureBind, [&](){return TMgr->SetupTextureMatrix();});
	s->setVector4(Shader::U_ProjectileBlurVector, 0, 0, 0, 0);
	s->setVector4(Shader::U_FlameRipple, 0, 0, 0, 0);

	if (renderStep == kGlow) {
		s->setFloat(Shader::U_BloomScale, TMgr->BloomScale());
		s->setFloat(Shader::U_BloomShift, TMgr->BloomShift());
	}
	s->setFloat(Shader::U_Flare, flare);
	s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
	s->setFloat(Shader::U_Pulsate, 0);
	s->setFloat(Shader::U_Wobble, 0);
	s->setFloat(Shader::U_Depth, offset);
	s->setFloat(Shader::U_ObjectWorldZ,
		static_cast<float>(rect.Position.z));
	const bool sprintathon_strict_sprite_depth =
		!view->mimic_sw_perspective &&
		input_preferences->sprintathon_enabled &&
		input_preferences->sprintathon_mouselook_mode > 0;
	s->setFloat(Shader::U_StrictDepthMode,
		(OGL_ForceSpriteDepth() || sprintathon_strict_sprite_depth) ? 1 : 0);
	s->setFloat(Shader::U_Glow, 0);
    if (s == Shader::get(renderStep == kGlow ? Shader::S_SpriteBloom : Shader::S_Sprite)) {
        if(type==OGL_Txtr_Inhabitant && renderStep==kDiffuse && !sprintathon_shaft_source.active &&
           (sprintathon_object_light_isolation() & SprintathonSkipActorTint))
            s->setVector4(Shader::U_SprintathonLightColor,0,0,0,0);
        else SprintathonObjectOps::measure(SprintathonObjectOps::Lighting, [&](){return sprintathon_set_sprite_light(s, rect, type, view);});
    }
	glColor4f(color[0], color[1], color[2], 1);
	return TMgr;
}

// Circle constants
const double Radian2Circle = 1/TWO_PI;			// A circle is 2*pi radians
const double FullCircleReciprocal = 1/double(FULL_CIRCLE);

// Resolve and sample the level landscape once, after texture preparation.
static void sprintathon_prepare_liquid_palette()
{
    auto& palette=sprintathon_liquid_palette;
    if(palette.ready && palette.level==dynamic_world->current_level_number &&
       palette.polygons==dynamic_world->polygon_count)return;
    palette=SprintathonLiquidPalette{};
    palette.ready=true;palette.level=dynamic_world->current_level_number;
    palette.polygons=dynamic_world->polygon_count;
    shape_descriptor sky=UNONE;
    for(int i=0;i<dynamic_world->polygon_count;++i) {
        const auto* polygon=get_polygon_data(i);
        if(SprintathonSkyBake::sky(polygon->ceiling_transfer_mode) && polygon->ceiling_texture!=UNONE) {
            sky=polygon->ceiling_texture;break;
        }
        if(SprintathonSkyBake::sky(polygon->floor_transfer_mode) && polygon->floor_texture!=UNONE)
            sky=polygon->floor_texture;
    }
    if(sky==UNONE)return;
    TextureManager landscape;
    landscape.ShapeDesc=sky;
    get_shape_bitmap_and_shading_table(sky,&landscape.Texture,&landscape.ShadingTables,_shading_normal);
    landscape.TextureType=OGL_Txtr_Landscape;
    landscape.TransferMode=_big_landscaped_transfer;
    landscape.TransferData=0;landscape.IsShadeless=false;
    const auto* options=View_GetLandscapeOptions(sky);
    landscape.LandscapeVertRepeat=options->VertRepeat;
    landscape.Landscape_AspRatExp=options->SphereMap ? 1 : options->OGL_AspRatExp;
    glActiveTextureARB(GL_TEXTURE0_ARB+5);
    if(landscape.Setup()) {
        landscape.RenderNormal();
        float sunlight[3];
        palette.valid=landscape.GetSkyColor(palette.colour,sunlight);
    }
    glActiveTextureARB(GL_TEXTURE0_ARB);
}

static void sprintathon_bind_liquid_sky(Shader* shader)
{
    // Fallback for a level transition outside the normal loading path.
    sprintathon_prepare_liquid_palette();
    const auto& palette=sprintathon_liquid_palette;
    if(palette.valid) {
        shader->setVector4(Shader::U_LiquidSkyParams,
            palette.colour[0],palette.colour[1],palette.colour[2],1);
        shader->setFloat(Shader::U_LiquidSkyMode,1);
    }
}

// Track the actual displayed texture origin, not an invented current.
static void sprintathon_set_liquid_flow(Shader* shader,short mediaIndex,float x,float y)
{
    if(!shader || mediaIndex==NONE)return;
    struct Flow { bool ready=false; float x=0,y=0; double u=0,v=0; };
    static std::map<short,Flow> flows;
    static int level=-1,tick=-1;
    if(level!=dynamic_world->current_level_number || dynamic_world->tick_count<tick) flows.clear();
    level=dynamic_world->current_level_number;tick=dynamic_world->tick_count;
    auto& flow=flows[mediaIndex];
    if(!flow.ready) { flow.u=x;flow.v=y;flow.ready=true; }
    else {
        // Texture origins wrap by one world unit. Follow the nearest continuous
        // displacement so wrapping doesn't reset the procedural wave pattern.
        double dx=x-flow.x,dy=y-flow.y;
        dx-=std::round(dx/WORLD_ONE)*WORLD_ONE;
        dy-=std::round(dy/WORLD_ONE)*WORLD_ONE;
        flow.u+=dx;flow.v+=dy;
    }
    flow.x=x;flow.y=y;
    shader->setVector4(Shader::U_LiquidFlow,float(flow.u),float(flow.v),0,0);
}

std::unique_ptr<TextureManager> RenderRasterize_Shader::setupWallTexture(const shape_descriptor& Texture, short transferMode, float pulsate, float wobble, float intensity, float offset, RenderStep renderStep, int16 mediaType) {
    SprintathonSetupScope setupTime(SprintathonSetupTrace::WallTexture);
    SprintathonShaftCpuScope trace_time(sprintathon_shaft_trace.wall_ms);
    if(sprintathon_shaft_trace.enabled) {
        ++sprintathon_shaft_trace.wall_setups;
        sprintathon_shaft_trace.material=(transferMode==_xfer_landscape || transferMode==_xfer_big_landscape) ? 2 : 0;
    }

	Shader *s = NULL;

    const bool resolved=renderStep==kDiffuse && sprintathon_resolved_wall && bool(*sprintathon_resolved_wall);
    auto TMgr=resolved?std::move(*sprintathon_resolved_wall):std::make_unique<TextureManager>();
    if(resolved)++SprintathonMaterialTrace::get().bound;
	LandscapeOptions *opts = NULL;
	TMgr->ShapeDesc = Texture;
	if (TMgr->ShapeDesc == UNONE) { return TMgr; }
	if(!resolved)get_shape_bitmap_and_shading_table(Texture, &TMgr->Texture, &TMgr->ShadingTables, _shading_normal);

	TMgr->TransferMode = _textured_transfer;
	TMgr->IsShadeless = current_player->infravision_duration ? 1 : 0;
	TMgr->TransferData = 0;

    if (sprintathon_fast_shaft_source() && renderStep == kDiffuse &&
        transferMode != _xfer_landscape && transferMode != _xfer_big_landscape) {
        TMgr->TextureType=OGL_Txtr_Wall;
        if (transferMode == _xfer_static) TMgr->TransferMode=_static_transfer;
        if (!TMgr->Setup()) { TMgr->ShapeDesc=UNONE; return TMgr; }
        TMgr->RenderNormal();
        TMgr->SetupTextureMatrix();
        Shader* mask=Shader::get(Shader::S_ShaftOcclusion);
        mask->enable();
        sprintathon_trace_mask_bound();
        mask->setFloat(Shader::U_Depth,offset);
        mask->setFloat(Shader::U_Pulsate,pulsate);
        mask->setFloat(Shader::U_Wobble,wobble);
        mask->setFloat(Shader::U_Visibility,1.0f);
        glEnable(GL_TEXTURE_2D); glColor4f(1,1,1,1);
        return TMgr;
    }

	float flare = weaponFlare;

	glEnable(GL_TEXTURE_2D);
	glColor4f(intensity, intensity, intensity, 1.0);

	switch(transferMode) {
		case _xfer_static:
			TMgr->TextureType = OGL_Txtr_Wall;
			TMgr->TransferMode = _static_transfer;
			TMgr->IsShadeless = 1;
			flare = -1;
			s = Shader::get(renderStep == kGlow ? Shader::S_InvincibleBloom : Shader::S_Invincible);
			s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
            s->setFloat(Shader::U_TransferFadeOut,0);
			break;
		case _xfer_landscape:
		case _xfer_big_landscape:
			TMgr->TextureType = OGL_Txtr_Landscape;
			TMgr->TransferMode = _big_landscaped_transfer;
			opts = View_GetLandscapeOptions(Texture);
			TMgr->LandscapeVertRepeat = opts->VertRepeat;
			TMgr->Landscape_AspRatExp = opts->SphereMap ? 1 : opts->OGL_AspRatExp;
			if (current_player->infravision_duration) {
				GLfloat color[3] {1, 1, 1};
				FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(Texture)), color);
				glColor4f(color[0], color[1], color[2], 1);
				if (opts->SphereMap)
				{
					s = Shader::get(Shader::S_LandscapeSphereInfravision);
				}
				else
				{
					s = Shader::get(Shader::S_LandscapeInfravision);
				}
			} else {
				if (opts->SphereMap)
				{
					if (renderStep == kDiffuse)
					{
						s = Shader::get(Shader::S_LandscapeSphere);
					}
					else
					{
						s = Shader::get(Shader::S_LandscapeSphereBloom);
					}
				}
				else
				{
					if (renderStep == kDiffuse) {
						s = Shader::get(Shader::S_Landscape);
					} else {
						s = Shader::get(Shader::S_LandscapeBloom);
					}
				}
			}
			s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
			break;
		default:
			TMgr->TextureType = OGL_Txtr_Wall;
			if(TMgr->IsShadeless) {
				if (renderStep == kDiffuse) {
					glColor4f(1,1,1,1);
				} else {
					glColor4f(0,0,0,1);
				}
				flare = -1;
			}
	}

	if(s == NULL) {
		if (current_player->infravision_duration) {
			GLfloat color[3] {1, 1, 1};
			FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(Texture)), color);
			glColor4f(color[0], color[1], color[2], 1);
			s = Shader::get(Shader::S_WallInfravision);
		} else if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap) && !sprintathon_fast_shaft_source()) {
			s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
		}
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
	}

    s->setFloat(Shader::U_LiquidSkyMode,0);
    if(renderStep==kDiffuse && mediaType!=NONE && mediaType!=_media_lava &&
       graphics_preferences->liquid_specular && !current_player->infravision_duration) {
        sprintathon_bind_liquid_sky(s);
        // Sky colour is a uniform; unit 5 can hold the cached wave normals.
        SprintathonWaterNormals::bind();
    }

	if(resolved || TMgr->Setup()) {
		TMgr->RenderNormal(); // must allocate first
        if (SprintathonSkyBake::enabled() && TMgr->TextureType == OGL_Txtr_Landscape &&
            renderStep == kDiffuse && !IsInfravisionActive()) {
            float ambient[3], sunlight[3];
            if (TMgr->GetSkyColor(ambient, sunlight)) SprintathonSkyBake::landscapeColor(ambient, sunlight);
        }
		if (TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap) && !sprintathon_fast_shaft_source()) {
			glActiveTextureARB(GL_TEXTURE1_ARB);
			TMgr->RenderBump();
			glActiveTextureARB(GL_TEXTURE0_ARB);
		}
	} else {
		TMgr->ShapeDesc = UNONE;
		return TMgr;
	}

    // Reset for every material, including non-emitting textures and media.
    float emitterShade = 1.0f;
    if (TMgr->TextureType == OGL_Txtr_Wall && mediaType == NONE &&
        !current_player->infravision_duration &&
        graphics_preferences->projectile_lights_per_pixel &&
        graphics_preferences->bright_texture_lights) {
        float u, v, rgb[3];
        if (TMgr->GetBrightEmission(u, v, rgb))
            emitterShade = std::max(0.0f, std::min(1.0f, intensity));
    }
    s->setFloat(Shader::U_EmitterShade, emitterShade);

	// The shaft filter reads colour only at landscape depth. Other walls
	// need their geometry and texture alpha, not surface lighting. Keep the
	// existing blend/alpha/depth state for grates and transparent surfaces.
	// Media retains its refraction path; landscapes retain their colour.
	const bool fast_shaft_source = sprintathon_fast_shaft_source() &&
		TMgr->TextureType == OGL_Txtr_Wall && mediaType == NONE &&
		renderStep == kDiffuse;
	s->setFloat(Shader::U_SprintathonShaftSource, fast_shaft_source ? 1.0f : 0.0f);
    const char *detail_override = std::getenv("SPRINTATHON_DISTANT_SURFACE_DETAIL");
    const bool simplify_distant = graphics_preferences->simplify_distant_surfaces &&
        (!detail_override || detail_override[0] != '0');
    s->setFloat(Shader::U_SprintathonDistantSurfaceDetail, simplify_distant ? 1.0f : 0.0f);


	TMgr->SetupTextureMatrix();
	const OGL_ConfigureData& config = Get_OGL_ConfigureData();
	const float ripple_phase = sprintathon_liquid_ripple_phase(mediaType);
    sprintathon_set_visible_sun(s,view);
    s->setVector4(Shader::U_LiquidFlow,0,0,0,0);
    s->setVector4(Shader::U_WaterSceneryColor0,0,0,0,0);
    s->setVector4(Shader::U_WaterSceneryColor1,0,0,0,0);
    s->setFloat(Shader::U_LiquidSceneAvailable,0);
    s->setVector4(Shader::U_LiquidSpecLight,0,0,1,0);
    if (renderStep == kDiffuse && mediaType != NONE && mediaType != _media_lava &&
        graphics_preferences->liquid_specular && !current_player->infravision_duration) {
        const double azimuth=(SprintathonLuaSun::get(SprintathonLuaSun::Azimuth)-90.0)*0.017453292519943295;
        const double elevation=SprintathonLuaSun::get(SprintathonLuaSun::Elevation)*0.017453292519943295;
        s->setVector4(Shader::U_LiquidSpecLight,
            std::cos(azimuth)*std::cos(elevation),std::sin(azimuth)*std::cos(elevation),
            std::sin(elevation),graphics_preferences->liquid_specular_strength/100.0f);
        // W carries continuous wave time; it must not reset with texture ripple phase.
        s->setVector4(Shader::U_LiquidSpecEye,view->origin.x,view->origin.y,view->origin.z,
            sprintathon_liquid_ripple_phase(mediaType,1.0,false));
    }
	s->setFloat(Shader::U_Time, ripple_phase);
	s->setFloat(Shader::U_PixelWidth,
		SprintathonRenderPixels(view->screen_width));
	s->setFloat(Shader::U_PixelHeight,
		SprintathonRenderPixels(view->screen_height));
	s->setFloat(Shader::U_MediaRipple,
		mediaType != NONE && config.AnimatedMediaRipples ?
			(static_cast<float>(config.AnimatedMediaRippleStrength) + 1.0f) *
				0.25f : 0.0f);
	s->setFloat(Shader::U_MediaWetness,
		mediaType != NONE && config.AnimatedMediaRipples ?
			static_cast<float>(config.AnimatedMediaWetTextureStrength) *
				0.25f : 0.0f);

	// Capture the scene already drawn behind a transparent media surface.
	// Diffuse media shaders sample it from texture unit 2 and bend that sample
	// with the same wave field used to animate the liquid texture.
	if (mediaType != NONE && renderStep == kDiffuse &&
        (config.AnimatedMediaRipples || (graphics_preferences->liquid_specular && mediaType!=_media_lava)) &&
		TEST_FLAG(config.Flags, OGL_Flag_LiqSeeThru))
	{
		const GLsizei width = static_cast<GLsizei>(
			SprintathonRenderPixels(view->screen_width));
		const GLsizei height = static_cast<GLsizei>(
			SprintathonRenderPixels(view->screen_height));

        sprintathon_prepare_media_scene(width,height);
        s->setFloat(Shader::U_LiquidSceneAvailable,1);
        // The traversal interleaves media with geometry behind each surface.
        // Capture at this draw, after that geometry has become available.
        glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
            0, 0, width, height);
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}
	
	if (TMgr->TextureType == OGL_Txtr_Landscape && opts) {
		if (opts->SphereMap)
		{
			s->setFloat(Shader::U_OffsetX, opts->Azimuth * TWO_PI * FullCircleReciprocal);
		}
		else
		{
			double TexScale = std::abs(TMgr->U_Scale);
			double HorizScale = double(1 << opts->HorizExp);
			s->setFloat(Shader::U_ScaleX, HorizScale * (npotTextures ? 1.0 : TexScale) * Radian2Circle);
			s->setFloat(Shader::U_OffsetX, HorizScale * (0.25 + opts->Azimuth * FullCircleReciprocal));
			
			short AdjustedVertExp = opts->VertExp + opts->OGL_AspRatExp;
			double VertScale = (AdjustedVertExp >= 0) ? double(1 << AdjustedVertExp)
		                       : 1/double(1 << (-AdjustedVertExp));
			s->setFloat(Shader::U_ScaleY, VertScale * TexScale * Radian2Circle);
			s->setFloat(Shader::U_OffsetY, (0.5 + TMgr->U_Offset) * TexScale);
		}
	}

	if (renderStep == kGlow) {
		if (TMgr->TextureType == OGL_Txtr_Landscape) {
			s->setFloat(Shader::U_BloomScale, TMgr->LandscapeBloom());
		} else {
			s->setFloat(Shader::U_BloomScale, TMgr->BloomScale());
			s->setFloat(Shader::U_BloomShift, TMgr->BloomShift());
		}
	}
	s->setFloat(Shader::U_Flare, flare);
	s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
	s->setFloat(Shader::U_Pulsate, pulsate);
	s->setFloat(Shader::U_Wobble, wobble);
	s->setFloat(Shader::U_Depth, offset);
	s->setFloat(Shader::U_Glow, 0);
	return TMgr;
}

void instantiate_transfer_mode(struct view_data *view, short transfer_mode, world_distance &x0, world_distance &y0) {
	short alternate_transfer_phase;
	short transfer_phase = view->tick_count;

	switch (transfer_mode) {

		case _xfer_fast_horizontal_slide:
		case _xfer_horizontal_slide:
		case _xfer_vertical_slide:
		case _xfer_fast_vertical_slide:
		case _xfer_wander:
		case _xfer_fast_wander:
		case _xfer_reverse_horizontal_slide:
		case _xfer_reverse_fast_horizontal_slide:
		case _xfer_reverse_vertical_slide:
		case _xfer_reverse_fast_vertical_slide:
			x0 = y0= 0;
			switch (transfer_mode) {
				case _xfer_fast_horizontal_slide: transfer_phase<<= 1;
				case _xfer_horizontal_slide: x0= (transfer_phase<<2)&(WORLD_ONE-1); break;

				case _xfer_fast_vertical_slide: transfer_phase<<= 1;
				case _xfer_vertical_slide: y0= (transfer_phase<<2)&(WORLD_ONE-1); break;
				case _xfer_reverse_fast_horizontal_slide: transfer_phase<<= 1;
				case _xfer_reverse_horizontal_slide: x0 = WORLD_ONE - (transfer_phase<<2)&(WORLD_ONE-1); break;
			
		        case _xfer_reverse_fast_vertical_slide: transfer_phase<<= 1;
				case _xfer_reverse_vertical_slide: y0 = WORLD_ONE - (transfer_phase<<2)&(WORLD_ONE-1); break;

				case _xfer_fast_wander: transfer_phase<<= 1;
				case _xfer_wander:
					alternate_transfer_phase= transfer_phase%(10*FULL_CIRCLE);
					transfer_phase= transfer_phase%(6*FULL_CIRCLE);
					x0 = (cosine_table[NORMALIZE_ANGLE(alternate_transfer_phase)] +
						 (cosine_table[NORMALIZE_ANGLE(2*alternate_transfer_phase)]>>1) +
						 (cosine_table[NORMALIZE_ANGLE(5*alternate_transfer_phase)]>>1))>>(WORLD_FRACTIONAL_BITS-TRIG_SHIFT+2);
					y0 = (sine_table[NORMALIZE_ANGLE(transfer_phase)] +
						 (sine_table[NORMALIZE_ANGLE(2*transfer_phase)]>>1) +
						 (sine_table[NORMALIZE_ANGLE(3*transfer_phase)]>>1))>>(WORLD_FRACTIONAL_BITS-TRIG_SHIFT+2);
					break;
			}
			break;
		// wobble is done in the shader
		default:
			break;
	}
}

float calcWobble(short transferMode, short transfer_phase) {
	float wobble = 0;
	switch(transferMode) {
		case _xfer_fast_wobble:
			transfer_phase*= 15;
		case _xfer_pulsate:
		case _xfer_wobble:
			transfer_phase&= WORLD_ONE/16-1;
			transfer_phase= (transfer_phase>=WORLD_ONE/32) ? (WORLD_ONE/32+WORLD_ONE/64 - transfer_phase) : (transfer_phase - WORLD_ONE/64);
			wobble = transfer_phase / 1024.0;
			break;
	}
	return wobble;
}

void setupBlendFunc(short blendType) {
	switch(blendType)
	{
		case OGL_BlendType_Crossfade:
			glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
			break;
		case OGL_BlendType_Add:
			glBlendFunc(GL_SRC_ALPHA,GL_ONE);
			break;
		case OGL_BlendType_Crossfade_Premult:
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case OGL_BlendType_Add_Premult:
			glBlendFunc(GL_ONE, GL_ONE);
			break;
	}
}

bool setupGlow(struct view_data *view, std::unique_ptr<TextureManager>& TMgr, float wobble, float intensity, float flare, float selfLuminosity, float offset, RenderStep renderStep) {
    SprintathonObjectOps::Scope opsGlow(SprintathonObjectOps::Glow);
    // Glow is an extra material draw, not an additional sky opening. Its
    // normal surface already supplies the occluding depth and cutout alpha.
    if (sprintathon_fast_shaft_source() && renderStep == kDiffuse &&
        TMgr->TextureType != OGL_Txtr_Landscape)
        return false;
	if (TMgr->TransferMode == _textured_transfer && TMgr->IsGlowMapped()) {
		Shader *s = NULL;
		if (TMgr->TextureType == OGL_Txtr_Wall) {
			if (TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
				s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
			} else {
				s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
			}
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_SpriteBloom : Shader::S_Sprite);
		}

		TMgr->RenderGlowing();
		setupBlendFunc(TMgr->GlowBlend());
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);

		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
		s->setVector4(Shader::U_LiquidSpecLight,0,0,1,0);
		// Glow overlays retain their existing alpha/depth behavior. Also reset
		// the shared wall shader before it is reused by another draw pass.
		s->setFloat(Shader::U_SprintathonShaftSource, 0.0f);
		if (renderStep == kGlow) {
			s->setFloat(Shader::U_BloomScale, TMgr->GlowBloomScale());
			s->setFloat(Shader::U_BloomShift, TMgr->GlowBloomShift());
		}
		s->setFloat(Shader::U_Flare, flare);
		s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
		s->setFloat(Shader::U_Wobble, wobble);
		s->setFloat(Shader::U_Depth, offset - 1.0);
        s->setFloat(Shader::U_EmitterShade,
            TMgr->TextureType == OGL_Txtr_Wall &&
            !current_player->infravision_duration &&
            graphics_preferences->projectile_lights_per_pixel &&
            graphics_preferences->bright_texture_lights ?
            std::max(0.0f, std::min(1.0f, intensity)) : 1.0f);
		s->setFloat(Shader::U_Glow, TMgr->MinGlowIntensity());
		return true;
	}
	return false;
}


// The traversal still chooses the window and ordering. This only replaces the
// repeated surface preparation, preserving occluding black and alpha-cutout draws.
static void sprintathon_replay_bloom(SprintathonBloomPacket& p,view_data* view,
                                    float flare,float selfLuminosity,bool continuous=false,bool bindMaterial=true) {
    auto& material=p.material;
    const bool bump=TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_BumpMap);
    Shader* shader=Shader::get(bump?Shader::S_BumpBloom:Shader::S_WallBloom);
    if(!continuous)shader->enable();
    if(bindMaterial){
        material->RenderNormal();
        if(bump){glActiveTextureARB(GL_TEXTURE1_ARB);material->RenderBump();glActiveTextureARB(GL_TEXTURE0_ARB);}
        material->SetupTextureMatrix();
    }
    shader->setFloat(Shader::U_BloomScale,material->BloomScale());
    shader->setFloat(Shader::U_BloomShift,material->BloomShift());
    shader->setFloat(Shader::U_Flare,flare);
    shader->setFloat(Shader::U_SelfLuminosity,selfLuminosity);
    shader->setFloat(Shader::U_Pulsate,0);
    shader->setFloat(Shader::U_Wobble,0);
    shader->setFloat(Shader::U_Depth,p.offset);
    shader->setFloat(Shader::U_Glow,0);
    shader->setFloat(Shader::U_MediaRipple,0);
    const char* detail=std::getenv("SPRINTATHON_DISTANT_SURFACE_DETAIL");
    shader->setFloat(Shader::U_SprintathonDistantSurfaceDetail,
        graphics_preferences->simplify_distant_surfaces && (!detail || detail[0]!='0') ? 1.0f:0.0f);
    SprintathonSkyBake::textureReceiver=false;
    glEnable(GL_TEXTURE_2D);glColor4f(p.color[0],p.color[1],p.color[2],1);
    if(material->IsBlended()){
        glEnable(GL_BLEND);setupBlendFunc(material->NormalBlend());
        glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.001);
    }else{
        glDisable(GL_BLEND);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.5);
    }
    if(p.voidPresent && material->IsBlended()){glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);}
    glNormal3f(p.normal[0],p.normal[1],p.normal[2]);
    glMultiTexCoord4fARB(GL_TEXTURE1_ARB,p.tangent[0],p.tangent[1],p.tangent[2],p.tangent[3]);
    glVertexPointer(3,GL_FLOAT,0,p.vertices.data());
    glTexCoordPointer(2,GL_FLOAT,0,p.uv.data());
    sprintathon_shaft_draw_arrays(p.mode,0,GLsizei(p.vertices.size()/3));
    if(setupGlow(view,material,0,p.intensity,flare,selfLuminosity,p.offset,kGlow))
        sprintathon_shaft_draw_arrays(p.mode,0,GLsizei(p.vertices.size()/3));
    if(!continuous){Shader::disable();glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_MODELVIEW);}
}

// Consume the same frame-local surfaces emitted by the main opaque pass.
// Keeping this as a compact loop avoids material Setup(), geometry rebuilding
// and per-surface shader disable/enable pairs in the secondary passes.
void RenderRasterize_Shader::replay_surface_commands(RenderStep step) {
    auto& cache=SprintathonBloomReuse::get();
    const bool shaft=step==kDiffuse && sprintathon_fast_shaft_source();
    if(!cache.commands || cache.commandList.empty() || (!shaft && step!=kGlow))return;
    const auto start=std::chrono::steady_clock::now();
    // The command list is restricted to non-glowing opaque/cutout surfaces.
    // Their shader never changes within this loop.
    Shader* shader=Shader::get(shaft ? Shader::S_ShaftOcclusion :
        (TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_BumpMap) ? Shader::S_BumpBloom : Shader::S_WallBloom));
    shader->enable();
    if(shaft){
        sprintathon_trace_mask_bound();
        shader->setFloat(Shader::U_Pulsate,0);
        shader->setFloat(Shader::U_Wobble,0);
        shader->setFloat(Shader::U_Visibility,1);
    }
    clipping_window_data* lastWindow=nullptr;
    const TextureManager* lastMaterial=nullptr;
    for(auto* packet:cache.commandList){
        auto& p=*packet;
        // All commands are normal, non-shadeless wall materials. Matching the
        // resolved descriptor/frame/bitmap makes reuse local to this GL loop.
        const auto* material=p.material.get();
        const bool bindMaterial=!lastMaterial || material->ShapeDesc!=lastMaterial->ShapeDesc ||
            material->LowLevelShape!=lastMaterial->LowLevelShape || material->Texture!=lastMaterial->Texture;
        if(p.window!=lastWindow){clip_to_window(p.window);lastWindow=p.window;}
        if(shaft){
            if(bindMaterial){p.material->RenderNormal();p.material->SetupTextureMatrix();}
            shader->setFloat(Shader::U_Depth,p.offset);
            glEnable(GL_TEXTURE_2D);glColor4f(1,1,1,1);
            glDisable(GL_BLEND);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.5);
            glVertexPointer(3,GL_FLOAT,0,p.vertices.data());
            glTexCoordPointer(2,GL_FLOAT,0,p.uv.data());
            sprintathon_shaft_draw_arrays(p.mode,0,GLsizei(p.vertices.size()/3));
            ++cache.shaftCommands;
        }else{
            sprintathon_replay_bloom(p,view,weaponFlare,selfLuminosity,true,bindMaterial);
            ++cache.bloomCommands;
        }
        lastMaterial=material;
    }
    Shader::disable();glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_MODELVIEW);
    glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(shaft){cache.shaftReplayed=true;cache.shaftReplayMs+=elapsed;}
    else {cache.bloomReplayed=true;cache.bloomReplayMs+=elapsed;}
}

static const Shader::UniformName sprintathon_pool_positions[4] = {
    Shader::U_SprintathonConePool0, Shader::U_SprintathonConePool1,
    Shader::U_SprintathonConePool2, Shader::U_SprintathonConePool3};
static const Shader::UniformName sprintathon_pool_colors[4] = {
    Shader::U_SprintathonConeColor0, Shader::U_SprintathonConeColor1,
    Shader::U_SprintathonConeColor2, Shader::U_SprintathonConeColor3};

// Follow only real open portals intersecting the pool footprint. Matching
// XY coordinates alone would incorrectly connect Marathon's overlapping rooms.
static bool sprintathon_pool_connected(short start,const polygon_data* target,
    float x,float y,float radius)
{
    if(start==NONE)return false;
    static std::vector<short> queue;
    queue.clear();queue.push_back(start);
    for(size_t at=0;at<queue.size();++at) {
        const auto* p=get_polygon_data(queue[at]);
        if(p==target)return true;
        for(short edge=0;edge<p->vertex_count;++edge) {
            const short next=p->adjacent_polygon_indexes[edge];
            if(next==NONE || std::find(queue.begin(),queue.end(),next)!=queue.end())continue;
            const auto* line=get_line_data(p->line_indexes[edge]);
            if(LINE_IS_SOLID(line))continue;
            const auto* n=get_polygon_data(next);
            if(std::max(p->floor_height,n->floor_height)>=std::min(p->ceiling_height,n->ceiling_height))continue;
            const auto& a=get_endpoint_data(p->endpoint_indexes[edge])->vertex;
            const auto& b=get_endpoint_data(p->endpoint_indexes[(edge+1)%p->vertex_count])->vertex;
            const double dx=double(b.x)-a.x,dy=double(b.y)-a.y;
            const double len2=dx*dx+dy*dy;
            const double t=len2>0?std::max(0.0,std::min(1.0,((x-a.x)*dx+(y-a.y)*dy)/len2)):0;
            const double ex=x-(a.x+t*dx),ey=y-(a.y+t*dy);
            if(ex*ex+ey*ey>double(radius)*radius)continue;
            queue.push_back(next);
        }
    }
    return false;
}

template<class SurfaceSink>
static void sprintathon_write_floor_cone_pools(SurfaceSink *shader,
    const polygon_data *polygon, const horizontal_surface_data *surface, bool ceiling)
{
    // Liquid tops receive the same pool at their own surface height.
    // Keep ceilings and the underside of liquids excluded.
    if(!shader || !surface || ceiling || !polygon ||
       !graphics_preferences->ceiling_fog_cones || !graphics_preferences->projectile_lights_per_pixel ||
       graphics_preferences->ceiling_fog_cone_strength == 0) return;
    const auto fog=OGL_GetCurrFogData();
    if(!TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_Fog) ||
       ((!fog || !fog->IsPresent) && !TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_ForceFog))) return;
    int count=0;
    // Retained fixtures are available even when their sprites are off screen.
    for(const auto& cone:sprintathon_floor_cones) {
        if(count==4)break;
        if(cone.object_index<0 || size_t(cone.object_index)>=ObjectList.size())continue;
        const auto* object=&ObjectList[cone.object_index];
        if(!SLOT_IS_USED(object) || GET_OBJECT_OWNER(object)!=_object_is_scenery ||
           object->polygon==NONE)continue;
        const float height=cone.z-surface->height;
        // A capped cone that ends above the floor must not leave a detached pool.
        if(height<=0 || height>5.0f*WORLD_ONE)continue;
        const float radius=2.0f*std::min(cone.radius,WORLD_ONE*0.04f+height*0.28f);
        if(radius<1)continue;
        if(!sprintathon_pool_connected(object->polygon,polygon,cone.x,cone.y,radius))continue;
        const float gain=0.65f*(graphics_preferences->ceiling_fog_cone_strength/100.0f)/
            (1.0f+0.12f*(height/WORLD_ONE)*(height/WORLD_ONE));
        shader->setVector4(sprintathon_pool_positions[count],cone.x,cone.y,surface->height,1.0f/radius);
        shader->setVector4(sprintathon_pool_colors[count],cone.red*gain,cone.green*gain,cone.blue*gain,1);
        ++count;
    }
}

static void sprintathon_set_floor_cone_pools(Shader *shader,
    const polygon_data *polygon,const horizontal_surface_data *surface,bool ceiling)
{ sprintathon_write_floor_cone_pools(shader,polygon,surface,ceiling); }

// Each vec4 stores an inward-facing edge (normal x/y, offset) and the
// neighboring sector's light. A negative fourth component disables the slot.
template<class SurfaceSink>
static void sprintathon_write_sector_light_edges(SurfaceSink *shader,
                                               const polygon_data *polygon,
                                               const horizontal_surface_data *surface,
                                               bool ceiling, const view_data *camera,bool bloom=false,
    SprintathonOpaqueWorld::RetainedEdges* capture=nullptr)
{
    SprintathonSetupScope preparationTime(SprintathonSetupTrace::SectorEdges);
    if (!shader) return;

    // Preserve the receiver reset, but omit unused sector, pool and liquid-edge
    // setup for bloom. Material brightness and occluding draws still run.
    if (bloom) return;
    shader->setFloat(Shader::U_TextureBakeEnabled,0);
    for(int i=0;i<4;++i) shader->setVector4(sprintathon_pool_colors[i],0,0,0,0);
    shader->setFloat(Shader::U_SprintathonSectorBlendWidth,
        std::max(1.0f, 0.44f * WORLD_ONE * graphics_preferences->sector_shading_softness / 100.0f));
    shader->setVector4(Shader::U_SprintathonWallBlendAxis, 0, 0, 0, 0);
    // Refraction must not cross the boundary into previously composited media.
    const Shader::UniformName liquid_edges[] = {
        Shader::U_LiquidEdge0,
        Shader::U_LiquidEdge1,
        Shader::U_LiquidEdge2,
        Shader::U_LiquidEdge3,
        Shader::U_LiquidEdge4,
        Shader::U_LiquidEdge5,
        Shader::U_LiquidEdge6,
        Shader::U_LiquidEdge7
    };
    for (int i = 0; i < 8; ++i) {
        float nx = 0, ny = 0, offset = 0, enabled = 0;
        if (polygon && surface && surface->is_media && i < polygon->vertex_count) {
            const auto& a = get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            const auto& b = get_endpoint_data(polygon->endpoint_indexes[(i+1)%polygon->vertex_count])->vertex;
            const float dx = float(b.x-a.x), dy = float(b.y-a.y);
            const float length = std::sqrt(dx*dx+dy*dy);
            if (length > 0.0f) {
                nx = -dy/length; ny = dx/length;
                offset = -(nx*a.x+ny*a.y); enabled = 1;
            }
        }
        shader->setVector4(liquid_edges[i], nx, ny, offset, enabled);
    }
    // The weapon flare is the actual firing flash. The constant player light
    // preference only controls the classic view-centered ambient circle.
    const float flash = graphics_preferences->projectile_lights_per_pixel ?
        PIN(camera->maximum_depth_intensity - NATURAL_LIGHT_INTENSITY,
            0, FIXED_ONE) / float(FIXED_ONE) : 0.0f;
    shader->setVector4(Shader::U_SprintathonMuzzlePosition,
        camera->origin.x, camera->origin.y, camera->origin.z,
        1.0f / (4.5f * WORLD_ONE));
    shader->setVector4(Shader::U_SprintathonMuzzleColor,
        flash * 0.95f, flash * 0.65f, flash * 0.32f,
        flash > 0.001f ? 1.0f : 0.0f);
    const Shader::UniformName names[8] = {
        Shader::U_SprintathonSectorEdge0, Shader::U_SprintathonSectorEdge1,
        Shader::U_SprintathonSectorEdge2, Shader::U_SprintathonSectorEdge3,
        Shader::U_SprintathonSectorEdge4, Shader::U_SprintathonSectorEdge5,
        Shader::U_SprintathonSectorEdge6, Shader::U_SprintathonSectorEdge7
    };
    const Shader::UniformName spans[8] = {
        Shader::U_SprintathonSectorSpan0,
        Shader::U_SprintathonSectorSpan1,
        Shader::U_SprintathonSectorSpan2,
        Shader::U_SprintathonSectorSpan3,
        Shader::U_SprintathonSectorSpan4,
        Shader::U_SprintathonSectorSpan5,
        Shader::U_SprintathonSectorSpan6,
        Shader::U_SprintathonSectorSpan7
    };
    int count = 0;
    SprintathonOpaqueWorld::edgeDependency(capture,polygon);
    if (polygon && surface && graphics_preferences->soft_sector_light_edges &&
        graphics_preferences->sector_shading_softness > 0) {
        float center_x = 0.0f, center_y = 0.0f;
        for (short i = 0; i < polygon->vertex_count; ++i) {
            const auto& point = get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            center_x += point.x; center_y += point.y;
        }
        if (polygon->vertex_count) {
            center_x /= polygon->vertex_count;
            center_y /= polygon->vertex_count;
        }
        const float own_light = get_light_intensity(surface->lightsource_index) /
                                float(FIXED_ONE - 1);
        for (short i = 0; i < polygon->vertex_count && (capture || count < 8); ++i) {
            const short adjacent_index = polygon->adjacent_polygon_indexes[i];
            if (adjacent_index == NONE) continue;
            const polygon_data *adjacent = get_polygon_data(adjacent_index);
            if (!adjacent) continue;
            SprintathonOpaqueWorld::edgeDependency(capture,adjacent);
            short adjacent_light;
            if (surface->is_media) {
                // Blend only a continuous exposed liquid surface, including its
                // underside. Use liquid lighting rather than floor/ceiling light.
                if (adjacent->media_index == NONE) continue;
                const media_data *neighbor_media = get_media_data(adjacent->media_index);
                if (!neighbor_media || neighbor_media->type != surface->media_type ||
                    neighbor_media->height != surface->height) continue;
                if (surface->height <= adjacent->floor_height ||
                    surface->height >= adjacent->ceiling_height ||
                    surface->height <= polygon->floor_height ||
                    surface->height >= polygon->ceiling_height) continue;
                adjacent_light = adjacent->media_lightsource_index;
            } else {
                const short adjacent_height = ceiling ? adjacent->ceiling_height :
                                                        adjacent->floor_height;
                if (adjacent_height != surface->height) continue;
                adjacent_light = ceiling ? adjacent->ceiling_lightsource_index :
                                           adjacent->floor_lightsource_index;
            }
            const float neighbor = get_light_intensity(adjacent_light) /
                                   float(FIXED_ONE - 1);
            if (!capture && std::abs(neighbor - own_light) < 0.025f) continue;
            const auto& a = get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            const auto& b = get_endpoint_data(
                polygon->endpoint_indexes[(i + 1) % polygon->vertex_count])->vertex;
            const float dx = float(b.x - a.x), dy = float(b.y - a.y);
            const float length = std::sqrt(dx*dx + dy*dy);
            if (length < 1.0f) continue;
            float nx = -dy / length, ny = dx / length;
            if ((center_x - a.x)*nx + (center_y - a.y)*ny < 0.0f) {
                nx = -nx; ny = -ny;
            }
            const float offset = -(a.x*nx + a.y*ny);
            if(capture){
                capture->candidates.push_back({{{nx,ny,offset}},{{float(a.x),float(a.y),float(b.x),float(b.y)}},adjacent_light,nullptr});
                continue;
            }
            shader->setVector4(names[count], nx, ny, offset, neighbor);
            shader->setVector4(spans[count], a.x, a.y, b.x, b.y);
            ++count;
        }
    }
    for (int i = count; i < 8; ++i)
        shader->setVector4(names[i], 0.0f, 0.0f, 0.0f, -1.0f);
}

static void sprintathon_set_sector_light_edges(Shader* shader,const polygon_data* polygon,
    const horizontal_surface_data* surface,bool ceiling,const view_data* camera)
{
    SprintathonSkyBake::textureReceiver=false;
    sprintathon_write_sector_light_edges(shader,polygon,surface,ceiling,camera,
        shader==Shader::get(Shader::S_WallBloom) || shader==Shader::get(Shader::S_BumpBloom));
}

namespace SprintathonOpaqueWorld {
// Reuse the normal renderer's selection logic, writing CPU records rather than
// changing a shader or issuing per-surface GL uniform calls.
struct DynamicSink {
    std::array<std::array<float,12>,20> emitterRecords{};
    std::array<float,12> projectile{};
    float count=0,first=0,end=0;std::array<float,4> liquid{};
    void setFloat(Shader::UniformName name,float value){
        if(name==Shader::U_SprintathonSurfaceLightCount)count=value;
        else if(name==Shader::U_SprintathonSceneryFirst)first=value;
        else if(name==Shader::U_SprintathonAllSceneryCount)end=value;
    }
    void setVector4(Shader::UniformName name,float x,float y,float z,float w){
        float* out=nullptr;
        switch(name){
        case Shader::U_SprintathonLightPosition2:out=emitterRecords[0].data()+0;break;
        case Shader::U_SprintathonLightColor2:out=emitterRecords[0].data()+4;break;
        case Shader::U_SprintathonLightPlane2:out=emitterRecords[0].data()+8;break;
        case Shader::U_SprintathonLightPosition3:out=emitterRecords[1].data()+0;break;
        case Shader::U_SprintathonLightColor3:out=emitterRecords[1].data()+4;break;
        case Shader::U_SprintathonLightPlane3:out=emitterRecords[1].data()+8;break;
        case Shader::U_SprintathonLightPosition4:out=emitterRecords[2].data()+0;break;
        case Shader::U_SprintathonLightColor4:out=emitterRecords[2].data()+4;break;
        case Shader::U_SprintathonLightPlane4:out=emitterRecords[2].data()+8;break;
        case Shader::U_SprintathonLightPosition5:out=emitterRecords[3].data()+0;break;
        case Shader::U_SprintathonLightColor5:out=emitterRecords[3].data()+4;break;
        case Shader::U_SprintathonLightPlane5:out=emitterRecords[3].data()+8;break;
        case Shader::U_SprintathonLightPosition6:out=emitterRecords[4].data()+0;break;
        case Shader::U_SprintathonLightColor6:out=emitterRecords[4].data()+4;break;
        case Shader::U_SprintathonLightPlane6:out=emitterRecords[4].data()+8;break;
        case Shader::U_SprintathonLightPosition7:out=emitterRecords[5].data()+0;break;
        case Shader::U_SprintathonLightColor7:out=emitterRecords[5].data()+4;break;
        case Shader::U_SprintathonLightPlane7:out=emitterRecords[5].data()+8;break;
        case Shader::U_SprintathonLightPosition8:out=emitterRecords[6].data()+0;break;
        case Shader::U_SprintathonLightColor8:out=emitterRecords[6].data()+4;break;
        case Shader::U_SprintathonLightPlane8:out=emitterRecords[6].data()+8;break;
        case Shader::U_SprintathonLightPosition9:out=emitterRecords[7].data()+0;break;
        case Shader::U_SprintathonLightColor9:out=emitterRecords[7].data()+4;break;
        case Shader::U_SprintathonLightPlane9:out=emitterRecords[7].data()+8;break;
        case Shader::U_SprintathonLightPosition10:out=emitterRecords[8].data()+0;break;
        case Shader::U_SprintathonLightColor10:out=emitterRecords[8].data()+4;break;
        case Shader::U_SprintathonLightPlane10:out=emitterRecords[8].data()+8;break;
        case Shader::U_SprintathonLightPosition11:out=emitterRecords[9].data()+0;break;
        case Shader::U_SprintathonLightColor11:out=emitterRecords[9].data()+4;break;
        case Shader::U_SprintathonLightPlane11:out=emitterRecords[9].data()+8;break;
        case Shader::U_SprintathonLightPosition12:out=emitterRecords[10].data()+0;break;
        case Shader::U_SprintathonLightColor12:out=emitterRecords[10].data()+4;break;
        case Shader::U_SprintathonLightPlane12:out=emitterRecords[10].data()+8;break;
        case Shader::U_SprintathonLightPosition13:out=emitterRecords[11].data()+0;break;
        case Shader::U_SprintathonLightColor13:out=emitterRecords[11].data()+4;break;
        case Shader::U_SprintathonLightPlane13:out=emitterRecords[11].data()+8;break;
        case Shader::U_SprintathonLightPosition14:out=emitterRecords[12].data()+0;break;
        case Shader::U_SprintathonLightColor14:out=emitterRecords[12].data()+4;break;
        case Shader::U_SprintathonLightPlane14:out=emitterRecords[12].data()+8;break;
        case Shader::U_SprintathonLightPosition15:out=emitterRecords[13].data()+0;break;
        case Shader::U_SprintathonLightColor15:out=emitterRecords[13].data()+4;break;
        case Shader::U_SprintathonLightPlane15:out=emitterRecords[13].data()+8;break;
        case Shader::U_SprintathonLightPosition16:out=emitterRecords[14].data()+0;break;
        case Shader::U_SprintathonLightColor16:out=emitterRecords[14].data()+4;break;
        case Shader::U_SprintathonLightPlane16:out=emitterRecords[14].data()+8;break;
        case Shader::U_SprintathonLightPosition17:out=emitterRecords[15].data()+0;break;
        case Shader::U_SprintathonLightColor17:out=emitterRecords[15].data()+4;break;
        case Shader::U_SprintathonLightPlane17:out=emitterRecords[15].data()+8;break;
        case Shader::U_SprintathonLightPosition18:out=emitterRecords[16].data()+0;break;
        case Shader::U_SprintathonLightColor18:out=emitterRecords[16].data()+4;break;
        case Shader::U_SprintathonLightPlane18:out=emitterRecords[16].data()+8;break;
        case Shader::U_SprintathonLightPosition19:out=emitterRecords[17].data()+0;break;
        case Shader::U_SprintathonLightColor19:out=emitterRecords[17].data()+4;break;
        case Shader::U_SprintathonLightPlane19:out=emitterRecords[17].data()+8;break;
        case Shader::U_SprintathonLightPosition20:out=emitterRecords[18].data()+0;break;
        case Shader::U_SprintathonLightColor20:out=emitterRecords[18].data()+4;break;
        case Shader::U_SprintathonLightPlane20:out=emitterRecords[18].data()+8;break;
        case Shader::U_SprintathonLightPosition21:out=emitterRecords[19].data()+0;break;
        case Shader::U_SprintathonLightColor21:out=emitterRecords[19].data()+4;break;
        case Shader::U_SprintathonLightPlane21:out=emitterRecords[19].data()+8;break;
        case Shader::U_SprintathonLightPosition:out=projectile.data();break;
        case Shader::U_SprintathonLightColor:out=projectile.data()+4;break;
        case Shader::U_SprintathonLiquidLightSettings:out=liquid.data();break;
        default:break;
        }
        if(out){out[0]=x;out[1]=y;out[2]=z;out[3]=w;}
    }
};
inline bool prepareDynamic(int count,const float* vertices,bool baked,float* row,const view_data* view){
    SprintathonSurfaceLightBounds bounds;
    bounds.x0=bounds.x1=vertices[0];bounds.y0=bounds.y1=vertices[1];bounds.z0=bounds.z1=vertices[2];bounds.valid=true;
    for(int i=1;i<count;++i){
        bounds.x0=std::min(bounds.x0,vertices[i*3]);bounds.x1=std::max(bounds.x1,vertices[i*3]);
        bounds.y0=std::min(bounds.y0,vertices[i*3+1]);bounds.y1=std::max(bounds.y1,vertices[i*3+1]);
        bounds.z0=std::min(bounds.z0,vertices[i*3+2]);bounds.z1=std::max(bounds.z1,vertices[i*3+2]);
    }
    const std::array<float,7> key{{bounds.x0,bounds.y0,bounds.z0,bounds.x1,bounds.y1,bounds.z1,baked?1.0f:0.0f}};
    if(reusePreparation()){
        const auto found=dynamicPreparation.find(key);
        if(found!=dynamicPreparation.end()){
            std::copy(found->second.begin(),found->second.end(),row+32);++dynamicPrepHits;return true;
        }
    }
    ++dynamicPrepMisses;
    DynamicSink sink;
    sprintathon_write_view_emitters(&sink,&bounds,true,baked);
    if(graphics_preferences->projectile_lights_per_pixel)
        sprintathon_write_projectile_light(&sink,(bounds.x0+bounds.x1)*.5f,(bounds.y0+bounds.y1)*.5f,
            (bounds.z0+bounds.z1)*.5f,&bounds);
    row[32]=sink.count;row[33]=sink.first;row[34]=sink.end;
    for(int i=0;i<int(sink.count) && i<20;++i){
        const int id=storeLight(sink.emitterRecords[i]);if(id<0)return false;row[36+i]=float(id);
    }
    const int projectile=storeLight(sink.projectile);if(projectile<0)return false;row[56]=float(projectile);
    const float flash=graphics_preferences->projectile_lights_per_pixel ?
        PIN(view->maximum_depth_intensity-NATURAL_LIGHT_INTENSITY,0,FIXED_ONE)/float(FIXED_ONE):0.0f;
    std::array<float,12> muzzle{{float(view->origin.x),float(view->origin.y),float(view->origin.z),
        1.0f/(4.5f*WORLD_ONE),flash*.95f,flash*.65f,flash*.32f,flash>.001f?1.0f:0.0f,0,0,0,0}};
    const int muzzleId=storeLight(muzzle);if(muzzleId<0)return false;row[57]=float(muzzleId);
    std::copy(sink.liquid.begin(),sink.liquid.end(),row+60);
    sceneryTexture=sprintathon_scenery_light_texture;
    if(reusePreparation() && dynamicPreparation.size()<8192){
        std::array<float,32> record;std::copy(row+32,row+64,record.begin());
        dynamicPreparation.emplace(key,record);
    }
    return true;
}
}

namespace SprintathonOpaqueWorld {
// Compare exact input values once per view, rather than hashing or walking
// neighbouring surfaces for every submitted polygon. Conservative invalidation
// also covers platform movement, Lua changes and visual-editor modifications.
inline void beginRetainedFrame(){
    const auto start=std::chrono::steady_clock::now();
    auto& state=retainedNextState;state.clear();
    state={int64_t(SprintathonSkyBake::mapGeneration),dynamic_world->polygon_count,
        SprintathonSkyBake::enabled(),SprintathonSkyBake::available,
        SprintathonSkyBake::textureBakeActive(),int64_t(atlasGeneration)};
    const bool reset=state!=retainedState;
    if(reset){
        retainedSurfaces.clear();retainedState.swap(state);
        retainedPolygonState.clear();++retainedInvalidations;
    }
    const int count=dynamic_world->polygon_count;
    retainedPolygonState.resize(count);
    std::vector<unsigned char> changed(count,0);
    std::vector<int64_t> local;local.reserve(160);
    for(int i=0;i<count;++i){
        const auto* p=get_polygon_data(i);local.clear();
        local.insert(local.end(),{p->type,p->floor_height,p->ceiling_height,p->vertex_count});
        for(int e=0;e<p->vertex_count;++e){
            const auto& v=get_endpoint_data(p->endpoint_indexes[e])->vertex;
            local.insert(local.end(),{p->endpoint_indexes[e],v.x,v.y,p->line_indexes[e],
                p->adjacent_polygon_indexes[e],p->side_indexes[e]});
            if(p->side_indexes[e]!=NONE){
                const auto* side=get_side_data(p->side_indexes[e]);
                local.insert(local.end(),{side->type,side->primary_texture.texture,side->secondary_texture.texture,
                    side->primary_transfer_mode,side->secondary_transfer_mode,
                    side->primary_lightsource_index,side->secondary_lightsource_index});
            }
        }
        local.insert(local.end(),{p->floor_lightsource_index,p->ceiling_lightsource_index});
        if(local!=retainedPolygonState[i]){
            ++retainedPolygonVersions[p];
            changed[i]=1;retainedPolygonState[i].assign(local.begin(),local.end());
        }
    }
    // A changed room can also alter its neighbour's wall extent. Check both
    // directions of the current adjacency; topology edits also change local state.
    if(!reset){
        std::vector<int64_t> owners;
        for(int i=0;i<count;++i){
            const auto* p=get_polygon_data(i);bool affected=changed[i]!=0;
            for(int e=0;e<p->vertex_count && !affected;++e){
                const int other=p->adjacent_polygon_indexes[e];
                affected=other>=0 && other<count && changed[other];
            }
            if(affected)owners.push_back(int64_t(reinterpret_cast<uintptr_t>(p)));
        }
        std::sort(owners.begin(),owners.end());
        if(!owners.empty())for(auto it=retainedSurfaces.begin();it!=retainedSurfaces.end();){
            if(std::binary_search(owners.begin(),owners.end(),it->first[0])){
                it=retainedSurfaces.erase(it);++retainedLocalInvalidations;
            }else ++it;
        }
    }
    retainedValidationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
inline bool submitRetained(Material& material,RetainedSurface& record,float shade,float depth,
    clipping_window_data* window,const view_data* view,const polygon_data* polygon=nullptr,
    const horizontal_surface_data* surface=nullptr,bool ceiling=false,
    const vertical_surface_data* wallSurface=nullptr){
    // Geometry survives light changes. Refresh sector-edge shading and cone
    // pools from current state, including neighbour lights and platform heights.
    SurfaceRecord detail;
    const float detailWeight=surfaceDetail(record.count,record.vertices.data(),view);
    if(detailWeight>0.0f)prepareRetainedDetail(record,detail,polygon,surface,ceiling,wallSurface,view);
    return submit(material,record.count,record.vertices.data(),record.uv.data(),shade,depth,window,view,
        record.bake,&detail,&record.geometry,detailWeight);
}
static bool transfer(short mode){return mode==_xfer_normal || mode==_xfer_2x || mode==_xfer_4x;}
static bool floor(clipping_window_data* window,polygon_data* polygon,horizontal_surface_data* surface,bool ceiling,const view_data* view){
    if(!active)return false;
    ++seen;
    if(surface->is_media || !transfer(surface->transfer_mode)){++fallback;return false;}
    if(!stationary(polygon)){++skippedPlatforms;return false;}
    if(polygon->vertex_count<3 || polygon->vertex_count>16){++fallback;return false;}
    auto* m=material(AnimTxtr_Translate(surface->texture));
    if(!m){++skippedMaterials;return false;}
    float scale=WORLD_ONE*m->manager->TileRatio();
    if(surface->transfer_mode==_xfer_2x)scale*=2;
    if(surface->transfer_mode==_xfer_4x)scale*=4;
    if(!(scale>0)){++fallback;return false;}
    RetainedKey retainedKey{};
    if(retainedEnabled()){
        retainedKey={{int64_t(reinterpret_cast<uintptr_t>(polygon)),0,ceiling?1:0,surface->height,
            surface->origin.x,surface->origin.y,surface->texture,surface->transfer_mode,surface->lightsource_index}};
        // Preserve the exact float scale, without rounding texture dimensions.
        static_assert(sizeof(scale)==sizeof(uint32_t),"float scale size");
        uint32_t scaleBits=0;std::memcpy(&scaleBits,&scale,sizeof(scale));retainedKey[19]=scaleBits;
        if(auto* record=findRetained(retainedKey)){
            if(SprintathonSkyBake::enabled() && record->bake!=baked(polygon,ceiling?1:0,surface->height,record->yExtent)){
                ++fallback;return false;
            }
            const float shade=get_light_intensity(surface->lightsource_index)/float(FIXED_ONE-1);
            if(submitRetained(*m,*record,shade,0,window,view,polygon,surface,ceiling))return true;
            ++fallback;return false;
        }
    }
    float vertices[48],uv[32];
    for(int i=0;i<polygon->vertex_count;++i){
        const int at=ceiling?polygon->vertex_count-1-i:i;
        const auto& point=get_endpoint_data(polygon->endpoint_indexes[at])->vertex;
        vertices[i*3]=point.x;vertices[i*3+1]=point.y;vertices[i*3+2]=surface->height;
        uv[i*2]=(point.x+surface->origin.x)/scale;uv[i*2+1]=(point.y+surface->origin.y)/scale;
    }
    const BakeEntry* bake=nullptr;
    if(SprintathonSkyBake::enabled()){
        int y0=32767,y1=-32768;
        for(int i=0;i<polygon->vertex_count;++i){int y=get_endpoint_data(polygon->endpoint_indexes[i])->vertex.y;y0=std::min(y0,y);y1=std::max(y1,y);}
        bake=baked(polygon,ceiling?1:0,surface->height,y1-y0);
        if(!bake){++fallback;return false;}
    }
    const float detailWeight=surfaceDetail(polygon->vertex_count,vertices,view);
    SurfaceRecord detail;if(detailWeight>0.0f || retainedEnabled())prepareSurface(detail,polygon,surface,ceiling,nullptr,view);
    if(retainedEnabled())rememberSurface(retainedKey,polygon->vertex_count,vertices,uv,detail,bake);
    const float shade=get_light_intensity(surface->lightsource_index)/float(FIXED_ONE-1);
    if(submit(*m,polygon->vertex_count,vertices,uv,shade,0,window,view,bake,&detail,nullptr,detailWeight))return true;
    ++fallback;return false;
}
static bool side(clipping_window_data* window,vertical_surface_data* surface,bool voidPresent,const view_data* view){
    if(!active)return false;
    ++seen;
    if(!transfer(surface->transfer_mode)){++fallback;return false;}
    if(!stationary(surface->blend_polygon)){++skippedPlatforms;return false;}
    const world_distance h=std::min(surface->h1,surface->hmax);
    if(h<=surface->h0 || surface->length<=0){++fallback;return false;}
    auto* m=material(AnimTxtr_Translate(surface->texture_definition->texture));
    if(!m){++skippedMaterials;return false;}
    float scale=WORLD_ONE*m->manager->TileRatio();
    if(surface->transfer_mode==_xfer_2x)scale*=2;
    if(surface->transfer_mode==_xfer_4x)scale*=4;
    if(!(scale>=1 && scale<=65535)){++fallback;return false;}
    RetainedKey retainedKey{};
    if(retainedEnabled()){
        const auto* t=surface->texture_definition;
        retainedKey={{int64_t(reinterpret_cast<uintptr_t>(surface->blend_polygon)),1,
            int64_t(reinterpret_cast<uintptr_t>(t)),surface->blend_edge,
            int64_t(surface->h0)+view->origin.z,int64_t(surface->h1)+view->origin.z,
            int64_t(surface->hmax)+view->origin.z,surface->length,
            surface->p0.i,surface->p0.j,surface->p1.i,surface->p1.j,
            t->texture,t->x0,t->y0,surface->transfer_mode,surface->lightsource_index,surface->ambient_delta}};
        uint32_t scaleBits=0;std::memcpy(&scaleBits,&scale,sizeof(scale));retainedKey[19]=scaleBits;
        if(auto* record=findRetained(retainedKey)){
            if(SprintathonSkyBake::enabled() && record->bake!=baked(surface->blend_polygon,2+surface->blend_edge,0,0)){
                ++fallback;return false;
            }
            const float shade=(get_light_intensity(surface->lightsource_index)+surface->ambient_delta)/float(FIXED_ONE-1);
            if(submitRetained(*m,*record,shade,voidPresent?0:-2,window,view,nullptr,nullptr,false,surface))return true;
            ++fallback;return false;
        }
    }
    const uint16 div=uint16(scale);
    const world_distance x0=surface->texture_definition->x0%div;
    const world_distance y0=surface->texture_definition->y0%div;
    const double tOffset=surface->h1+view->origin.z+y0;
    world_point2d points[2];uint16 flags;
    long_to_overflow_short_2d(surface->p0,points[0],flags);
    long_to_overflow_short_2d(surface->p1,points[1],flags);
    // Match the legacy wall's short-coordinate conversion before upload.
    const world_distance top=h+view->origin.z,bottom=surface->h0+view->origin.z;
    float vertices[12]={float(points[0].x),float(points[0].y),float(top),
        float(points[1].x),float(points[1].y),float(top),
        float(points[1].x),float(points[1].y),float(bottom),
        float(points[0].x),float(points[0].y),float(bottom)};
    float uv[8];
    for(int i=0;i<4;++i){uv[2*i]=float((tOffset-vertices[3*i+2])/div);
        uv[2*i+1]=(x0+((i==1 || i==2)?surface->length:0))/float(div);}
    const BakeEntry* bake=nullptr;
    if(SprintathonSkyBake::enabled()){
        if(surface->blend_edge==NONE){++fallback;return false;}
        bake=baked(surface->blend_polygon,2+surface->blend_edge,0,0);
        if(!bake){++fallback;return false;}
    }
    const float detailWeight=surfaceDetail(4,vertices,view);
    SurfaceRecord detail;if(detailWeight>0.0f || retainedEnabled())prepareSurface(detail,nullptr,nullptr,false,surface,view);
    if(retainedEnabled())rememberSurface(retainedKey,4,vertices,uv,detail,bake);
    const float shade=(get_light_intensity(surface->lightsource_index)+surface->ambient_delta)/float(FIXED_ONE-1);
    if(submit(*m,4,vertices,uv,shade,voidPresent?0:-2,window,view,bake,&detail,nullptr,detailWeight))return true;
    ++fallback;return false;
}
}

void RenderRasterize_Shader::render_node_floor_or_ceiling(clipping_window_data *window,
	polygon_data *polygon, horizontal_surface_data *surface, bool void_present, bool ceil, RenderStep renderStep) {
    SprintathonDetailScope detailScope;
    if(surface_editor_noclip() && ceil && !surface->is_media)return;
    if(renderStep==kDiffuse && world_surface_pass==WorldSurfacePass::opaque &&
       SprintathonOpaqueWorld::floor(window,polygon,surface,ceil,view)){
        SprintathonPortalTrace::get().surface(polygon,nullptr,ceil?1:0);return;
    }

    const bool batchTransfer=surface->transfer_mode==_xfer_normal ||
        surface->transfer_mode==_xfer_2x || surface->transfer_mode==_xfer_4x;
    auto& bloomReuse=SprintathonBloomReuse::get();
    const bool reuseEligible=bloomReuse.active && batchTransfer && (!sprintathon_fast_shaft_source() || bloomReuse.commands) && !surface->is_media;
    SprintathonBloomReuse::Key bloomKey;
    if(reuseEligible){
        bloomKey={0,reinterpret_cast<intptr_t>(window),void_present,
            surface->transfer_mode,reinterpret_cast<intptr_t>(polygon),ceil,surface->height,surface->origin.x,surface->origin.y,surface->texture};
        if((sprintathon_fast_shaft_source() && bloomReuse.shaftReplayed) ||
           (renderStep==kGlow && bloomReuse.bloomReplayed)) {
            auto found=bloomReuse.packets.find(bloomKey);
            if(found!=bloomReuse.packets.end() && found->second.commandOpaque){
                if(sprintathon_fast_shaft_source())++bloomReuse.shaftSkips;
                else ++bloomReuse.bloomSkips;
                return;
            }
        }
        if(renderStep==kGlow){
            if(auto* packet=bloomReuse.find(bloomKey)){
                clip_to_window(window);
                sprintathon_replay_bloom(*packet,view,weaponFlare,selfLuminosity);
                return;
            }
        }
    }

    if (SprintathonOpaqueBatch::active && (!batchTransfer || void_present)) flush_opaque_batch();



    // These surfaces stay in the original back-to-front pass. In particular,
    // skip media before setupWallTexture can copy the scene for refraction.
    if (world_surface_pass == WorldSurfacePass::opaque &&
        (surface->is_media || surface->transfer_mode == _xfer_landscape ||
         surface->transfer_mode == _xfer_big_landscape)) return;

    bool outside_frustum = false;
    SprintathonSurfaceLightBounds surface_bounds;
    if ((world_frustum_active || (surface_light_bounds_active &&
         graphics_preferences->projectile_lights_per_pixel)) && polygon && polygon->vertex_count > 0) {
        const auto& first = get_endpoint_data(polygon->endpoint_indexes[0])->vertex;
        float x0 = first.x, x1 = first.x, y0 = first.y, y1 = first.y;
        for (short i = 1; i < polygon->vertex_count; ++i) {
            const auto& vertex = get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            x0 = std::min(x0, float(vertex.x)); x1 = std::max(x1, float(vertex.x));
            y0 = std::min(y0, float(vertex.y)); y1 = std::max(y1, float(vertex.y));
        }
        outside_frustum = world_bounds_outside(x0, y0, surface->height,
                                               x1, y1, surface->height);
        surface_bounds = {x0, y0, float(surface->height), x1, y1, float(surface->height),
            surface_light_bounds_active && graphics_preferences->projectile_lights_per_pixel};
    }
    // Opaque preparation and bloom do not collect diffuse texture emitters.
    if (outside_frustum && (sprintathon_fast_shaft_source() || world_surface_pass == WorldSurfacePass::opaque ||
                            renderStep == kGlow ||
                            !graphics_preferences->projectile_lights_per_pixel ||
                            !graphics_preferences->bright_texture_lights)) return;

	float offset = 0;

	const shape_descriptor& texture = AnimTxtr_Translate(surface->texture);
	float intensity = get_light_intensity(surface->lightsource_index) / float(FIXED_ONE - 1);
    float projectile_rgb[3] = {0.0f, 0.0f, 0.0f};
    float surface_light_x = 0.0f, surface_light_y = 0.0f;
    float surface_light_z = surface->height;
    if (!sprintathon_fast_shaft_source() && polygon && polygon->vertex_count > 0) {
        float center_x = 0.0f, center_y = 0.0f;
        for (short i = 0; i < polygon->vertex_count; ++i) {
            const world_point2d& vertex = get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            center_x += vertex.x; center_y += vertex.y;
        }
        center_x /= polygon->vertex_count;
        center_y /= polygon->vertex_count;
        surface_light_x = center_x; surface_light_y = center_y;
        if (!graphics_preferences->projectile_lights_per_pixel)
            sprintathon_projectile_light_rgb(center_x, center_y, surface->height, projectile_rgb);
    }
	float wobble = calcWobble(surface->transfer_mode, view->tick_count);
	// note: wobble and pulsate behave the same way on floors and ceilings
	// note 2: stronger wobble looks more like classic with default shaders
    std::unique_ptr<TextureManager> resolvedMaterial;
    if(sprintathon_early_material_enabled() && renderStep==kDiffuse && batchTransfer &&
       !sprintathon_fast_shaft_source() && !current_player->infravision_duration &&
       world_surface_pass!=WorldSurfacePass::all && !surface->is_media){
        resolvedMaterial=sprintathon_resolve_wall(texture);
        if(resolvedMaterial->ShapeDesc==UNONE)return;
        const bool resolvedOpaque=void_present || !resolvedMaterial->IsBlended();
        const bool rejectOpaque=world_surface_pass==WorldSurfacePass::opaque && !resolvedOpaque;
        const bool rejectRemaining=world_surface_pass==WorldSurfacePass::remaining && (resolvedOpaque || outside_frustum);
        if(rejectOpaque || rejectRemaining){
            auto& trace=SprintathonMaterialTrace::get();
            if(rejectOpaque)++trace.skippedOpaque;else ++trace.skippedRemaining;
            // Emitter observations still occur in the remaining traversal,
            // including off-screen opaque surfaces. No shader or texture binds.
            if(rejectRemaining && graphics_preferences->projectile_lights_per_pixel)
                { SprintathonSetupScope preparationTime(SprintathonSetupTrace::EmitterRecord);
                sprintathon_record_texture_light(resolvedMaterial.get(),surface_light_x,surface_light_y,
                    surface_light_z,renderStep,false,false,surface->lightsource_index,
                    0.0f,NONE,NONE,0.0f,
                    SprintathonSkyBake::textureSourceBaked(polygon,ceil?1:0,texture,surface->lightsource_index),
                    polygon,nullptr,ceil?1:0);
            }
            return;
        }
    }
    SprintathonResolvedWallScope resolvedScope(resolvedMaterial);
	auto TMgr = setupWallTexture(texture, surface->transfer_mode, wobble * 4.0,
		0, intensity, offset, renderStep, surface->media_type);
    if (!graphics_preferences->projectile_lights_per_pixel)
        glColor4f(std::min(1.0f, intensity + projectile_rgb[0]),
              std::min(1.0f, intensity + projectile_rgb[1]),
              std::min(1.0f, intensity + projectile_rgb[2]), 1.0f);
	if(TMgr->ShapeDesc == UNONE) { return; }
    // void_present describes the space behind the surface, not transparency.
    // Below, void-backed blended textures already draw with blending and
    // alpha testing disabled. Classify that existing draw as opaque too.
    const bool opaque_surface = !surface->is_media && TMgr->TextureType == OGL_Txtr_Wall &&
        (void_present || !TMgr->IsBlended());
    if (opaque_surface)
        SprintathonOpaqueBatch::inspectSurface(TMgr->IsGlowMapped(),batchTransfer);
    if (SprintathonOpaqueBatch::active && SprintathonOpaqueBatch::count && opaque_surface && TMgr->IsGlowMapped()) {
        flush_opaque_batch();
        SprintathonOpaqueBatch::active=false;
        render_node_floor_or_ceiling(window,polygon,surface,void_present,ceil,renderStep);
        SprintathonOpaqueBatch::active=true;
        return;
    }
    if (world_surface_pass == WorldSurfacePass::opaque && skip_world_surface(opaque_surface)) return;
    const bool lava_surface = surface->is_media && surface->media_type == _media_lava;
    if (graphics_preferences->projectile_lights_per_pixel &&
        world_surface_pass != WorldSurfacePass::opaque) {
        { SprintathonSetupScope preparationTime(SprintathonSetupTrace::EmitterRecord);
                sprintathon_record_texture_light(TMgr.get(), surface_light_x, surface_light_y,
            surface_light_z, renderStep, false, lava_surface, surface->lightsource_index,
            0.0f, NONE, surface->is_media ? surface->media_type : NONE, 0.0f,
            !surface->is_media && SprintathonSkyBake::textureSourceBaked(polygon,ceil?1:0,texture,surface->lightsource_index),
            polygon, nullptr, surface->is_media ? 2 : (ceil ? 1 : 0));
            }
        if (surface->is_media && polygon && renderStep == kDiffuse) {
            // One emitter at the center of a broad pool cannot reach its banks.
            // The shared emitter limit keeps the added work bounded.
            for (short i = 0; i < polygon->vertex_count; ++i) {
                const world_point2d& bank =
                    get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
                { SprintathonSetupScope preparationTime(SprintathonSetupTrace::EmitterRecord);
                sprintathon_record_texture_light(TMgr.get(), bank.x, bank.y,
                    surface->height, renderStep, false, lava_surface,
                    surface->lightsource_index, 0.0f, NONE, surface->media_type, 0.0f, false,
                    polygon, nullptr, short(3+i));
            }
            }
        }
    }

    // Retain emitter collection even for off-screen surfaces, then avoid
    // sector/pixel-light uniforms, clipping setup, geometry and glow draws.
    if (outside_frustum) {
        reset_skipped_world_surface();
        return;
    }
    if (world_surface_pass == WorldSurfacePass::remaining && skip_world_surface(opaque_surface)) return;
    sprintathon_world_detail.select(TMgr->TextureType == OGL_Txtr_Landscape ? 2 : (opaque_surface ? 0 : 1));

    sprintathon_set_sector_light_edges(
        sprintathon_surface_shader(renderStep, TMgr->TextureType), polygon, surface, ceil, view);
    if (SprintathonSkyBake::enabled() && renderStep == kDiffuse &&
        (!surface->is_media || surface->media_type != _media_lava) &&
        !current_player->infravision_duration && TMgr->TextureType == OGL_Txtr_Wall && polygon && polygon->vertex_count) {
        SprintathonSetupScope preparationTime(SprintathonSetupTrace::BakedBinding);
        double x0=1e30,y0=1e30,x1=-1e30,y1=-1e30;
        for(int i=0;i<polygon->vertex_count;++i){const auto& p=get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            x0=std::min(x0,double(p.x));x1=std::max(x1,double(p.x));y0=std::min(y0,double(p.y));y1=std::max(y1,double(p.y));}
        using SprintathonSkyBake::V;
        SprintathonSkyBake::bind(sprintathon_surface_shader(renderStep, TMgr->TextureType),polygon,
            surface->is_media ? (ceil ? 17 : 16) : (ceil ? 1 : 0),
            V(x0,y0,surface->height),V(1,0,0),V(0,1,0),V(0,0,ceil?-1:1),x1-x0,y1-y0);
    }

    if(renderStep == kDiffuse)
        sprintathon_set_floor_cone_pools(sprintathon_surface_shader(renderStep, TMgr->TextureType),
            polygon, surface, ceil);
    if (lava_surface && graphics_preferences->bright_texture_lights &&
        !current_player->infravision_duration) {
        // Lava emits its own light: preserve its visible brightness even when
        // the sector light is dark. The glow floor applies to both wall and bump shaders.
        Shader *lava_shader = sprintathon_surface_shader(renderStep, TMgr->TextureType);
        if (lava_shader) lava_shader->setFloat(Shader::U_Glow, 0.85f);
    }
    if (graphics_preferences->projectile_lights_per_pixel) {
        glColor4f(intensity, intensity, intensity, 1.0f);
        sprintathon_set_pixel_light(surface_light_x, surface_light_y, surface_light_z, renderStep, TMgr->TextureType, &surface_bounds);
        if(surface->is_media && surface->media_type!=_media_lava && renderStep==kDiffuse &&
           graphics_preferences->liquid_specular && !current_player->infravision_duration)
            sprintathon_set_water_scenery(sprintathon_surface_shader(renderStep,TMgr->TextureType),surface_bounds);

    } else {
        sprintathon_clear_pixel_light(renderStep, TMgr->TextureType, &surface_bounds);
    }



	const bool adjustable_media = surface->is_media &&
		TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_LiqSeeThru);
	if (adjustable_media)
	{
		GLfloat color[4];
		glGetFloatv(GL_CURRENT_COLOR, color);
		// The preference is the final surface opacity, not a multiplier on
		// the scenario texture's pre-existing alpha.
		color[3] = A1_PIN(
			Get_OGL_ConfigureData().AnimatedMediaOpacity / 100.0f,
			0.25f, 1.0f);
		glColor4fv(color);
	}

	if (TMgr->IsBlended() || adjustable_media) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	if (void_present && TMgr->IsBlended()) {
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
	}

	short vertex_count = polygon->vertex_count;

	if (vertex_count) {
        clip_to_window(window);

		world_distance x = 0.0, y = 0.0;
		instantiate_transfer_mode(view, surface->transfer_mode, x, y);
        if(surface->is_media && surface->media_type!=_media_lava && renderStep==kDiffuse &&
           graphics_preferences->liquid_specular && !current_player->infravision_duration)
            sprintathon_set_liquid_flow(sprintathon_surface_shader(renderStep,TMgr->TextureType),
                polygon->media_index,surface->origin.x+x,surface->origin.y+y);


		vec3 N;
		vec3 T;
		float sign;
		if(ceil) {
			N = vec3(0,0,-1);
			T = vec3(0,1,0);
			sign = 1;
		} else {
			N = vec3(0,0,1);
			T = vec3(0,1,0);
			sign = -1;
		}
		glNormal3f(N[0], N[1], N[2]);
		glMultiTexCoord4fARB(GL_TEXTURE1_ARB, T[0], T[1], T[2], sign);

		GLfloat vertex_array[MAXIMUM_VERTICES_PER_POLYGON * 3];
		GLfloat texcoord_array[MAXIMUM_VERTICES_PER_POLYGON * 2];

		GLfloat* vp = vertex_array;
		GLfloat* tp = texcoord_array;
		float scale;

		switch (surface->transfer_mode)
		{
			case _xfer_2x:
				scale = 2 * WORLD_ONE * TMgr->TileRatio();
				break;
		    case _xfer_4x:
				scale = 4 * WORLD_ONE * TMgr->TileRatio();
				break;
			default:
				scale = WORLD_ONE * TMgr->TileRatio();
				break;
		}

        SprintathonOpaqueGeometry::CpuKey geometryKey;
        SprintathonOpaqueGeometry::CpuEntry* preparedGeometry=nullptr;
        const bool cacheGeometry=opaque_surface && SprintathonOpaqueGeometry::enabled() && vertex_count<=16;
        if(cacheGeometry){
            geometryKey.count=vertex_count;
            geometryKey.inputs[0]=0;geometryKey.inputs[1]=ceil;
            geometryKey.inputs[2]=surface->height;geometryKey.inputs[3]=scale;
            geometryKey.inputs[4]=surface->origin.x;geometryKey.inputs[5]=surface->origin.y;
            geometryKey.inputs[6]=x;geometryKey.inputs[7]=y;
            for(int i=0;i<vertex_count;++i){const auto& point=get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
                geometryKey.inputs[8+2*i]=point.x;geometryKey.inputs[9+2*i]=point.y;}
            preparedGeometry=SprintathonOpaqueGeometry::findPrepared(geometryKey);
        }
        if(!preparedGeometry){
		if (ceil)
		{
			for(short i = 0; i < vertex_count; ++i) {
				world_point2d vertex = get_endpoint_data(polygon->endpoint_indexes[vertex_count - 1 - i])->vertex;
				*vp++ = vertex.x;
				*vp++ = vertex.y;
				*vp++ = surface->height;
				*tp++ = (vertex.x + surface->origin.x + x) / scale;
				*tp++ = (vertex.y + surface->origin.y + y) / scale;
			}
		}
		else
		{
			for(short i=0; i<vertex_count; ++i) {
				world_point2d vertex = get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
				*vp++ = vertex.x;
				*vp++ = vertex.y;
				*vp++ = surface->height;
				*tp++ = (vertex.x + surface->origin.x + x) / scale;
				*tp++ = (vertex.y + surface->origin.y + y) / scale;
			}
		}
            if(cacheGeometry)preparedGeometry=SprintathonOpaqueGeometry::storePrepared(geometryKey,vertex_array,texcoord_array);
        }
        const GLfloat* drawVertices=preparedGeometry?preparedGeometry->vertices.data():vertex_array;
        const GLfloat* drawUV=preparedGeometry?preparedGeometry->uv.data():texcoord_array;
		glVertexPointer(3, GL_FLOAT, 0, drawVertices);
		glTexCoordPointer(2, GL_FLOAT, 0, drawUV);

		const int queued=(batchTransfer && opaque_surface && !void_present && !TMgr->IsGlowMapped()) ? queue_opaque_batch(window,TMgr.get(),GL_POLYGON,
                vertex_count,drawVertices,drawUV,preparedGeometry,intensity,N[0],N[1],N[2],T[0],T[1],T[2],sign) : 0;
            if (queued<0) {
                // Cache exhaustion or unsupported state: redo setup after the
                // barrier, then use the established immediate path for this draw.
                SprintathonOpaqueBatch::active=false;
                render_node_floor_or_ceiling(window,polygon,surface,void_present,ceil,renderStep);
                SprintathonOpaqueBatch::active=true;
                return;
            }
            if(opaque_surface)SprintathonPortalTrace::get().surface(polygon,nullptr,ceil?1:0);
            if (!queued) sprintathon_draw_opaque_geometry(GL_POLYGON, vertex_count, drawVertices, drawUV, opaque_surface, preparedGeometry);

		// see note 2 above; pulsate uniform should stay set from setupWall call
		if (setupGlow(view, TMgr, 0, intensity, weaponFlare, selfLuminosity, offset, renderStep)) {
            sprintathon_set_sector_light_edges(
                sprintathon_surface_shader(renderStep, TMgr->TextureType), polygon, surface, ceil, view);
			sprintathon_draw_opaque_geometry(GL_POLYGON, vertex_count, drawVertices, drawUV, opaque_surface, preparedGeometry);
		}

            if(reuseEligible && renderStep==kDiffuse && !sprintathon_fast_shaft_source())
                bloomReuse.save(bloomKey,TMgr,GL_POLYGON,vertex_count,
                    drawVertices,drawUV,N[0],N[1],N[2],T[0],T[1],T[2],sign,
                    intensity,offset,projectile_rgb,void_present,window,opaque_surface);

		Shader::disable();
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
	}
}

// Reuse the sector blend shader in wall-local coordinates (along wall, height).
// Follow only polygons sharing the endpoint; never search unrelated nearby walls.
template<class SurfaceSink>
static void sprintathon_write_wall_light_edges(SurfaceSink *shader,
    const vertical_surface_data *surface, const view_data *camera,bool bloom=false,
    SprintathonOpaqueWorld::RetainedEdges* capture=nullptr)
{
    SprintathonSetupScope preparationTime(SprintathonSetupTrace::SectorEdges);
    sprintathon_write_sector_light_edges(shader, nullptr, nullptr, false, camera,bloom);
    if (bloom) return;
    if (!shader || !graphics_preferences->soft_sector_light_edges ||
        graphics_preferences->sector_shading_softness == 0 ||
        !surface->blend_polygon || surface->blend_edge == NONE) return;
    const polygon_data *owner = surface->blend_polygon;
    SprintathonOpaqueWorld::edgeDependency(capture,owner);
    const short edge = surface->blend_edge;
    const short endpoints[2] = {owner->endpoint_indexes[edge],
        owner->endpoint_indexes[(edge + 1) % owner->vertex_count]};
    const auto& a = get_endpoint_data(endpoints[0])->vertex;
    const auto& b = get_endpoint_data(endpoints[1])->vertex;
    const float dx = float(b.x) - a.x, dy = float(b.y) - a.y;
    const float length = std::sqrt(dx*dx + dy*dy);
    if (length < 1) return;
    // Transparent panels and landscape transfers keep their original shading.
    const short own_side_index = owner->side_indexes[edge];
    if (own_side_index == NONE) return;
    const side_data *own_side = get_side_data(own_side_index);
    if (surface->texture_definition == &own_side->transparent_texture ||
        surface->transfer_mode == _xfer_landscape ||
        surface->transfer_mode == _xfer_big_landscape) return;
    shader->setVector4(Shader::U_SprintathonWallBlendAxis,
        dx/length, dy/length, -(a.x*dx + a.y*dy)/length, 1);
    const Shader::UniformName edges[8] = {
        Shader::U_SprintathonSectorEdge0, Shader::U_SprintathonSectorEdge1,
        Shader::U_SprintathonSectorEdge2, Shader::U_SprintathonSectorEdge3,
        Shader::U_SprintathonSectorEdge4, Shader::U_SprintathonSectorEdge5,
        Shader::U_SprintathonSectorEdge6, Shader::U_SprintathonSectorEdge7};
    const Shader::UniformName spans[8] = {
        Shader::U_SprintathonSectorSpan0, Shader::U_SprintathonSectorSpan1,
        Shader::U_SprintathonSectorSpan2, Shader::U_SprintathonSectorSpan3,
        Shader::U_SprintathonSectorSpan4, Shader::U_SprintathonSectorSpan5,
        Shader::U_SprintathonSectorSpan6, Shader::U_SprintathonSectorSpan7};
    int count = 0;
    for (int end = 0; end < 2; ++end) {
        const polygon_data *polygon = owner;
        short incoming = owner->line_indexes[edge];
        for (int step = 0; step < 32 && count < 8; ++step) {
            SprintathonOpaqueWorld::edgeDependency(capture,polygon);
            short next = NONE;
            for (short j = 0; j < polygon->vertex_count; ++j)
                if (polygon->line_indexes[j] != incoming &&
                    (polygon->endpoint_indexes[j] == endpoints[end] ||
                     polygon->endpoint_indexes[(j+1)%polygon->vertex_count] == endpoints[end])) {
                    next = j; break;
                }
            if (next == NONE) break;
            const short adjacent_index = polygon->adjacent_polygon_indexes[next];
            const polygon_data *adjacent = adjacent_index == NONE ? nullptr : get_polygon_data(adjacent_index);
            SprintathonOpaqueWorld::edgeDependency(capture,adjacent);
            const short side_index = polygon->side_indexes[next];
            if (side_index != NONE) {
                const side_data *side = get_side_data(side_index);
                auto add = [&](float low, float high, short light, short transfer,
                               const side_texture_definition& texture) {
                    if (count >= 8 || high <= low || texture.texture == UNONE ||
                        light == NONE || transfer == _xfer_landscape || transfer == _xfer_big_landscape) return;
                    low = std::max(low, float(surface->h0) + camera->origin.z);
                    high = std::min(high, float(std::min(surface->h1, surface->hmax)) + camera->origin.z);
                    if (high <= low) return;
                    const float neighbor = std::max(0.0f, std::min(1.0f,
                        (get_light_intensity(light) + side->ambient_delta) / float(FIXED_ONE - 1)));
                    const float x = end ? length : 0.0f;
                    if(capture)capture->candidates.push_back({{{end?-1.0f:1.0f,0,end?length:0}},{{x,low,x,high}},light,side});
                    shader->setVector4(edges[count], end ? -1 : 1, 0, end ? length : 0, neighbor);
                    shader->setVector4(spans[count], x, low, x, high);
                    ++count;
                };
                if (side->type == _full_side)
                    add(polygon->floor_height, polygon->ceiling_height,
                        side->primary_lightsource_index, side->primary_transfer_mode, side->primary_texture);
                else if (adjacent) {
                    if (side->type == _low_side || side->type == _split_side)
                        add(polygon->floor_height, std::min(polygon->ceiling_height, adjacent->floor_height),
                            side->type == _split_side ? side->secondary_lightsource_index : side->primary_lightsource_index,
                            side->type == _split_side ? side->secondary_transfer_mode : side->primary_transfer_mode,
                            side->type == _split_side ? side->secondary_texture : side->primary_texture);
                    if (side->type == _high_side || side->type == _split_side)
                        add(std::max(polygon->floor_height, adjacent->ceiling_height), polygon->ceiling_height,
                            side->primary_lightsource_index, side->primary_transfer_mode, side->primary_texture);
                }
            }
            if (!adjacent || adjacent == owner ||
                (side_index != NONE && get_side_data(side_index)->type == _full_side)) break;
            incoming = polygon->line_indexes[next];
            polygon = adjacent;
        }
    }
}

static void sprintathon_set_wall_light_edges(Shader* shader,const vertical_surface_data* surface,
    const view_data* camera)
{
    SprintathonSkyBake::textureReceiver=false;
    sprintathon_write_wall_light_edges(shader,surface,camera,
        shader==Shader::get(Shader::S_WallBloom) || shader==Shader::get(Shader::S_BumpBloom));
}

namespace SprintathonOpaqueWorld {
struct SurfaceSink {
    SurfaceRecord& data;
    explicit SurfaceSink(SurfaceRecord& value):data(value){}
    void setFloat(Shader::UniformName name,float value){
        if(name==Shader::U_SprintathonSectorBlendWidth)data.values[0]=value;
    }
    void setVector4(Shader::UniformName name,float x,float y,float z,float w){
        float* out=nullptr;
        switch(name){
        case Shader::U_SprintathonSectorEdge0:out=data.values.data()+8;break;
        case Shader::U_SprintathonSectorSpan0:out=data.values.data()+40;break;
        case Shader::U_SprintathonSectorEdge1:out=data.values.data()+12;break;
        case Shader::U_SprintathonSectorSpan1:out=data.values.data()+44;break;
        case Shader::U_SprintathonSectorEdge2:out=data.values.data()+16;break;
        case Shader::U_SprintathonSectorSpan2:out=data.values.data()+48;break;
        case Shader::U_SprintathonSectorEdge3:out=data.values.data()+20;break;
        case Shader::U_SprintathonSectorSpan3:out=data.values.data()+52;break;
        case Shader::U_SprintathonSectorEdge4:out=data.values.data()+24;break;
        case Shader::U_SprintathonSectorSpan4:out=data.values.data()+56;break;
        case Shader::U_SprintathonSectorEdge5:out=data.values.data()+28;break;
        case Shader::U_SprintathonSectorSpan5:out=data.values.data()+60;break;
        case Shader::U_SprintathonSectorEdge6:out=data.values.data()+32;break;
        case Shader::U_SprintathonSectorSpan6:out=data.values.data()+64;break;
        case Shader::U_SprintathonSectorEdge7:out=data.values.data()+36;break;
        case Shader::U_SprintathonSectorSpan7:out=data.values.data()+68;break;
        case Shader::U_SprintathonConePool0:out=data.values.data()+72;break;
        case Shader::U_SprintathonConeColor0:out=data.values.data()+88;break;
        case Shader::U_SprintathonConePool1:out=data.values.data()+76;break;
        case Shader::U_SprintathonConeColor1:out=data.values.data()+92;break;
        case Shader::U_SprintathonConePool2:out=data.values.data()+80;break;
        case Shader::U_SprintathonConeColor2:out=data.values.data()+96;break;
        case Shader::U_SprintathonConePool3:out=data.values.data()+84;break;
        case Shader::U_SprintathonConeColor3:out=data.values.data()+100;break;
        case Shader::U_SprintathonWallBlendAxis:out=data.values.data()+4;break;
        default:break;
        }
        if(out){out[0]=x;out[1]=y;out[2]=z;out[3]=w;}
    }
};
inline void prepareRetainedDetail(RetainedSurface& record,SurfaceRecord& data,
    const polygon_data* polygon,const horizontal_surface_data* floorSurface,bool ceiling,
    const vertical_surface_data* wallSurface,const view_data* view){
    auto& edges=record.edges;
    const bool blend=graphics_preferences->soft_sector_light_edges;
    const int softness=graphics_preferences->sector_shading_softness;
    bool valid=edges.ready && edges.blend==blend && edges.softness==softness;
    for(const auto& dep:edges.dependencies)
        if(retainedPolygonVersions[dep.first]!=dep.second){valid=false;break;}
    if(!valid){
        edges=RetainedEdges();edges.blend=blend;edges.softness=softness;
        SurfaceRecord rebuilt;SurfaceSink sink(rebuilt);
        if(wallSurface)sprintathon_write_wall_light_edges(&sink,wallSurface,view,false,&edges);
        else sprintathon_write_sector_light_edges(&sink,polygon,floorSurface,ceiling,view,false,&edges);
        // Preserve the normal/tangent prepared with the retained geometry.
        std::copy(record.detail.values.begin()+104,record.detail.values.end(),rebuilt.values.begin()+104);
        record.detail=rebuilt;edges.ready=true;++retainedEdgeBuilds;
    }else ++retainedEdgeHits;
    data=record.detail;data.values[1]=0;
    for(int i=0;i<8;++i)data.values[8+i*4+3]=-1;
    const float own=wallSurface?0:get_light_intensity(floorSurface->lightsource_index)/float(FIXED_ONE-1);
    int count=0;
    for(const auto& edge:edges.candidates){
        float value=get_light_intensity(edge.light)/float(FIXED_ONE-1);
        if(edge.side)value=std::max(0.0f,std::min(1.0f,
            (get_light_intensity(edge.light)+edge.side->ambient_delta)/float(FIXED_ONE-1)));
        else if(std::abs(value-own)<0.025f)continue;
        std::copy(edge.plane.begin(),edge.plane.end(),data.values.begin()+8+count*4);
        data.values[11+count*4]=value;
        std::copy(edge.span.begin(),edge.span.end(),data.values.begin()+40+count*4);
        if(++count==8)break;
    }
    data.values[1]=float(count);
    if(floorSurface){SurfaceSink sink(data);sprintathon_write_floor_cone_pools(&sink,polygon,floorSurface,ceiling);}
}
inline void prepareSurface(SurfaceRecord& data,const polygon_data* polygon,
    const horizontal_surface_data* floorSurface,bool ceiling,const vertical_surface_data* wallSurface,
    const view_data* view)
{
    std::vector<int64_t> key;
    if(reusePreparation()){
        const polygon_data* owner=wallSurface?wallSurface->blend_polygon:polygon;
        key={int64_t(reinterpret_cast<uintptr_t>(owner)),wallSurface?1:0,ceiling?1:0};
        if(wallSurface){
            const auto& s=*wallSurface;
            key.insert(key.end(),{s.blend_edge,s.h0,s.h1,s.hmax,s.length,s.p0.i,s.p0.j,s.p1.i,s.p1.j,
                s.transfer_mode,s.lightsource_index,s.ambient_delta,
                int64_t(reinterpret_cast<uintptr_t>(s.texture_definition))});
        }else if(floorSurface){
            key.insert(key.end(),{floorSurface->height,floorSurface->lightsource_index,floorSurface->is_media?1:0});
        }
        const auto found=surfacePreparation.find(key);
        if(found!=surfacePreparation.end()){data=found->second;++surfacePrepHits;return;}
    }
    ++surfacePrepMisses;
    SurfaceSink sink(data);
    if(wallSurface){
        sprintathon_write_wall_light_edges(&sink,wallSurface,view);
        const double dx=(wallSurface->p1.i-wallSurface->p0.i)/double(wallSurface->length);
        const double dy=(wallSurface->p1.j-wallSurface->p0.j)/double(wallSurface->length);
        data.values[104]=-dy;data.values[105]=dx;data.values[106]=0;
        data.values[108]=dx;data.values[109]=dy;data.values[110]=0;data.values[111]=1;
    }else{
        sprintathon_write_sector_light_edges(&sink,polygon,floorSurface,ceiling,view);
        sprintathon_write_floor_cone_pools(&sink,polygon,floorSurface,ceiling);
        data.values[104]=data.values[105]=0;data.values[106]=ceiling?-1:1;
        data.values[108]=0;data.values[109]=1;data.values[110]=0;data.values[111]=ceiling?1:-1;
    }
    // Only visit occupied edge slots in the fragment shader.
    for(int i=0;i<8;++i)if(data.values[8+i*4+3]>=0)data.values[1]=float(i+1);
    if(reusePreparation() && surfacePreparation.size()<8192)surfacePreparation.emplace(std::move(key),data);
}
}

void RenderRasterize_Shader::render_node_side(clipping_window_data *window, vertical_surface_data *surface, bool void_present, RenderStep renderStep) {
    SprintathonDetailScope detailScope;
    if(renderStep==kDiffuse && world_surface_pass==WorldSurfacePass::opaque &&
       SprintathonOpaqueWorld::side(window,surface,void_present,view)){
        SprintathonPortalTrace::get().surface(surface->blend_polygon,surface->texture_definition,2);return;
    }
    const bool batchTransfer=surface->transfer_mode==_xfer_normal ||
        surface->transfer_mode==_xfer_2x || surface->transfer_mode==_xfer_4x;
    auto& bloomReuse=SprintathonBloomReuse::get();
    const bool reuseEligible=bloomReuse.active && batchTransfer && (!sprintathon_fast_shaft_source() || bloomReuse.commands);
    SprintathonBloomReuse::Key bloomKey;
    if(reuseEligible){
        bloomKey={1,reinterpret_cast<intptr_t>(window),void_present,
            surface->transfer_mode,reinterpret_cast<intptr_t>(surface->texture_definition),surface->p0.i,surface->p0.j,surface->p1.i,surface->p1.j,surface->h0,surface->h1,surface->hmax,view->origin.z,surface->texture_definition->texture,surface->texture_definition->x0,surface->texture_definition->y0,surface->length};
        if((sprintathon_fast_shaft_source() && bloomReuse.shaftReplayed) ||
           (renderStep==kGlow && bloomReuse.bloomReplayed)) {
            auto found=bloomReuse.packets.find(bloomKey);
            if(found!=bloomReuse.packets.end() && found->second.commandOpaque){
                if(sprintathon_fast_shaft_source())++bloomReuse.shaftSkips;
                else ++bloomReuse.bloomSkips;
                return;
            }
        }
        if(renderStep==kGlow){
            if(auto* packet=bloomReuse.find(bloomKey)){
                clip_to_window(window);
                sprintathon_replay_bloom(*packet,view,weaponFlare,selfLuminosity);
                return;
            }
        }
    }

    if (SprintathonOpaqueBatch::active && (!batchTransfer || void_present)) flush_opaque_batch();


    // These surfaces stay in the original back-to-front pass. In particular,
    // skip media before setupWallTexture can copy the scene for refraction.
    if (world_surface_pass == WorldSurfacePass::opaque &&
        (surface->transfer_mode == _xfer_landscape ||
         surface->transfer_mode == _xfer_big_landscape)) return;

    const float bottom = float(surface->h0) + view->origin.z;
    const float top = float(std::min(surface->h1, surface->hmax)) + view->origin.z;
    // Use the same world-space posts and heights as the submitted wall quad.
    // If a legacy short coordinate would wrap, leave clipping to OpenGL.
    const bool outside_frustum = bottom >= INT16_MIN && top <= INT16_MAX &&
        top >= bottom && world_bounds_outside(
            float(std::min(surface->p0.i, surface->p1.i)),
            float(std::min(surface->p0.j, surface->p1.j)), bottom,
            float(std::max(surface->p0.i, surface->p1.i)),
            float(std::max(surface->p0.j, surface->p1.j)), top);
    SprintathonSurfaceLightBounds surface_bounds;
    if (surface_light_bounds_active && graphics_preferences->projectile_lights_per_pixel &&
        bottom >= INT16_MIN && top <= INT16_MAX && top >= bottom &&
        std::min(surface->p0.i, surface->p1.i) >= INT16_MIN &&
        std::max(surface->p0.i, surface->p1.i) <= INT16_MAX &&
        std::min(surface->p0.j, surface->p1.j) >= INT16_MIN &&
        std::max(surface->p0.j, surface->p1.j) <= INT16_MAX) {
        surface_bounds = {float(std::min(surface->p0.i, surface->p1.i)),
            float(std::min(surface->p0.j, surface->p1.j)), bottom,
            float(std::max(surface->p0.i, surface->p1.i)),
            float(std::max(surface->p0.j, surface->p1.j)), top, true};
    }
    if (outside_frustum && (sprintathon_fast_shaft_source() || world_surface_pass == WorldSurfacePass::opaque ||
                            renderStep == kGlow ||
                            !graphics_preferences->projectile_lights_per_pixel ||
                            !graphics_preferences->bright_texture_lights)) return;

	float offset = 0;
	if (!void_present) {
		offset = -2.0;
	}

	const shape_descriptor& texture = AnimTxtr_Translate(surface->texture_definition->texture);
	float intensity = (get_light_intensity(surface->lightsource_index) + surface->ambient_delta) / float(FIXED_ONE - 1);
    float surface_light_x = (surface->p0.i + surface->p1.i) * 0.5f;
    float surface_light_y = (surface->p0.j + surface->p1.j) * 0.5f;
    float surface_light_z = (surface->h0 + std::min(surface->h1, surface->hmax)) * 0.5f + view->origin.z;
    float projectile_rgb[3] = {0,0,0};
    if (!sprintathon_fast_shaft_source() && !graphics_preferences->projectile_lights_per_pixel)
        sprintathon_projectile_light_rgb(
            (surface->p0.i + surface->p1.i) * 0.5f,
            (surface->p0.j + surface->p1.j) * 0.5f,
            (surface->h0 + std::min(surface->h1, surface->hmax)) * 0.5f + view->origin.z,
            projectile_rgb);
	float wobble = calcWobble(surface->transfer_mode, view->tick_count);
	float pulsate = 0;
	if (surface->transfer_mode == _xfer_pulsate) {
		pulsate = wobble;
		wobble = 0;
	}
    std::unique_ptr<TextureManager> resolvedMaterial;
    if(sprintathon_early_material_enabled() && renderStep==kDiffuse && batchTransfer &&
       !sprintathon_fast_shaft_source() && !current_player->infravision_duration &&
       world_surface_pass!=WorldSurfacePass::all){
        resolvedMaterial=sprintathon_resolve_wall(texture);
        if(resolvedMaterial->ShapeDesc==UNONE)return;
        const bool resolvedOpaque=void_present || !resolvedMaterial->IsBlended();
        const bool rejectOpaque=world_surface_pass==WorldSurfacePass::opaque && !resolvedOpaque;
        const bool rejectRemaining=world_surface_pass==WorldSurfacePass::remaining && (resolvedOpaque || outside_frustum);
        if(rejectOpaque || rejectRemaining){
            auto& trace=SprintathonMaterialTrace::get();
            if(rejectOpaque)++trace.skippedOpaque;else ++trace.skippedRemaining;
            // Emitter observations still occur in the remaining traversal,
            // including off-screen opaque surfaces. No shader or texture binds.
            if(rejectRemaining && graphics_preferences->projectile_lights_per_pixel)
                { SprintathonSetupScope preparationTime(SprintathonSetupTrace::EmitterRecord);
                sprintathon_record_texture_light(resolvedMaterial.get(),surface_light_x,surface_light_y,
                    surface_light_z,renderStep,true,false,surface->lightsource_index,
                    float(surface->ambient_delta),NONE,NONE,0.0f,
                    SprintathonSkyBake::textureSourceBaked(surface->blend_polygon,2+surface->blend_edge,texture,surface->lightsource_index),
                    surface->blend_polygon,surface->texture_definition,0);
            }
            return;
        }
    }
    SprintathonResolvedWallScope resolvedScope(resolvedMaterial);
	auto TMgr = setupWallTexture(texture, surface->transfer_mode, pulsate, wobble, intensity, offset, renderStep);
    if (!graphics_preferences->projectile_lights_per_pixel)
        glColor4f(std::min(1.0f, intensity + projectile_rgb[0]),
              std::min(1.0f, intensity + projectile_rgb[1]),
              std::min(1.0f, intensity + projectile_rgb[2]), 1.0f);
	if(TMgr->ShapeDesc == UNONE) { return; }
    // Keep the existing void-dependent depth offset and alpha rules below.
    const bool opaque_surface = TMgr->TextureType == OGL_Txtr_Wall &&
        (void_present || !TMgr->IsBlended());
    if (opaque_surface)
        SprintathonOpaqueBatch::inspectSurface(TMgr->IsGlowMapped(),batchTransfer);
    if (SprintathonOpaqueBatch::active && SprintathonOpaqueBatch::count && opaque_surface && TMgr->IsGlowMapped()) {
        flush_opaque_batch();
        SprintathonOpaqueBatch::active=false;
        render_node_side(window,surface,void_present,renderStep);
        SprintathonOpaqueBatch::active=true;
        return;
    }
    if (world_surface_pass == WorldSurfacePass::opaque && skip_world_surface(opaque_surface)) return;
    if (graphics_preferences->projectile_lights_per_pixel &&
        world_surface_pass != WorldSurfacePass::opaque)
        { SprintathonSetupScope preparationTime(SprintathonSetupTrace::EmitterRecord);
                sprintathon_record_texture_light(TMgr.get(), surface_light_x, surface_light_y, surface_light_z, renderStep, true, false,
            surface->lightsource_index, float(surface->ambient_delta), NONE, NONE, 0.0f,
            SprintathonSkyBake::textureSourceBaked(surface->blend_polygon,2+surface->blend_edge,texture,surface->lightsource_index),
            surface->blend_polygon, surface->texture_definition, 0);
            }
    // Retain emitter collection even for off-screen surfaces, then avoid
    // sector/pixel-light uniforms, clipping setup, geometry and glow draws.
    if (outside_frustum) {
        reset_skipped_world_surface();
        return;
    }
    if (world_surface_pass == WorldSurfacePass::remaining && skip_world_surface(opaque_surface)) return;
    sprintathon_world_detail.select(TMgr->TextureType == OGL_Txtr_Landscape ? 2 : (opaque_surface ? 0 : 1));

    sprintathon_set_wall_light_edges(
        sprintathon_surface_shader(renderStep, TMgr->TextureType), surface, view);
    if (SprintathonSkyBake::enabled() && renderStep == kDiffuse && !current_player->infravision_duration &&
        TMgr->TextureType == OGL_Txtr_Wall && surface->blend_polygon && surface->blend_edge != NONE) {
        SprintathonSetupScope preparationTime(SprintathonSetupTrace::BakedBinding);
        using SprintathonSkyBake::V;
        const auto* owner=surface->blend_polygon;
        V origin(surface->p0.i,surface->p0.j,surface->h0+view->origin.z);
        V delta(double(surface->p1.i)-surface->p0.i,double(surface->p1.j)-surface->p0.j,0);
        double length=std::sqrt(SprintathonSkyBake::dot(delta,delta));
        V axis=SprintathonSkyBake::unit(delta),normal(-axis.y,axis.x,0);
        V center;
        for(int i=0;i<owner->vertex_count;++i){const auto& p=get_endpoint_data(owner->endpoint_indexes[i])->vertex;center=center+V(p.x,p.y,0);}
        center=center*(1.0/std::max(1,int(owner->vertex_count)));
        if(SprintathonSkyBake::dot(center+origin*(-1),normal)<0)normal=normal*(-1);
        SprintathonSkyBake::bind(sprintathon_surface_shader(renderStep, TMgr->TextureType),owner,2+surface->blend_edge,
            origin,axis,V(0,0,1),normal,length,std::min(surface->h1,surface->hmax)-surface->h0);
    }

    if (graphics_preferences->projectile_lights_per_pixel) {
        glColor4f(intensity, intensity, intensity, 1.0f);
        sprintathon_set_pixel_light(surface_light_x, surface_light_y, surface_light_z, renderStep, TMgr->TextureType, &surface_bounds);
    } else {
        sprintathon_clear_pixel_light(renderStep, TMgr->TextureType, &surface_bounds);
    }



	if (TMgr->IsBlended()) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	if (void_present && TMgr->IsBlended()) {
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
	}

	world_distance h= MIN(surface->h1, surface->hmax);

	if (h>surface->h0) {

		world_point2d vertex[2];
		uint16 flags;
		flagged_world_point3d vertices[MAXIMUM_VERTICES_PER_WORLD_POLYGON];
		short vertex_count;

		/* initialize the two posts of our trapezoid */
		vertex_count= 2;
		long_to_overflow_short_2d(surface->p0, vertex[0], flags);
		long_to_overflow_short_2d(surface->p1, vertex[1], flags);

		if (vertex_count) {
            clip_to_window(window);

			vertex_count= 4;
			vertices[0].z= vertices[1].z= h + view->origin.z;
			vertices[2].z= vertices[3].z= surface->h0 + view->origin.z;
			vertices[0].x= vertices[3].x= vertex[0].x, vertices[0].y= vertices[3].y= vertex[0].y;
			vertices[1].x= vertices[2].x= vertex[1].x, vertices[1].y= vertices[2].y= vertex[1].y;
			vertices[0].flags = vertices[3].flags = 0;
			vertices[1].flags = vertices[2].flags = 0;

			uint16 div;
			switch (surface->transfer_mode)
			{
				case _xfer_2x:
					div = 2 * WORLD_ONE * TMgr->TileRatio();
					break;
				case _xfer_4x:
					div = 4 * WORLD_ONE * TMgr->TileRatio();
					break;
				default:
					div = WORLD_ONE * TMgr->TileRatio();;
					break;
			}
			
			double dx = (surface->p1.i - surface->p0.i) / double(surface->length);
			double dy = (surface->p1.j - surface->p0.j) / double(surface->length);

			world_distance x0 = surface->texture_definition->x0 % div;
			world_distance y0 = surface->texture_definition->y0 % div;

			double tOffset = surface->h1 + view->origin.z + y0;

			vec3 N(-dy, dx, 0);
			vec3 T(dx, dy, 0);
			float sign = 1;
			glNormal3f(N[0], N[1], N[2]);
			glMultiTexCoord4fARB(GL_TEXTURE1_ARB, T[0], T[1], T[2], sign);

			world_distance x = 0.0, y = 0.0;
			instantiate_transfer_mode(view, surface->transfer_mode, x, y);

			x0 -= x;
			tOffset -= y;

			GLfloat vertex_array[12];
			GLfloat texcoord_array[8];

			GLfloat* vp = vertex_array;
			GLfloat* tp = texcoord_array;

            SprintathonOpaqueGeometry::CpuKey geometryKey;
            SprintathonOpaqueGeometry::CpuEntry* preparedGeometry=nullptr;
            const bool cacheGeometry=opaque_surface && SprintathonOpaqueGeometry::enabled();
            if(cacheGeometry){
                geometryKey.count=vertex_count;geometryKey.inputs[0]=1;
                geometryKey.inputs[1]=div;geometryKey.inputs[2]=tOffset;
                geometryKey.inputs[3]=x0;geometryKey.inputs[4]=surface->length;
                for(int i=0;i<vertex_count;++i){geometryKey.inputs[5+3*i]=vertices[i].x;
                    geometryKey.inputs[6+3*i]=vertices[i].y;geometryKey.inputs[7+3*i]=vertices[i].z;}
                preparedGeometry=SprintathonOpaqueGeometry::findPrepared(geometryKey);
            }
            if(!preparedGeometry){
			for(int i = 0; i < vertex_count; ++i) {
				float p2 = 0;
				if(i == 1 || i == 2) { p2 = surface->length; }

				*vp++ = vertices[i].x;
				*vp++ = vertices[i].y;
				*vp++ = vertices[i].z;
				*tp++ = (tOffset - vertices[i].z) / static_cast<float>(div);
				*tp++ = (x0+p2) / static_cast<float>(div);
			}
                if(cacheGeometry)preparedGeometry=SprintathonOpaqueGeometry::storePrepared(geometryKey,vertex_array,texcoord_array);
            }
            const GLfloat* drawVertices=preparedGeometry?preparedGeometry->vertices.data():vertex_array;
            const GLfloat* drawUV=preparedGeometry?preparedGeometry->uv.data():texcoord_array;
			glVertexPointer(3, GL_FLOAT, 0, drawVertices);
			glTexCoordPointer(2, GL_FLOAT, 0, drawUV);
			
			const int queued=(batchTransfer && opaque_surface && !void_present && !TMgr->IsGlowMapped()) ? queue_opaque_batch(window,TMgr.get(),GL_QUADS,
                vertex_count,drawVertices,drawUV,preparedGeometry,intensity,N[0],N[1],N[2],T[0],T[1],T[2],sign) : 0;
            if (queued<0) {
                // Cache exhaustion or unsupported state: redo setup after the
                // barrier, then use the established immediate path for this draw.
                SprintathonOpaqueBatch::active=false;
                render_node_side(window,surface,void_present,renderStep);
                SprintathonOpaqueBatch::active=true;
                return;
            }
            if(opaque_surface)SprintathonPortalTrace::get().surface(surface->blend_polygon,surface->texture_definition,2);
            if (!queued) sprintathon_draw_opaque_geometry(GL_QUADS, vertex_count, drawVertices, drawUV, opaque_surface, preparedGeometry);

			if (setupGlow(view, TMgr, wobble, intensity, weaponFlare, selfLuminosity, offset, renderStep)) {
            sprintathon_set_wall_light_edges(
                sprintathon_surface_shader(renderStep, TMgr->TextureType), surface, view);
				sprintathon_draw_opaque_geometry(GL_QUADS, vertex_count, drawVertices, drawUV, opaque_surface, preparedGeometry);
			}

            if(reuseEligible && renderStep==kDiffuse && !sprintathon_fast_shaft_source())
                bloomReuse.save(bloomKey,TMgr,GL_QUADS,vertex_count,
                    drawVertices,drawUV,N[0],N[1],N[2],T[0],T[1],T[2],sign,
                    intensity,offset,projectile_rgb,void_present,window,opaque_surface);

			Shader::disable();
			glMatrixMode(GL_TEXTURE);
			glLoadIdentity();
			glMatrixMode(GL_MODELVIEW);
		}
	}
}

extern void FlatBumpTexture(); // from OGL_Textures.cpp

bool RenderModel(rectangle_definition& RenderRectangle, short Collection,
	short CLUT, float flare, float selfLuminosity, RenderStep renderStep,
	GLsizei scene_width, GLsizei scene_height) {

	OGL_ModelData *ModelPtr = RenderRectangle.ModelPtr;
    if(sprintathon_shaft_trace.enabled)sprintathon_shaft_trace.material=0;
	OGL_SkinData *SkinPtr = ModelPtr->GetSkin(CLUT);
	if(!SkinPtr) { return false; }

	if (ModelPtr->Sidedness < 0) {
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
	} else if (ModelPtr->Sidedness > 0) {
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CW);
	} else {
		glDisable(GL_CULL_FACE);
	}

	glEnable(GL_TEXTURE_2D);
	if (SkinPtr->OpacityType != OGL_OpacType_Crisp || RenderRectangle.transfer_mode == _tinted_transfer) {
		glEnable(GL_BLEND);
		setupBlendFunc(SkinPtr->NormalBlend);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	GLfloat color[3];
	GLdouble shade = PIN(static_cast<GLfloat>(RenderRectangle.ambient_shade)/static_cast<GLfloat>(FIXED_ONE),0,1);
	color[0] = color[1] = color[2] = shade;

	Shader *s = NULL;
	bool canGlow = false;
	if (RenderRectangle.transfer_mode == _static_transfer) {
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invincible);
		} else {
			s = Shader::get(Shader::S_InvincibleBloom);
		}
        s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
        s->setFloat(Shader::U_TransferFadeOut,((float)((uint16)RenderRectangle.transfer_data))/(float)((int)FIXED_ONE));
	} else if (current_player->infravision_duration) {
		color[0] = color[1] = color[2] = 1;
		FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(RenderRectangle.ShapeDesc)), color);
		s = Shader::get(Shader::S_WallInfravision);
	} else if (RenderRectangle.transfer_mode == _tinted_transfer) {
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invisible);
		} else {
			s = Shader::get(Shader::S_InvisibleBloom);
		}
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
		const float visibility =
			1.0f - RenderRectangle.transfer_data / 32.0f;
		if (renderStep == kDiffuse &&
			!sprintathon_fast_shaft_source() && Get_OGL_ConfigureData().RefractiveInvisibility)
			setup_invisibility_refraction(s, scene_width, scene_height, visibility);
		else if (renderStep == kDiffuse)
			setup_classic_invisibility(s, visibility);
		else
			s->setFloat(Shader::U_Visibility, visibility);
	} else if (RenderRectangle.transfer_mode == _solid_transfer) {
		color[0] = 0;
		color[1] = 1;
		color[2] = 0;
	} else if (RenderRectangle.transfer_mode == _textured_transfer) {
		if (RenderRectangle.flags & _SHADELESS_BIT) {
			if (renderStep == kDiffuse) {
				color[0] = color[1] = color[2] = 1;
			} else {
				color[0] = color[1] = color[2] = 0;
			}
			flare = -1;
		} else {
			canGlow = true;
		}
	} else {
		color[0] = 0;
		color[1] = 0;
		color[2] = 1;
	}

	if(s == NULL) {
		if(!sprintathon_fast_shaft_source() && TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
			s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
		}
		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
	}

    if (sprintathon_fast_shaft_source() && renderStep == kDiffuse) {
        s=Shader::get(Shader::S_ShaftOcclusion); s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
        sprintathon_trace_mask_bound();
        s->setFloat(Shader::U_Visibility,RenderRectangle.transfer_mode == _tinted_transfer ?
            std::max(0.0f,std::min(1.0f,1.0f-RenderRectangle.transfer_data/32.0f)) : 1.0f);
        s->setFloat(Shader::U_Pulsate,0.0f);
        canGlow=false;
    }
	if (renderStep == kGlow) {
		s->setFloat(Shader::U_BloomScale, SkinPtr->BloomScale);
		s->setFloat(Shader::U_BloomShift, SkinPtr->BloomShift);
	}
	s->setFloat(Shader::U_Flare, flare);
	s->setFloat(Shader::U_SelfLuminosity, selfLuminosity);
	s->setFloat(Shader::U_Wobble, 0);
	s->setFloat(Shader::U_Depth, 0);
	s->setFloat(Shader::U_Glow, 0);
	glColor4f(color[0], color[1], color[2], 1);

	// Find an animated model's vertex positions and normals:
	short ModelSequence = RenderRectangle.ModelSequence;
	if (ModelSequence >= 0)
	{
		int NumFrames = ModelPtr->Model.NumSeqFrames(ModelSequence);
		if (NumFrames > 0)
		{
			short ModelFrame = PIN(RenderRectangle.ModelFrame, 0, NumFrames - 1);
			short NextModelFrame = PIN(RenderRectangle.NextModelFrame, 0, NumFrames - 1);
			float MixFrac = RenderRectangle.MixFrac;
			ModelPtr->Model.FindPositions_Sequence(true,
				ModelSequence, ModelFrame, MixFrac, NextModelFrame);
		}
		else
			ModelPtr->Model.FindPositions_Neutral(true);	// Fallback: neutral
	}
	else
		ModelPtr->Model.FindPositions_Neutral(true);	// Fallback: neutral (will do nothing for static models)

	glVertexPointer(3,GL_FLOAT,0,ModelPtr->Model.PosBase());
	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	if (ModelPtr->Model.TxtrCoords.empty()) {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	} else {
		glTexCoordPointer(2,GL_FLOAT,0,ModelPtr->Model.TCBase());
	}

	glEnableClientState(GL_NORMAL_ARRAY);
	glNormalPointer(GL_FLOAT,0,ModelPtr->Model.NormBase());

	glClientActiveTextureARB(GL_TEXTURE1_ARB);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(4,GL_FLOAT,sizeof(vec4),ModelPtr->Model.TangentBase());

	if(ModelPtr->Use(CLUT,OGL_SkinManager::Normal)) {
		LoadModelSkin(SkinPtr->NormalImg, Collection, CLUT);
	}

	if(!sprintathon_fast_shaft_source() && TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
		glActiveTextureARB(GL_TEXTURE1_ARB);
		if(ModelPtr->Use(CLUT,OGL_SkinManager::Bump)) {
			LoadModelSkin(SkinPtr->OffsetImg, Collection, CLUT);
		}
		if (!SkinPtr->OffsetImg.IsPresent()) {
			FlatBumpTexture();
		}
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}

	sprintathon_shaft_draw_elements(GL_TRIANGLES,(GLsizei)ModelPtr->Model.NumVI(),GL_UNSIGNED_SHORT,ModelPtr->Model.VIBase());

	if (canGlow && SkinPtr->GlowImg.IsPresent()) {
		glEnable(GL_BLEND);
		setupBlendFunc(SkinPtr->GlowBlend);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);

		s->enable();
        s->setFloat(Shader::U_TextureBakeEnabled, 0);
        s->setFloat(Shader::U_SkyBakeEnabled, 0);
		s->setFloat(Shader::U_Glow, SkinPtr->MinGlowIntensity);
		if (renderStep == kGlow) {
			s->setFloat(Shader::U_BloomScale, SkinPtr->GlowBloomScale);
			s->setFloat(Shader::U_BloomShift, SkinPtr->GlowBloomShift);
		}

		if(ModelPtr->Use(CLUT,OGL_SkinManager::Glowing)) {
			LoadModelSkin(SkinPtr->GlowImg, Collection, CLUT);
		}
		sprintathon_shaft_draw_elements(GL_TRIANGLES,(GLsizei)ModelPtr->Model.NumVI(),GL_UNSIGNED_SHORT,ModelPtr->Model.VIBase());
	}

	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	if (ModelPtr->Model.TxtrCoords.empty()) {
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	// Restore the default render sidedness
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);
	Shader::disable();
	return true;
}

void RenderRasterize_Shader::render_liquid_reflections(sorted_node_data* node,
    clipping_window_data* window,polygon_data* polygon,horizontal_surface_data* surface,RenderStep step)
{
    if(!graphics_preferences->liquid_sprite_reflections || step!=kDiffuse ||
       world_surface_pass==WorldSurfacePass::opaque || sprintathon_fast_shaft_source() ||
       current_player->infravision_duration || surface->media_type==_media_lava ||
       view->origin.z<=surface->height+1 || polygon->vertex_count<3)return;
    SprintathonDetailScope reflectionScope;
    using namespace SprintathonSpriteReflection;
    const double h=surface->height,ex=view->origin.x,ey=view->origin.y,ez=view->origin.z;
    double cx=0,cy=0;
    for(int i=0;i<polygon->vertex_count;++i) {
        const auto& p=get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
        cx+=p.x;cy+=p.y;
    }
    cx/=polygon->vertex_count;cy/=polygon->vertex_count;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    clip_to_window(window);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1.0f,-1.0f);
    SprintathonWaterNormals::bind();
    Shader* shader=Shader::get(Shader::S_WaterSpriteReflection);
    shader->enable();
    shader->setVector4(Shader::U_LiquidSpecEye,ex,ey,ez,0);
    shader->setFloat(Shader::U_Time,sprintathon_liquid_ripple_phase(surface->media_type,1.0,false));
    shader->setFloat(Shader::U_Pulsate,Get_OGL_ConfigureData().AnimatedMediaRipples ? 1.0f : 0.0f);
    int drawn=0;
    for(auto* object=node->exterior_objects;object && drawn<8;object=object->next_object) {
        // Include characters (and other players), using their current sprite frame.
        if(!(object->is_scenery || object->casts_character_shadow) ||
           !object->node || object->node->polygon_index!=node->polygon_index)continue;
        const auto& rect=object->rectangle;
        if(rect.ModelPtr || rect.transfer_mode!=_textured_transfer || rect.Opacity<=0)continue;
        double dx=rect.Position.x-ex,dy=rect.Position.y-ey;
        double distance=std::sqrt(dx*dx+dy*dy);
        if(distance>=12.0*WORLD_ONE)continue;
        double bottom=rect.Position.z+rect.WorldBottom*rect.Scale;
        double top=rect.Position.z+rect.WorldTop*rect.Scale;
        if(top<=h || top<=bottom || bottom-h>4.0*WORLD_ONE)continue;
        // Clip the source to the part above this pool before mirroring it.
        double clippedBottom=std::max(bottom,h),fraction=(clippedBottom-bottom)/(top-bottom);
        TextureManager texture;
        texture.ShapeDesc=rect.ShapeDesc;texture.LowLevelShape=rect.LowLevelShape;
        texture.Texture=rect.texture;texture.ShadingTables=rect.shading_tables;
        texture.TransferMode=rect.transfer_mode;texture.TransferData=rect.transfer_data;
        texture.IsShadeless=(rect.flags&_SHADELESS_BIT)!=0;texture.TextureType=OGL_Txtr_Inhabitant;
        if(!texture.Setup())continue;
        glActiveTextureARB(GL_TEXTURE0_ARB);texture.RenderNormal();texture.SetupTextureMatrix();
        double uTop=texture.U_Offset,uBottom=uTop+texture.U_Scale;
        double vLeft=texture.V_Offset,vRight=vLeft+texture.V_Scale;
        if(rect.flip_vertical)std::swap(uTop,uBottom);
        if(rect.flip_horizontal)std::swap(vLeft,vRight);
        uBottom+=(uTop-uBottom)*fraction;
        double yaw=view->virtual_yaw*FixedAngleToDegrees*0.017453292519943295;
        double axisX=-std::sin(yaw),axisY=std::cos(yaw);
        double left=rect.WorldLeft*rect.Scale*rect.HorizScale;
        double right=rect.WorldRight*rect.Scale*rect.HorizScale;
        auto vertex=[&](double side,double z,double u,double v) {
            return project(ex,ey,ez,rect.Position.x+axisX*side,rect.Position.y+axisY*side,z,h,u,v);
        };
        std::vector<Vertex> vertices={vertex(left,top,uTop,vLeft),vertex(right,top,uTop,vRight),
            vertex(right,clippedBottom,uBottom,vRight),vertex(left,clippedBottom,uBottom,vLeft)};
        for(int i=0;i<polygon->vertex_count && !vertices.empty();++i) {
            const auto& a=get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
            const auto& b=get_endpoint_data(polygon->endpoint_indexes[(i+1)%polygon->vertex_count])->vertex;
            double side=(b.x-a.x)*(cy-a.y)-(b.y-a.y)*(cx-a.x);
            vertices=clip(vertices,a.x,a.y,b.x,b.y,side>=0 ? 1.0 : -1.0);
        }
        if(vertices.size()>=3) {
            double fade=1.0-std::max(0.0,(distance-6.0*WORLD_ONE)/(6.0*WORLD_ONE));
            fade*=1.0-std::min(1.0,std::max(0.0,bottom-h)/(4.0*WORLD_ONE));
            float shade=texture.IsShadeless ? 1.0f : std::max(0.0f,std::min(1.0f,float(rect.ambient_shade)/FIXED_ONE));
            glColor4f(shade,shade,shade,float(fade)*rect.Opacity*
                (graphics_preferences->liquid_reflection_strength/100.0f));
            sprintathon_world_detail.select(10);sprintathon_world_detail.draw();
            SprintathonPortalTrace::get().draw(int(vertices.size()));
            glBegin(GL_TRIANGLE_FAN);
            for(const auto& p:vertices){glTexCoord4d(p.u,p.v,0,p.q);glVertex3d(p.x,p.y,h);}
            glEnd();++drawn;
            sprintathon_world_detail.select(4);
        }
        texture.RestoreTextureMatrix();
    }
    Shader::disable();glPopAttrib();glActiveTextureARB(GL_TEXTURE0_ARB);
}

void RenderRasterize_Shader::render_node_object(render_object_data *object, bool other_side_of_media, RenderStep renderStep) {
    if (world_surface_pass == WorldSurfacePass::opaque) return;

    if (!object->clipping_windows)
        return;

	clipping_window_data *win;

	// To properly handle sprites in media, we render above and below
	// the media boundary in separate passes, just like the original
	// software renderer.
	short media_index = get_polygon_data(object->node->polygon_index)->media_index;
	media_data *media = (media_index != NONE) ? get_media_data(media_index) : NULL;
	if (media) {
		float h = media->height;
		GLdouble plane[] = { 0.0, 0.0, 1.0, -h };
		if (view->under_media_boundary ^ other_side_of_media) {
			plane[2] = -1.0;
			plane[3] = h;
		}
		glClipPlane(GL_CLIP_PLANE5, plane);
		glEnable(GL_CLIP_PLANE5);
	} else if (other_side_of_media) {
		// When there's no media present, we can skip the second pass.
		return;
	}

    for (win = object->clipping_windows; win; win = win->next_window)
    {
        auto& setupTrace=SprintathonSetupTrace::get();
        if(setupTrace.active && media){
            ++setupTrace.mediaWindows;
            const auto& r=object->rectangle;
            if(r.ModelPtr || object->flight_monster_index!=NONE) ++setupTrace.mediaExcluded;
            else {
                // A sphere around the local quad contains it for every billboard
                // pitch/yaw. Banking about another pivot is excluded above.
                const double x=std::max(std::abs(double(r.WorldLeft)),std::abs(double(r.WorldRight)))*std::abs(double(r.HorizScale)*r.Scale);
                const double z=std::max(std::abs(double(r.WorldTop)),std::abs(double(r.WorldBottom)))*std::abs(double(r.Scale));
                const double radius=std::sqrt(x*x+z*z)+1.0;
                const bool below=view->under_media_boundary ^ other_side_of_media;
                if((below && r.Position.z-radius>media->height) ||
                   (!below && r.Position.z+radius<media->height)) ++setupTrace.outsideBase;
            }
        }
        clip_to_window(win);
        _render_node_object_helper(object, renderStep);
    }
    
    glDisable(GL_CLIP_PLANE5);
}

// Render-only history: use actual movement rather than nominal weapon speed.
// Tick-based sampling keeps trail length independent of rendering frame rate.
struct SprintathonProjectileBlurSample {
    short object_index = NONE, type = NONE;
    uint64_t tick = 0;
    int32 game_tick = -1;
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
};
static bool sprintathon_projectile_blur_motion(short index, float motion[3])
{
    static std::vector<SprintathonProjectileBlurSample> history;
    static int16 level = NONE;
    if (index < 0 || static_cast<size_t>(index) >= ProjectileList.size()) return false;
    if (level != dynamic_world->current_level_number) {
        history.clear();
        level = dynamic_world->current_level_number;
    }
    history.resize(ProjectileList.size());
    const auto& projectile = ProjectileList[index];
    if (!SLOT_IS_USED(&projectile) || projectile.object_index == NONE) return false;
    const auto *source = get_object_data(projectile.object_index);
    if (!source) return false;
    auto& sample = history[index];
    const uint64_t tick = sprintathon_projectile_render_tick();
    const int32 game_tick = dynamic_world->tick_count;
    const float x = source->location.x, y = source->location.y, z = source->location.z;
    if (sample.object_index != projectile.object_index || sample.type != projectile.type ||
        sample.game_tick < 0 || game_tick < sample.game_tick ||
        tick < sample.tick || tick - sample.tick > 4) {
        sample.object_index = projectile.object_index;
        sample.type = projectile.type;
        sample.tick = tick;
        sample.x = x; sample.y = y; sample.z = z;
        float initial[3] = {};
        sprintathon_projectile_render_motion(index, initial);
        sample.vx = initial[0]; sample.vy = initial[1]; sample.vz = initial[2];
    }
    if (tick > sample.tick) {
        const float elapsed = float(tick - sample.tick);
        sample.vx = (x - sample.x) / elapsed;
        sample.vy = (y - sample.y) / elapsed;
        sample.vz = (z - sample.z) / elapsed;
        sample.x = x; sample.y = y; sample.z = z;
        sample.tick = tick;
    }
    sample.game_tick = game_tick;
    const float speed = std::sqrt(sample.vx*sample.vx + sample.vy*sample.vy + sample.vz*sample.vz);
    if (speed < WORLD_ONE / 6.0f || speed > WORLD_ONE * 2.0f) return false;
    // A visible short exposure, with a compact cap and less blur in bullet time.
    const float exposure = sprintathon_bullet_time_active() ? 0.875f : 1.25f;
    const float length = std::min(speed * exposure, WORLD_ONE *
        (sprintathon_bullet_time_active() ? 0.90f : 0.45f));
    motion[0] = sample.vx * length / speed;
    motion[1] = sample.vy * length / speed;
    motion[2] = sample.vz * length / speed;
    return true;
}

// Diagnostic isolation: callers retain traversal and portal/media clipping.
// The entire helper is bypassed, including models and scenery-emitter collection.
static bool sprintathon_skip_world_object_helper(bool mainWorld)
{
    if(!mainWorld)return false;
    static const bool enabled=[](){
        const char* v=std::getenv("SPRINTATHON_SKIP_WORLD_OBJECT_HELPERS");
        const bool on=v && v[0]=='1';
        if(on)std::fprintf(stderr,"Object helper isolation: main-world helpers disabled, including models and helper-side emitter collection; simulation/traversal/reflections/weapons retained\n");
        return on;
    }();
    if(!enabled)return false;
    static uint64_t frame=0;
    static unsigned skipped=0;
    const auto now=SprintathonStartupTiming::get().frame_id;
    if(now!=frame){
        if(skipped && frame%60==0 && SprintathonStartupTiming::get().active())
            std::fprintf(stderr,"Object helper isolation frame %llu: skipped %u helper visits\n",(unsigned long long)frame,skipped);
        frame=now;skipped=0;
    }
    ++skipped;return true;
}

void RenderRasterize_Shader::_render_node_object_helper(render_object_data *object, RenderStep renderStep) {
    SprintathonObjectOps::Root objectOps(renderStep==kDiffuse && !sprintathon_shaft_source.active);
    if(sprintathon_skip_world_object_helper(
        renderStep==kDiffuse && !sprintathon_shaft_source.active))return;

    SprintathonSetupScope setupTime(SprintathonSetupTrace::Object);
    SprintathonDetailScope detailScope;
    const int objectKind=object->is_scenery?0:(object->casts_character_shadow?1:(object->projectile_index!=NONE?2:3));
    const int objectCategory=objectKind==0?5:(objectKind==1?6:(objectKind==2?7:3));
    sprintathon_object_counts.visit(object,objectKind);

	rectangle_definition& rect = object->rectangle;
    const short projectile_index = object->projectile_index;
    const bool flame = projectile_index >= 0 &&
        static_cast<size_t>(projectile_index) < ProjectileList.size() &&
        SLOT_IS_USED(&ProjectileList[projectile_index]) &&
        ProjectileList[projectile_index].type == _projectile_flamethrower_burst &&
        rect.transfer_mode == _textured_transfer;

	const world_point3d& pos = rect.Position;
    
	if(rect.ModelPtr) {
		glPushMatrix();
		glTranslated(pos.x, pos.y, pos.z);
		glRotated((360.0/FULL_CIRCLE)*rect.Azimuth,0,0,1);
		GLfloat HorizScale = rect.Scale*rect.HorizScale;
		glScalef(HorizScale,HorizScale,rect.Scale);

		short descriptor = GET_DESCRIPTOR_COLLECTION(rect.ShapeDesc);
		short collection = GET_COLLECTION(descriptor);
		short clut = ModifyCLUT(rect.transfer_mode,GET_COLLECTION_CLUT(descriptor));

        sprintathon_world_detail.select(11);
		SprintathonObjectOps::measure(SprintathonObjectOps::Models, [&](){return RenderModel(rect, collection, clut, weaponFlare, selfLuminosity,
			renderStep,
			static_cast<GLsizei>(SprintathonRenderPixels(view->screen_width)),
			static_cast<GLsizei>(SprintathonRenderPixels(view->screen_height)));});
		glPopMatrix();
		return;
	}

    SprintathonSpriteDrawIsolation isolateSpriteDraws(
        renderStep==kDiffuse && !sprintathon_shaft_source.active);

	glPushMatrix();
	glTranslated(pos.x, pos.y, pos.z);

	double yaw = view->virtual_yaw * FixedAngleToDegrees;
	glRotated(yaw, 0.0, 0.0, 1.0);

			
	float offset = 0;
	const bool sprintathon_strict_sprite_depth =
		!view->mimic_sw_perspective &&
		input_preferences->sprintathon_enabled &&
		input_preferences->sprintathon_mouselook_mode > 0;
	if (OGL_ForceSpriteDepth() || sprintathon_strict_sprite_depth) {
		// look for parasitic objects based on y position,
		// and offset them to draw in proper depth order
		if(pos.y == objectY) {
			objectCount++;
			offset = objectCount * -1.0;
		} else {
			objectCount = 0;
			objectY = pos.y;
		}
	} else {
		glDisable(GL_DEPTH_TEST);
	}

	auto TMgr = setupSpriteTexture(rect, OGL_Txtr_Inhabitant, offset, renderStep);
	if (TMgr->ShapeDesc == UNONE) { glPopMatrix(); return; }
    if (renderStep == kDiffuse && !sprintathon_fast_shaft_source() &&
        !current_player->infravision_duration && rect.transfer_mode == _textured_transfer &&
        !(rect.flags & _SHADELESS_BIT) && object->node &&
        (sprintathon_shaft_source.active || !(sprintathon_object_light_isolation() & SprintathonSkipActorBake))) {
        SprintathonObjectOps::measure(SprintathonObjectOps::Lighting, [&](){return SprintathonSkyBake::bindActor(Shader::get(Shader::S_Sprite),object->node->polygon_index,
            SprintathonSkyBake::V(pos.x,pos.y,pos.z+(rect.WorldTop+rect.WorldBottom)*rect.Scale*0.5));});
    }
    if (renderStep == kDiffuse && object->projectile_index != NONE &&
        object->projectile_index >= 0 &&
        static_cast<size_t>(object->projectile_index) < ProjectileList.size()) {
        float visual[3];
        if (TMgr->GetProjectileVisualColor(visual)) {
            if (sprintathon_projectile_visuals.size() < ProjectileList.size())
                sprintathon_projectile_visuals.resize(ProjectileList.size(),
                    SprintathonProjectileVisual{NONE, NONE, {0, 0, 0}});
            auto& cached = sprintathon_projectile_visuals[object->projectile_index];
            cached.object_index = ProjectileList[object->projectile_index].object_index;
            cached.projectile_type = ProjectileList[object->projectile_index].type;
            cached.rgb[0] = visual[0]; cached.rgb[1] = visual[1]; cached.rgb[2] = visual[2];
        }
    }


	if (!sprintathon_fast_shaft_source() && object->is_scenery && renderStep == kDiffuse &&
        (sprintathon_shaft_source.active || !(sprintathon_object_light_isolation() & SprintathonSkipActorEmitters))) {
        SprintathonObjectOps::Scope opsEmitters(SprintathonObjectOps::Emitters);
        float bright_u, bright_v, bright_rgb[3];
        if ((graphics_preferences->bright_scenery_lights || graphics_preferences->ceiling_fog_cones) &&
            TMgr->GetBrightEmission(bright_u, bright_v, bright_rgb)) {
            // Scale the height from the actual scenery frame. Its horizontal
            // hotspot stays at the object location for camera-facing sprites.
            const float bottom = rect.WorldBottom * rect.Scale;
            const float top = rect.WorldTop * rect.Scale;
            const float light_z = pos.z + bottom + (top - bottom) * (1.0f - bright_v);
            sprintathon_collect_fog_cone(object->scenery_object_index, light_z, bright_rgb);
            { SprintathonSetupScope preparationTime(SprintathonSetupTrace::EmitterRecord);
                sprintathon_record_texture_light(TMgr.get(), pos.x, pos.y, light_z, renderStep, true,
                false, NONE, 0.0f, object->scenery_object_index, NONE,
                0.25f*std::sqrt(float(rect.WorldRight-rect.WorldLeft)*float(rect.WorldRight-rect.WorldLeft)+
                                float(rect.WorldTop-rect.WorldBottom)*float(rect.WorldTop-rect.WorldBottom))*std::abs(rect.Scale));
            }
        }
    }

if (!view->mimic_sw_perspective)
	{
		if (TMgr->ForceXYBillboard() ||
			(view->billboard_xy && !TMgr->ForceYBillboard()))
		{
			glRotated(view->virtual_pitch * FixedAngleToDegrees, 0.0, -1.0, 0.0);
		}
	}

    if (object->flight_monster_index != NONE) {
        const float bank = sprintathon_flight_bank(object->flight_monster_index,
            float(yaw*0.017453292519943295));
        const float center = (rect.WorldTop+rect.WorldBottom)*rect.Scale*0.5f;
        glTranslatef(0, 0, center);
        glRotatef(bank, 1, 0, 0);
        glTranslatef(0, 0, -center);
    }

	float texCoords[2][2];

	if(rect.flip_vertical) {
		texCoords[0][1] = TMgr->U_Offset;
		texCoords[0][0] = TMgr->U_Scale+TMgr->U_Offset;
	} else {
		texCoords[0][0] = TMgr->U_Offset;
		texCoords[0][1] = TMgr->U_Scale+TMgr->U_Offset;
	}

	if(rect.flip_horizontal) {
		texCoords[1][1] = TMgr->V_Offset;
		texCoords[1][0] = TMgr->V_Scale+TMgr->V_Offset;
	} else {
		texCoords[1][0] = TMgr->V_Offset;
		texCoords[1][1] = TMgr->V_Scale+TMgr->V_Offset;
	}

	if(TMgr->IsBlended() || TMgr->TransferMode == _tinted_transfer) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

	GLfloat vertex_array[12] = {
		0,
		rect.WorldLeft * rect.HorizScale * rect.Scale,
		rect.WorldTop * rect.Scale,
		0,
		rect.WorldRight * rect.HorizScale * rect.Scale,
		rect.WorldTop * rect.Scale,
		0,
		rect.WorldRight * rect.HorizScale * rect.Scale,
		rect.WorldBottom * rect.Scale,
		0,
		rect.WorldLeft * rect.HorizScale * rect.Scale,
		rect.WorldBottom * rect.Scale
	};

	GLfloat texcoord_array[8] = {
		texCoords[0][0],
		texCoords[1][0],
		texCoords[0][0],
		texCoords[1][1],
		texCoords[0][1],
		texCoords[1][1],
		texCoords[0][1],
		texCoords[1][0]
	};

	glVertexPointer(3, GL_FLOAT, 0, vertex_array);
	glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);

    if (sprintathon_fast_shaft_source() && renderStep == kDiffuse) {
        sprintathon_shaft_draw_arrays(GL_QUADS,0,4);
        glEnable(GL_DEPTH_TEST);
        glPopMatrix();
        Shader::disable();
        TMgr->RestoreTextureMatrix();
        return;
    }

    float dodge_motion[3] = {0, 0, 0};
    const bool dodge_blur = object->flight_monster_index != NONE &&
        sprintathon_flight_dodge_motion(object->flight_monster_index, dodge_motion);
    if (renderStep == kDiffuse && !sprintathon_shaft_source.active &&
        !flame && (graphics_preferences->projectile_motion_blur || dodge_blur) &&
        rect.transfer_mode == _textured_transfer)
    {
        float motion[3];
        bool moving = dodge_blur;
        if (dodge_blur) std::copy(dodge_motion, dodge_motion+3, motion);
        else moving = sprintathon_projectile_blur_motion(object->projectile_index, motion);
        if (!moving && object->projectile_index == NONE) {
            const float *velocity = object->projectile_trail_motion;
            const float speed = std::sqrt(velocity[0]*velocity[0] +
                velocity[1]*velocity[1] + velocity[2]*velocity[2]);
            if (speed >= WORLD_ONE / 16.0f && speed <= WORLD_ONE * 2.0f) {
                const float exposure = sprintathon_bullet_time_active() ? 0.875f : 1.25f;
                const float length = std::min(speed * exposure, WORLD_ONE *
                    (sprintathon_bullet_time_active() ? 0.90f : 0.45f));
                for (int i = 0; i < 3; ++i) motion[i] = velocity[i] * length / speed;
                moving = true;
            }
        }
        if (moving) {
            // Convert the world trail to the current billboard's local axes.
            const float angle = float(yaw * 0.017453292519943295);
            const float along = std::cos(angle)*motion[0] + std::sin(angle)*motion[1];
            const float sideways = -std::sin(angle)*motion[0] + std::cos(angle)*motion[1];
            const bool tilted = !view->mimic_sw_perspective &&
                (TMgr->ForceXYBillboard() || (view->billboard_xy && !TMgr->ForceYBillboard()));
            const float pitch = tilted ? float(view->virtual_pitch * FixedAngleToDegrees *
                0.017453292519943295) : 0.0f;

            const float local_z = -std::sin(pitch)*along + std::cos(pitch)*motion[2];
            const float left = vertex_array[1], right = vertex_array[4];
            const float top = vertex_array[2], bottom = vertex_array[8];
            if (right > left && top > bottom &&
                std::abs(sideways) + std::abs(local_z) > 0.01f) {
                const float blur_left = std::min(left, left - sideways);
                const float blur_right = std::max(right, right - sideways);
                const float blur_top = std::max(top, top - local_z);
                const float blur_bottom = std::min(bottom, bottom - local_z);
                GLfloat smear_vertices[12] = {0, blur_left, blur_top,
                    0, blur_right, blur_top, 0, blur_right, blur_bottom,
                    0, blur_left, blur_bottom};
                GLfloat smear_uv[8];
                for (int i = 0; i < 4; ++i) {
                    smear_uv[2*i] = texCoords[0][0] +
                        (top - smear_vertices[3*i+2]) / (top - bottom) *
                        (texCoords[0][1] - texCoords[0][0]);
                    smear_uv[2*i+1] = texCoords[1][0] +
                        (smear_vertices[3*i+1] - left) / (right - left) *
                        (texCoords[1][1] - texCoords[1][0]);
                }
                // Substituted textures can rotate/swap UV axes. Match that matrix
                // for both the sampling direction and the sprite's valid bounds.
                GLfloat matrix[16];
                SprintathonObjectOps::measure(SprintathonObjectOps::StateRead, [&](){return glGetFloatv(GL_TEXTURE_MATRIX, matrix);});
                const float du = -local_z / (top - bottom) *
                    (texCoords[0][1] - texCoords[0][0]);
                const float dv = sideways / (right - left) *
                    (texCoords[1][1] - texCoords[1][0]);
                float u_min = 1e30f, v_min = 1e30f, u_max = -1e30f, v_max = -1e30f;
                for (int i = 0; i < 4; ++i) {
                    const float u = matrix[0]*texcoord_array[2*i] +
                        matrix[4]*texcoord_array[2*i+1] + matrix[12];
                    const float v = matrix[1]*texcoord_array[2*i] +
                        matrix[5]*texcoord_array[2*i+1] + matrix[13];
                    u_min = std::min(u_min, u); u_max = std::max(u_max, u);
                    v_min = std::min(v_min, v); v_max = std::max(v_max, v);
                }
                Shader *smear = Shader::get(current_player->infravision_duration ?
                    Shader::S_SpriteInfravision : Shader::S_Sprite);
                smear->setVector4(Shader::U_ProjectileBlurVector,
                    matrix[0]*du + matrix[4]*dv, matrix[1]*du + matrix[5]*dv, 1,
                    sprintathon_bullet_time_active() ? 48.0f : 16.0f);
                smear->setVector4(Shader::U_ProjectileBlurBounds, u_min, v_min, u_max, v_max);
                GLfloat color[4];
                SprintathonObjectOps::measure(SprintathonObjectOps::StateRead, [&](){return glGetFloatv(GL_CURRENT_COLOR, color);});
                glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER, 0.001f);
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glColor4f(color[0], color[1], color[2], color[3]);
                glVertexPointer(3, GL_FLOAT, 0, smear_vertices);
                glTexCoordPointer(2, GL_FLOAT, 0, smear_uv);
                sprintathon_world_detail.select(9);
                sprintathon_shaft_draw_arrays(GL_QUADS, 0, 4);
                sprintathon_world_detail.select(4);
                smear->setVector4(Shader::U_ProjectileBlurVector, 0, 0, 0, 0);
                glPopAttrib();
                glVertexPointer(3, GL_FLOAT, 0, vertex_array);
                glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);
            }
        }
    }

    // Capture once per view, then reuse the background for all flame sprites.
    // A dedicated texture prevents intervening invisibility draws overwriting it.
    static GLuint flame_scene = 0;
    static GLint flame_width = 0, flame_height = 0;
    GLint flame_viewport[4] = {0,0,0,0};
    const bool refract_flame = flame && renderStep == kDiffuse && !sprintathon_shaft_source.active;
    if (refract_flame) {
        SprintathonObjectOps::Scope opsRefraction(SprintathonObjectOps::Refraction);
        SprintathonObjectOps::measure(SprintathonObjectOps::StateRead, [&](){return glGetIntegerv(GL_VIEWPORT, flame_viewport);});
        const GLint width = flame_viewport[0]+flame_viewport[2];
        const GLint height = flame_viewport[1]+flame_viewport[3];
        glActiveTextureARB(GL_TEXTURE2_ARB);
        if (!flame_scene) glGenTextures(1, &flame_scene);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, flame_scene);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (width != flame_width || height != flame_height) {
            glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            flame_width = width; flame_height = height;
            SprintathonStartupPhase light_prepare(SprintathonStartupTiming::get().light_prepare_ms);
    sprintathon_flame_scene_captured = false;
        }
        if (!sprintathon_flame_scene_captured) {
            glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,0,0,width,height);
            sprintathon_flame_scene_captured = true;
        }
        glActiveTextureARB(GL_TEXTURE0_ARB);
    }
    Shader *flame_shader = nullptr;
    if (flame) {
        flame_shader = Shader::get(renderStep == kGlow ? Shader::S_SpriteBloom :
            (current_player->infravision_duration ? Shader::S_SpriteInfravision : Shader::S_Sprite));
        GLfloat matrix[16];
        SprintathonObjectOps::measure(SprintathonObjectOps::StateRead, [&](){return glGetFloatv(GL_TEXTURE_MATRIX, matrix);});
        float u0 = 1e30f, v0 = 1e30f, u1 = -1e30f, v1 = -1e30f;
        for (int i = 0; i < 4; ++i) {
            const float u = matrix[0]*texcoord_array[2*i] + matrix[4]*texcoord_array[2*i+1] + matrix[12];
            const float v = matrix[1]*texcoord_array[2*i] + matrix[5]*texcoord_array[2*i+1] + matrix[13];
            u0 = std::min(u0, u); u1 = std::max(u1, u);
            v0 = std::min(v0, v); v1 = std::max(v1, v);
        }
        flame_shader->setVector4(Shader::U_FlameBounds, u0, v0, u1, v1);
        flame_shader->setVector4(Shader::U_FlameRipple, 1,
            float(dynamic_world->tick_count % (TICKS_PER_SECOND * 3600)) / TICKS_PER_SECOND,
            float(projectile_index) * 2.39996f, refract_flame ? 1.0f : 0.0f);
        flame_shader->setFloat(Shader::U_PixelWidth, float(flame_width));
        flame_shader->setFloat(Shader::U_PixelHeight, float(flame_height));
        glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glAlphaFunc(GL_GREATER, 0.001f);
        glDepthMask(GL_FALSE);
    }
    sprintathon_world_detail.select(objectCategory);
    if(sprintathon_object_counts.enabled){++sprintathon_object_counts.baseDraws;
        if(!OGL_ForceSpriteDepth() && !sprintathon_strict_sprite_depth)++sprintathon_object_counts.depthless;}
	sprintathon_shaft_draw_arrays(GL_QUADS, 0, 4);
    sprintathon_world_detail.select(4);
    if (flame) {
        flame_shader->setVector4(Shader::U_FlameRipple, 0, 0, 0, 0);
        glPopAttrib();
    }

	if (!flame && setupGlow(view, TMgr, 0, 1, weaponFlare, selfLuminosity, offset, renderStep)) {
        sprintathon_world_detail.select(8);
		sprintathon_shaft_draw_arrays(GL_QUADS, 0, 4);
	}
        
	glEnable(GL_DEPTH_TEST);
	glPopMatrix();

	// Draw a consistent soft elliptical contact shadow on the polygon floor.
	// Restrict this first pass to ordinary textured sprites: effects, static,
	// and invisible transfer modes otherwise produce distracting dark flashes.
	if (renderStep == kDiffuse &&
		Get_OGL_ConfigureData().SpriteShadows &&
		object->casts_character_shadow &&
		rect.transfer_mode == _textured_transfer &&
		!(rect.flags & _SHADELESS_BIT) &&
		object->node && object->node->polygon_index != NONE)
	{
		polygon_data *polygon = get_polygon_data(object->node->polygon_index);
		if (polygon)
		{
			const float scale = rect.Scale;
			const float feet_z = pos.z + rect.WorldBottom * scale;
			const float height_above_floor = std::max(0.0f,
				static_cast<float>(feet_z - polygon->floor_height));
			const float height_fade = 1.0f - std::min(1.0f,
				height_above_floor / (1.5f * WORLD_ONE));
			const float sprite_width = std::abs(
				(rect.WorldRight - rect.WorldLeft) * rect.HorizScale * scale);
			const float shadow_width =
				sprite_width * 0.82f + height_above_floor * 0.12f;
			const float shadow_depth =
				shadow_width * 0.52f + height_above_floor * 0.06f;

			if (height_fade > 0.01f && shadow_width > 1.0f && shadow_depth > 1.0f)
			{
				Shader *shadow = Shader::get(Shader::S_SpriteShadow);
				shadow->enable();
				shadow->setFloat(Shader::U_ObjectWorldZ,
					static_cast<float>(polygon->floor_height));

				glPushMatrix();
				glTranslated(pos.x, pos.y, polygon->floor_height + 1.0);
				glRotated((360.0 / FULL_CIRCLE) * rect.Azimuth, 0.0, 0.0, 1.0);

				const GLfloat half_width = shadow_width * 0.5f;
				const GLfloat half_depth = shadow_depth * 0.5f;
				GLfloat shadow_vertices[12] = {
					half_depth, -half_width, 0.0f,
					half_depth,  half_width, 0.0f,
					-half_depth, half_width, 0.0f,
					-half_depth, -half_width, 0.0f
				};
				const GLfloat shadow_texcoords[8] = {
					0.0f, 0.0f,
					1.0f, 0.0f,
					1.0f, 1.0f,
					0.0f, 1.0f
				};
				glVertexPointer(3, GL_FLOAT, 0, shadow_vertices);
				glTexCoordPointer(2, GL_FLOAT, 0, shadow_texcoords);
				glColor4f(0.0f, 0.0f, 0.0f, 0.38f * height_fade);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glEnable(GL_ALPHA_TEST);
				glAlphaFunc(GL_GREATER, 0.005f);
                sprintathon_world_detail.select(12);
				sprintathon_shaft_draw_arrays(GL_QUADS, 0, 4);
				glPopMatrix();
			}
		}
	}
	Shader::disable();
	TMgr->RestoreTextureMatrix();
}

extern void position_sprite_axis(short *x0, short *x1, short scale_width, short screen_width, short positioning_mode, _fixed position, bool flip, world_distance world_left, world_distance world_right);

extern GLdouble Screen_2_Clip[16];

static void render_slide_legs(view_data *view, RenderStep renderStep)
{
	const bool back_dodge_active =
		current_player &&
		current_player->dodge_last_direction == 2 &&
		current_player->dodge_ticks_remaining > 0;
	const bool back_dodge_recovering =
		current_player &&
		current_player->dodge_last_direction == 2 &&
		current_player->back_dodge_recovery_ticks > 0;
	const bool back_dodge =
		back_dodge_active || back_dodge_recovering;

	/*
	 * Keep this visual state separate from gameplay. The legs respond more
	 * slowly than the weapon, creating the impression that the camera/head
	 * turns first and the body is dragged behind it.
	 */
	static float smooth_back_dodge_drag_x = 0.0f;
	static bool back_dodge_was_active = false;

	if (!back_dodge)
	{
		smooth_back_dodge_drag_x = 0.0f;
		back_dodge_was_active = false;
	}
	else if (!back_dodge_was_active)
	{
		smooth_back_dodge_drag_x = 0.0f;
		back_dodge_was_active = true;
	}

	if (renderStep != kDiffuse ||
		!input_preferences->sprintathon_enabled ||
		!current_player ||
		(!back_dodge &&
		 (!input_preferences->sprintathon_slide ||
		  (current_player->slide_ticks_remaining == 0 &&
		   !current_player->flying_kick_active &&
		   current_player->flying_kick_landing_ticks == 0 &&
		   current_player->flying_kick_exit_ticks == 0))))
	{
		return;
	}

	static OGL_Blitter slide_legs;
	static OGL_Blitter front_legs;
	static bool load_attempted = false;
	static bool front_load_attempted = false;

	if (!load_attempted)
	{
		load_attempted = true;
		FileSpecifier file("gfx/slidelegs.png");

		if (!file.Exists() &&
			!file.SetNameWithPath("Sprintathon/slidelegs.png"))
		{
			file = FileSpecifier(
				get_data_path(kPathDefaultData) +
				"/Sprintathon/slidelegs.png");
		}

		if (file.Exists())
		{
			ImageDescriptor image;
			if (image.LoadFromFile(file, ImageLoader_Colors, 0))
				slide_legs.Load(image);
		}
	}

	if (!front_load_attempted)
	{
		front_load_attempted = true;
		FileSpecifier file("gfx/frontlegs.png");

		if (!file.Exists() &&
			!file.SetNameWithPath("Sprintathon/frontlegs.png"))
		{
			file = FileSpecifier(
				get_data_path(kPathDefaultData) +
				"/Sprintathon/frontlegs.png");
		}

		if (file.Exists())
		{
			ImageDescriptor image;
			if (image.LoadFromFile(file, ImageLoader_Colors, 0))
				front_legs.Load(image);
		}
	}

	OGL_Blitter& visible_legs =
		(back_dodge || (current_player->slide_roll_used && current_player->slide_ticks_remaining > 0)) ? front_legs : slide_legs;

	if (!visible_legs.Loaded())
		return;

	constexpr int slide_duration = (TICKS_PER_SECOND * 3) / 2;
	constexpr int back_dodge_duration = 12;
	constexpr int back_dodge_recovery_duration = 26;
	const bool flying_kick = current_player->flying_kick_active;
	const bool kick_landing =
		current_player->flying_kick_landing_ticks > 0;
	const bool kick_exit = current_player->flying_kick_exit_ticks > 0;
	const int ticks_remaining = flying_kick ? slide_duration :
		kick_landing ?
			static_cast<int>(current_player->flying_kick_landing_ticks) :
		kick_exit ?
			static_cast<int>(current_player->flying_kick_exit_ticks) :
		back_dodge_active ?
			A1_PIN(static_cast<int>(current_player->dodge_ticks_remaining),
				0, back_dodge_duration) :
		back_dodge_recovering ?
			A1_PIN(static_cast<int>(
				current_player->back_dodge_recovery_ticks),
				0, back_dodge_recovery_duration) :
			A1_PIN(static_cast<int>(current_player->slide_ticks_remaining),
				0, slide_duration);
	const int ticks_elapsed = back_dodge_active ?
		back_dodge_duration - ticks_remaining :
		back_dodge_recovering ?
			back_dodge_duration :
		flying_kick ?
			static_cast<int>(current_player->flying_kick_ticks) :
			slide_duration - ticks_remaining;

	/*
	 * Use position rather than transparency for the animation. The legs
	 * rise quickly at the start, remain fully visible, then drop rapidly
	 * below the screen over the final eight ticks.
	 */
	float slide_in = A1_PIN(ticks_elapsed / 4.0f, 0.0f, 1.0f);
	float slide_out = flying_kick ? 1.0f :
		back_dodge_active ? 1.0f :
		back_dodge_recovering ?
			A1_PIN(
				ticks_remaining /
					static_cast<float>(back_dodge_recovery_duration),
				0.0f, 1.0f) :
		A1_PIN(ticks_remaining /
			(kick_landing ? 12.0f : 8.0f), 0.0f, 1.0f);

	slide_in =
		slide_in * slide_in * (3.0f - 2.0f * slide_in);
	slide_out =
		slide_out * slide_out * (3.0f - 2.0f * slide_out);

	const float visibility =
		std::min(slide_in, slide_out);

	const float sprite_height = view->screen_height * 0.78f;
	const float sprite_width = sprite_height *
		static_cast<float>(visible_legs.UnscaledWidth()) /
		static_cast<float>(visible_legs.UnscaledHeight());
	// Keep most of the body below the frame at level pitch. Looking down
	// progressively reveals it, while the slide animation moves it up from
	// below rather than abruptly appearing in the middle of the view.
	/*
	 * Follow the player's live aim instead of the pitch stored in the
	 * current rendered view. This lets the body remain visually attached
	 * to the floor while the player looks around during the slide.
	 */
	const fixed_angle live_pitch =
		FIXED_INTEGERAL_PART(
			current_player->variables.elevation) * FIXED_ONE +
		virtual_aim_delta().pitch;

	const float downward_degrees =
		-static_cast<float>(live_pitch) * FixedAngleToDegrees;

	// Use both downward and upward pitch so the body travels continuously
	// rather than stopping at its level-view position.
	const float pitch_position =
		A1_PIN((downward_degrees + 45.0f) / 120.0f, 0.0f, 1.0f);

	float floor_reveal =
		pitch_position * pitch_position *
		(3.0f - 2.0f * pitch_position);

	/*
	 * Looking upward leaves only the boots at the bottom edge. Looking
	 * downward brings nearly the entire body into view.
	 */
	const float revealed_fraction = back_dodge ?
		0.08f + 0.92f * floor_reveal :
		(flying_kick || kick_exit) ?
			0.32f + 0.68f * floor_reveal :
			0.12f + 0.88f * floor_reveal;

	// A small independent offset creates the entrance from below without
	// allowing the fade animation to hide the sprite completely.
	float slide_in_offset =
		(1.0f - visibility) * sprite_height * 0.15f;

	/*
	 * Once back-dodge recovery starts, physically sweep the complete body
	 * below the frame instead of making it appear to vanish at the edge.
	 * Six ticks gives a quick but still readable downward motion.
	 */
	if (back_dodge_recovering)
	{
		constexpr float exit_ticks = 6.0f;
		float exit_progress = A1_PIN(
			(back_dodge_recovery_duration -
			 current_player->back_dodge_recovery_ticks) / exit_ticks,
			0.0f,
			1.0f);

		exit_progress =
			exit_progress * exit_progress *
			(3.0f - 2.0f * exit_progress);

		slide_in_offset +=
			exit_progress * sprite_height * 1.10f;
	}

	float back_dodge_drag_x = 0.0f;
	if (back_dodge)
	{
		const int horizontal_velocity =
			FIXED_INTEGERAL_PART(
				current_player->variables.angular_velocity);
		const int maximum_drag = view->screen_width / 7;
		const int target_drag = A1_PIN(
			(-horizontal_velocity * view->screen_width) / 192,
			-maximum_drag,
			maximum_drag);

		// Slower than the weapon's 0.14 follow speed.
		constexpr float leg_drag_follow_speed = 0.035f;
		smooth_back_dodge_drag_x +=
			(target_drag - smooth_back_dodge_drag_x) *
			leg_drag_follow_speed;
		back_dodge_drag_x = smooth_back_dodge_drag_x;
	}

	const Image_Rect destination(
		(view->screen_width - sprite_width) * 0.5f +
			back_dodge_drag_x,
		view->screen_height -
			sprite_height * revealed_fraction +
			slide_in_offset,
		sprite_width,
		sprite_height);

	// Match the first-person weapon's lighting source: the floor light of the
	// polygon containing the camera. Keep alpha independent so the entrance
	// and exit remain position-only animations.
	const _fixed polygon_light = get_light_intensity(
		get_polygon_data(view->origin_polygon_index)->floor_lightsource_index);
	const float light_shade = A1_PIN(
		static_cast<float>(polygon_light) / static_cast<float>(FIXED_ONE),
		0.0f,
		1.0f);
	visible_legs.tint_color_r = light_shade;
	visible_legs.tint_color_g = light_shade;
	visible_legs.tint_color_b = light_shade;
	visible_legs.tint_color_a = 1.0f;
	visible_legs.rotation = 0.0f;
	Shader::disable();
	visible_legs.Draw(destination);
}

void RenderRasterize_Shader::render_viewer_sprite_layer(RenderStep renderStep)
{
        if (!view->show_weapons_in_hand) return;
	if (sprintathon_single_pistol_zoom_active()) return;
    
        glMatrixMode(GL_TEXTURE);
        glPushMatrix();
    
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadMatrixd(Screen_2_Clip);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

	// Draw the sliding body beneath the normal first-person weapon sprites.
	render_slide_legs(view, renderStep);

        rectangle_definition rect;
	weapon_display_information display_data;
	shape_information_data *shape_information;
	short count;

	/*
	 * Briefly tuck the weapon away while either mantle system pulls the
	 * player over an edge. Calculate this once per rendered frame so paired
	 * weapons receive exactly the same offset.
	 */
	static float mantle_weapon_lower = 0.0f;
	const bool sprintathon_mantling =
		current_player &&
		input_preferences->sprintathon_enabled &&
		((current_player->variables.flags&_DRY_MANTLING_BIT) ||
		 (current_player->variables.flags&_WATER_MANTLING_BIT));
	const float mantle_lower_target = sprintathon_mantling ?
		static_cast<float>(view->screen_height) * 0.30f : 0.0f;
	const float mantle_ease =
		mantle_lower_target > mantle_weapon_lower ? 0.38f : 0.30f;
	mantle_weapon_lower +=
		(mantle_lower_target - mantle_weapon_lower) * mantle_ease;
	const short mantle_lower_offset =
		static_cast<short>(mantle_weapon_lower);

        rect.ModelPtr = nullptr;
        rect.Opacity = 1;

        /* get_weapon_display_information() returns true if there is a weapon to be drawn.  it
           should initially be passed a count of zero.  it returns the weapon’s texture and
           enough information to draw it correctly. */
	count= 0;
	while (get_weapon_display_information(&count, &display_data))
	{
		/* fetch relevant shape data */
                shape_information= extended_get_shape_information(display_data.collection, display_data.low_level_shape_index);

                // Nonexistent frame: skip
		if (!shape_information) continue;
		
		// LP change: for the convenience of the OpenGL renderer
		rect.ShapeDesc = BUILD_DESCRIPTOR(display_data.collection,0);
		rect.LowLevelShape = display_data.low_level_shape_index;

		if (shape_information->flags&_X_MIRRORED_BIT) display_data.flip_horizontal= !display_data.flip_horizontal;
		if (shape_information->flags&_Y_MIRRORED_BIT) display_data.flip_vertical= !display_data.flip_vertical;

		/* calculate shape rectangle */
		position_sprite_axis(&rect.x0, &rect.x1, view->screen_height, view->screen_width, display_data.horizontal_positioning_mode,
			display_data.horizontal_position, display_data.flip_horizontal, shape_information->world_left, shape_information->world_right);
		position_sprite_axis(&rect.y0, &rect.y1, view->screen_height, view->screen_height, display_data.vertical_positioning_mode,
			display_data.vertical_position, display_data.flip_vertical, -shape_information->world_top, -shape_information->world_bottom);

		if (input_preferences->sprintathon_enabled)
		{
		// Experimental: shrink first-person weapon around bottom-centre.
		constexpr int weapon_scale_percent = 82;
		const int weapon_anchor_x = view->screen_width / 2;
		const int weapon_anchor_y = view->screen_height;
		rect.x0 = weapon_anchor_x + (rect.x0 - weapon_anchor_x) * weapon_scale_percent / 100;
		rect.x1 = weapon_anchor_x + (rect.x1 - weapon_anchor_x) * weapon_scale_percent / 100;
		rect.y0 = weapon_anchor_y + (rect.y0 - weapon_anchor_y) * weapon_scale_percent / 100;
		rect.y1 = weapon_anchor_y + (rect.y1 - weapon_anchor_y) * weapon_scale_percent / 100;

		// Subtle viewmodel sway opposite the current camera movement.
		const int horizontal_velocity =
			FIXED_INTEGERAL_PART(
				local_player->variables.angular_velocity);
		const int vertical_velocity =
			FIXED_INTEGERAL_PART(
				local_player->variables.vertical_angular_velocity);

		/*
		 * Stronger viewmodel sway, scaled with resolution.
		 *
		 * Static visual state is intentionally renderer-local: it does
		 * not affect gameplay, networking or replay determinism.
		 */
		const int maximum_sway_x = view->screen_width / 16;
		const int maximum_sway_y = view->screen_height / 14;

		const int target_sway_x = A1_PIN(
			(-horizontal_velocity * view->screen_width) / 256,
			-maximum_sway_x,
			maximum_sway_x);
		const int target_sway_y = A1_PIN(
			(vertical_velocity * view->screen_height) / 192,
			-maximum_sway_y,
			maximum_sway_y);

		static float smooth_sway_x = 0.0f;
		static float smooth_sway_y = 0.0f;

		// Lower values are smoother but produce more visual lag.
		constexpr float sway_follow_speed = 0.14f;

		smooth_sway_x +=
			(target_sway_x - smooth_sway_x) * sway_follow_speed;
		smooth_sway_y +=
			(target_sway_y - smooth_sway_y) * sway_follow_speed;

		const int weapon_sway_x =
			static_cast<int>(smooth_sway_x);
		const int weapon_sway_y =
			static_cast<int>(smooth_sway_y);

		rect.x0 += weapon_sway_x;
		rect.x1 += weapon_sway_x;
		rect.y0 += weapon_sway_y;
		rect.y1 += weapon_sway_y;

		// Let the airborne camera lean carry the weapon slightly farther in the
		// same direction. The actual rotation is applied around its bottom edge.
		if (current_player)
		{
			const int strafe_weapon_offset=
				(static_cast<int>(current_player->sprintathon_strafe_roll) *
				 view->screen_width * 3) /
				(FULL_CIRCLE * 40);
			rect.x0 += strafe_weapon_offset;
			rect.x1 += strafe_weapon_offset;
		}

		/*
		 * Keep the weapon upright and anchored at the bottom during a
		 * cartwheel, but let it swing heavily against the camera rotation.
		 */
		if (current_player && current_player->cartwheel_active)
		{
			const angle cartwheel_phase= NORMALIZE_ANGLE(
				static_cast<angle>(current_player->cartwheel_camera_roll));
			const int cartwheel_sway_x= static_cast<int>(
				(static_cast<int64_t>(view->screen_width)*
				 sine_table[cartwheel_phase])/(5*TRIG_MAGNITUDE));
			const int cartwheel_sway_y= static_cast<int>(
				(static_cast<int64_t>(view->screen_height)*
				 (TRIG_MAGNITUDE-cosine_table[cartwheel_phase]))/
				 (14*TRIG_MAGNITUDE));
			rect.x0 -= cartwheel_sway_x;
			rect.x1 -= cartwheel_sway_x;
			rect.y0 += cartwheel_sway_y;
			rect.y1 += cartwheel_sway_y;
		}
		
		// Smoothly lower the weapon while sprinting.
		static float sprint_weapon_lower = 0.0f;
		float sprint_lower_target = 0.0f;

		if (current_player)
		{
			if (current_player->sprinting)
			{
				sprint_lower_target =
					static_cast<float>(view->screen_height) / 8.0f;
			}

			/*
			 * Lower the weapon more deeply during the final slide
			 * phase, then raise it throughout the 16-tick recovery.
			 */
			float slide_recovery_amount = 0.0f;

			if (current_player->flying_kick_landing_ticks > 0)
			{
				slide_recovery_amount = 1.0f;
			}
			else if (current_player->slide_ticks_remaining > 0 &&
				current_player->slide_ticks_remaining <= 8)
			{
				slide_recovery_amount = 1.0f;
			}
			else if (current_player->slide_recovery_ticks > 0)
			{
				slide_recovery_amount =
					current_player->slide_recovery_ticks / 24.0f;
			}

			sprint_lower_target = std::max(
				sprint_lower_target,
				slide_recovery_amount *
					static_cast<float>(view->screen_height) * 0.42f);
		}

		sprint_weapon_lower +=
			(sprint_lower_target - sprint_weapon_lower) * 0.16f;

		const short sprint_lower_offset =
			static_cast<short>(sprint_weapon_lower);

		rect.y0 += sprint_lower_offset;
		rect.y1 += sprint_lower_offset;

		rect.y0 += mantle_lower_offset;
		rect.y1 += mantle_lower_offset;

		// Quick side-to-side weapon swing while sprinting.
		static float sprint_sway_amount = 0.0f;

		const float sprint_sway_target =
			(current_player && current_player->sprinting)
				? 1.0f
				: 0.0f;

		sprint_sway_amount +=
			(sprint_sway_target - sprint_sway_amount) * 0.30f;

		// Follow the physics step phase so the swing peaks stay locked to
		// sprint footsteps instead of drifting with rendering time.
		const float sprint_sway_phase = current_player
			? static_cast<float>(current_player->variables.step_phase) *
				6.283185307f / FIXED_ONE
			: 0.0f;

		const short sprint_sway_offset = static_cast<short>(
			std::sin(sprint_sway_phase) *
			(static_cast<float>(view->screen_width) / 32.0f) *
			sprint_sway_amount);

		rect.x0 += sprint_sway_offset;
		rect.x1 += sprint_sway_offset;
		}

		/*
		 * Scaling and sway are applied after the scenario's weapon origin,
		 * so a sprite intentionally pinned to an edge can otherwise drift far
		 * enough inward to expose the hard boundary of its bitmap. Clamp these
		 * side-mounted sprites last and retain a small off-screen bleed. Doing
		 * this here also accounts for every per-frame sway contribution.
		 */
		if (display_data.side_mounted &&
			display_data.horizontal_positioning_mode == _position_center)
		{
			const _fixed side_margin = FIXED_ONE / 8;
			const int edge_bleed = std::max<int>(2, view->screen_width / 128);
			if (display_data.horizontal_position < FIXED_ONE_HALF - side_margin &&
				rect.x0 > -edge_bleed)
			{
				const int offset = -edge_bleed - rect.x0;
				rect.x0 += offset;
				rect.x1 += offset;
			}
			else if (display_data.horizontal_position > FIXED_ONE_HALF + side_margin &&
				rect.x1 < view->screen_width + edge_bleed)
			{
				const int offset = view->screen_width + edge_bleed - rect.x1;
				rect.x0 += offset;
				rect.x1 += offset;
			}
		}

		/* set rectangle bitmap and shading table */
		extended_get_shape_bitmap_and_shading_table(display_data.collection, display_data.low_level_shape_index, &rect.texture, &rect.shading_tables, view->shading_mode);
		if (!rect.texture) continue;
		
		rect.flags= 0;

		/* initialize clipping window to full screen */
		rect.clip_left= 0;
		rect.clip_right= view->screen_width;
		rect.clip_top= 0;
		rect.clip_bottom= view->screen_height;

		/* copy mirror flags */
		rect.flip_horizontal= display_data.flip_horizontal;
		rect.flip_vertical= display_data.flip_vertical;
		
		/* lighting: depth of zero in the camera’s polygon index */
		rect.depth= 0;
		rect.ambient_shade= get_light_intensity(get_polygon_data(view->origin_polygon_index)->floor_lightsource_index);
		rect.ambient_shade= MAX(shape_information->minimum_light_intensity, rect.ambient_shade);
		if (view->shading_mode==_shading_infravision) rect.flags|= _SHADELESS_BIT;

		// Calculate the object's horizontal position
		// for the convenience of doing teleport-in/teleport-out
		rect.xc = (rect.x0 + rect.x1) >> 1;

                /* make the weapon reflect the owner’s transfer mode */
		instantiate_rectangle_transfer_mode(view, &rect, display_data.transfer_mode, display_data.transfer_phase);

		const float strafe_weapon_degrees= current_player ?
			std::max(-8.0f, std::min(8.0f,
				static_cast<float>(current_player->sprintathon_strafe_roll) *
					720.0f / static_cast<float>(FULL_CIRCLE))) : 0.0f;
		render_viewer_sprite(rect, renderStep,
			display_data.rotation_degrees+strafe_weapon_degrees);
        }

        Shader::disable();
    
        glPopMatrix();

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();

        glMatrixMode(GL_TEXTURE);
        glPopMatrix();
    
        glMatrixMode(GL_MODELVIEW);
}

struct ExtendedVertexData
{
	GLdouble Vertex[4];
	GLdouble TexCoord[2];
	GLfloat Color[3];
	GLfloat GlowColor[3];
};

void RenderRasterize_Shader::render_viewer_sprite(rectangle_definition& RenderRectangle,
	RenderStep renderStep, float rotation_degrees)
{
	// Find texture coordinates
	ExtendedVertexData ExtendedVertexList[4];
	
	point2d TopLeft, BottomRight;
	// Clipped corners:
	TopLeft.x = MAX(RenderRectangle.x0,RenderRectangle.clip_left);
	TopLeft.y = MAX(RenderRectangle.y0,RenderRectangle.clip_top);
	BottomRight.x = MIN(RenderRectangle.x1,RenderRectangle.clip_right);
	BottomRight.y = MIN(RenderRectangle.y1,RenderRectangle.clip_bottom);
	
        // Screen coordinates; weapons-in-hand are in the foreground
        ExtendedVertexList[0].Vertex[0] = TopLeft.x;
        ExtendedVertexList[0].Vertex[1] = TopLeft.y;
        ExtendedVertexList[0].Vertex[2] = 1;
        ExtendedVertexList[2].Vertex[0] = BottomRight.x;
        ExtendedVertexList[2].Vertex[1] = BottomRight.y;
        ExtendedVertexList[2].Vertex[2] = 1;
	
	// Completely clipped away?
	if (BottomRight.x <= TopLeft.x) return;
	if (BottomRight.y <= TopLeft.y) return;
	
	// Use that texture
	auto TMgr = setupSpriteTexture(RenderRectangle, OGL_Txtr_WeaponsInHand, 0, renderStep);
	// Setup can fail while textures are unavailable during a renderer reload.
	// Match the world-sprite path: never read texture options or draw this quad
	// when setupSpriteTexture has marked the texture invalid.
	if (TMgr->ShapeDesc == UNONE) return;
    if (renderStep == kDiffuse && !current_player->infravision_duration &&
        RenderRectangle.transfer_mode == _textured_transfer && !(RenderRectangle.flags & _SHADELESS_BIT)) {
        SprintathonSkyBake::bindActor(Shader::get(Shader::S_Sprite),view->origin_polygon_index,
            SprintathonSkyBake::V(view->origin.x,view->origin.y,view->origin.z));
    }

	
	// Calculate the texture coordinates;
	// the scanline direction is downward, (texture coordinate 0)
	// while the line-to-line direction is rightward (texture coordinate 1)
	GLdouble U_Scale = TMgr->U_Scale/(RenderRectangle.y1 - RenderRectangle.y0);
	GLdouble V_Scale = TMgr->V_Scale/(RenderRectangle.x1 - RenderRectangle.x0);
	GLdouble U_Offset = TMgr->U_Offset;
	GLdouble V_Offset = TMgr->V_Offset;
	
	if (RenderRectangle.flip_vertical)
	{
		ExtendedVertexList[0].TexCoord[0] = U_Offset + U_Scale*(RenderRectangle.y1 - TopLeft.y);
		ExtendedVertexList[2].TexCoord[0] = U_Offset + U_Scale*(RenderRectangle.y1 - BottomRight.y);
	} else {
		ExtendedVertexList[0].TexCoord[0] = U_Offset + U_Scale*(TopLeft.y - RenderRectangle.y0);
		ExtendedVertexList[2].TexCoord[0] = U_Offset + U_Scale*(BottomRight.y - RenderRectangle.y0);
	}
	if (RenderRectangle.flip_horizontal)
	{
		ExtendedVertexList[0].TexCoord[1] = V_Offset + V_Scale*(RenderRectangle.x1 - TopLeft.x);
		ExtendedVertexList[2].TexCoord[1] = V_Offset + V_Scale*(RenderRectangle.x1 - BottomRight.x);
	} else {
		ExtendedVertexList[0].TexCoord[1] = V_Offset + V_Scale*(TopLeft.x - RenderRectangle.x0);
		ExtendedVertexList[2].TexCoord[1] = V_Offset + V_Scale*(BottomRight.x - RenderRectangle.x0);
	}
	
	// Fill in remaining points
	// Be sure that the order gives a sidedness the same as
	// that of the world-geometry polygons
	ExtendedVertexList[1].Vertex[0] = ExtendedVertexList[2].Vertex[0];
	ExtendedVertexList[1].Vertex[1] = ExtendedVertexList[0].Vertex[1];
	ExtendedVertexList[1].Vertex[2] = ExtendedVertexList[0].Vertex[2];
	ExtendedVertexList[1].TexCoord[0] = ExtendedVertexList[0].TexCoord[0];
	ExtendedVertexList[1].TexCoord[1] = ExtendedVertexList[2].TexCoord[1];
	ExtendedVertexList[3].Vertex[0] = ExtendedVertexList[0].Vertex[0];
	ExtendedVertexList[3].Vertex[1] = ExtendedVertexList[2].Vertex[1];
	ExtendedVertexList[3].Vertex[2] = ExtendedVertexList[2].Vertex[2];
	ExtendedVertexList[3].TexCoord[0] = ExtendedVertexList[2].TexCoord[0];
	ExtendedVertexList[3].TexCoord[1] = ExtendedVertexList[0].TexCoord[1];

	if (rotation_degrees != 0.0f)
	{
		constexpr double pi = 3.14159265358979323846;
		const double radians = rotation_degrees * pi / 180.0;
		const double cosine = std::cos(radians);
		const double sine = std::sin(radians);
		const double pivot_x =
			(RenderRectangle.x0 + RenderRectangle.x1) * 0.5;
		const double pivot_y = RenderRectangle.y1;
		for (auto& vertex : ExtendedVertexList)
		{
			const double x = vertex.Vertex[0] - pivot_x;
			const double y = vertex.Vertex[1] - pivot_y;
			vertex.Vertex[0] = pivot_x + x * cosine - y * sine;
			vertex.Vertex[1] = pivot_y + x * sine + y * cosine;
		}

		/*
		 * Rotation raises one of the quad's bottom corners. If the original
		 * sprite reached the bottom of the viewport, lower the rotated quad
		 * just enough to keep both corners off-screen and avoid an empty wedge.
		 */
		if (RenderRectangle.y1 >= RenderRectangle.clip_bottom)
		{
			const double lowest_bottom_edge = std::min(
				ExtendedVertexList[2].Vertex[1],
				ExtendedVertexList[3].Vertex[1]);
			if (lowest_bottom_edge < RenderRectangle.clip_bottom)
			{
				const double vertical_offset =
					RenderRectangle.clip_bottom - lowest_bottom_edge + 1.0;
				for (auto& vertex : ExtendedVertexList)
					vertex.Vertex[1] += vertical_offset;
			}
		}
	}

        if(TMgr->IsBlended() || TMgr->TransferMode == _tinted_transfer) {
		glEnable(GL_BLEND);
		setupBlendFunc(TMgr->NormalBlend());
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);
	} else {
		glDisable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5);
	}

        glDisable(GL_DEPTH_TEST);

	// Location of data:
	glVertexPointer(3,GL_DOUBLE,sizeof(ExtendedVertexData),ExtendedVertexList[0].Vertex);
	glTexCoordPointer(2,GL_DOUBLE,sizeof(ExtendedVertexData),ExtendedVertexList[0].TexCoord);
	glEnable(GL_TEXTURE_2D);
		
	// Go!
        sprintathon_shaft_draw_arrays(GL_POLYGON,0,4);

        if (setupGlow(view, TMgr, 0, 1, weaponFlare, selfLuminosity, 0, renderStep)) {
            sprintathon_shaft_draw_arrays(GL_QUADS, 0, 4);
	}
	
	glEnable(GL_DEPTH_TEST);
        Shader::disable();
	TMgr->RestoreTextureMatrix();

}

#endif
