/*
 *  RenderRasterize_Shader.cpp
 *  Created by Clemens Unterkofler on 1/20/09.
 *  for Aleph One
 *
 *  http://www.gnu.org/licenses/gpl.html
 */

#include "OGL_Headers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "RenderRasterize_Shader.h"

#include "lightsource.h"
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
#include "ChaseCam.h"
#include "preferences.h"
#include "screen.h"
#include "map.h"
#include "SoundManager.h"
#include "SoundPlayer.h"
#include "FileHandler.h"

#ifdef HAVE_OPENGL

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
    struct Sample { GLuint queries[5] = {}; bool pending = false; } samples[12];
    SDL_GLContext context = nullptr;
    bool initialized = false;
    int current = -1;
    bool active = false;
    double sums[5] = {};
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
                if (sample.queries[0]) remove(5, sample.queries);
        for (auto& sample : samples) sample = Sample{};
        initialized = active = false;
        current = -1;
        count = 0;
        for (double& sum : sums) sum = 0;
        sprintathon_gpu_timings = SprintathonGpuTimings{};
    }
    void begin_frame() {
        if (!ShowPosition) {
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
                for (auto& sample : samples) gen(5, sample.queries);
        }
        if (!sprintathon_gpu_timings.supported) return;
        for (auto& sample : samples) {
            if (!sample.pending) continue;
            GLuint ready = 0;
            available(sample.queries[4], query_available, &ready);
            if (!ready) continue;
            for (int i = 0; i < 5; ++i) {
                uint64_t nanoseconds = 0;
                result(sample.queries[i], query_result, &nanoseconds);
                sums[i] += double(nanoseconds) / 1000000.0;
            }
            ++count;
            sample.pending = false;
        }
        const uint64_t now = machine_tick_count();
        if (count && now - last_publish >= MACHINE_TICKS_PER_SECOND / 4) {
            for (int i = 0; i < 5; ++i) {
                sprintathon_gpu_timings.milliseconds[i] = sums[i] / count;
                sums[i] = 0;
            }
            count = 0;
            last_publish = now;
            sprintathon_gpu_timings.ready = true;
        }
        current = -1;
        for (int i = 0; i < 12; ++i)
            if (!samples[i].pending) { current = i; break; }
    }
    void pass(int index) {
        if (current < 0) return;
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


RenderRasterize_Shader::RenderRasterize_Shader() = default;
static void sprintathon_release_scenery_light_texture();
RenderRasterize_Shader::~RenderRasterize_Shader() {
    sprintathon_release_scenery_light_texture();
    sprintathon_gpu_profiler.reset();
}

/*
 * initialize some stuff
 * happens once after opengl, shaders and textures are setup
 */
void RenderRasterize_Shader::setupGL(Rasterizer_Shader_Class& Rasterizer) {
    sprintathon_gpu_profiler.reset();

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

static void setup_invisibility_refraction(
	Shader *shader, GLsizei width, GLsizei height, float visibility)
{
	static GLuint scene_texture = 0;
	static GLsizei texture_width = 0;
	static GLsizei texture_height = 0;

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
	}
	glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		0, 0, width, height);
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

static GLuint sprintathon_capture_scene_depth(GLsizei width, GLsizei height)
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
	glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		0, 0, width, height);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	return depth_texture;
}

static GLuint sprintathon_capture_scene_color(GLsizei width, GLsizei height)
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
	glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
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
static const float sprintathon_shaft_overscan = 1.60f;

static bool sprintathon_begin_shaft_source(GLsizei main_width,
	GLsizei main_height, const GLfloat *projection)
{
	if (!FBO_Allowed)
		return false;

	SprintathonShaftSource& source = sprintathon_shaft_source;
	const GLsizei width = std::max<GLsizei>(1, (main_width * 3 + 3) / 4);
	const GLsizei height = std::max<GLsizei>(1, (main_height * 3 + 3) / 4);

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
	glActiveTextureARB(GL_TEXTURE0_ARB);
	return true;
}

static void sprintathon_end_shaft_source()
{
	SprintathonShaftSource& source = sprintathon_shaft_source;
	if (!source.active)
		return;

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

static void sprintathon_draw_ambient_occlusion(GLuint color_texture,
	GLuint depth_texture, GLsizei width, GLsizei height,
	const GLfloat *projection, float strength, float fog_mode)
{
	static GLuint ao_texture = 0;
	static GLsizei ao_texture_width = 0;
	static GLsizei ao_texture_height = 0;
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
	const GLfloat *projection, float camera_yaw, float camera_pitch,
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

static void sprintathon_draw_anamorphic_lens_flare(GLuint color_texture,
	GLuint depth_texture, GLsizei width, GLsizei height,
	const GLfloat *projection, float strength, float fog_mode)
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

	Shader *shader = Shader::get(Shader::S_AnamorphicLensFlare);
	shader->enable();
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
    if (!sprintathon_has_active_lighting) return;
    for (const projectile_data& projectile : ProjectileList) {
        if (!SLOT_IS_USED(&projectile)) continue;
        float strength = 0.0f;
        float red = 1.0f, green = 1.0f, blue = 1.0f;
        switch (projectile.type) {
            case _projectile_fusion_bolt_minor:
                strength = 0.36f; red = 0.40f; green = 0.65f; blue = 1.0f; break;
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
};
static std::vector<SprintathonTextureEmitter> sprintathon_texture_lights;
static std::vector<SprintathonTextureEmitter> sprintathon_texture_lights_next;

// Optional uncapped list: one object identity per visible scenery fixture.
static std::vector<bool> sprintathon_visible_scenery;
static GLuint sprintathon_scenery_light_texture = 0;
static int sprintathon_scenery_light_texture_height = 0;
static int sprintathon_all_scenery_count = 0;
static std::vector<float> sprintathon_scenery_light_pixels;

static bool sprintathon_all_scenery_enabled()
{
    return graphics_preferences->projectile_lights_per_pixel &&
           graphics_preferences->bright_scenery_lights &&
           graphics_preferences->all_visible_scenery_lights;
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
                                    const SprintathonTextureEmitter& b,
                                    float distance_squared = WORLD_ONE * WORLD_ONE)
{
    if (a.scenery != b.scenery) return false;
    if (a.scenery)
        return a.scenery_object_index != NONE &&
               a.scenery_object_index == b.scenery_object_index &&
               a.scenery_type == b.scenery_type;
    const float dx = a.x-b.x, dy = a.y-b.y, dz = a.z-b.z;
    return a.light_index == b.light_index && dx*dx+dy*dy+dz*dz < distance_squared;
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
    return std::max(0.0f, std::min(1.0f, intensity));
}

// Preserve known static texture sources before adding newly visible ones. Camera
// clipping must not expire a wall light or let draw order replace its position.
static void sprintathon_refresh_texture_lights()
{
    std::vector<SprintathonTextureEmitter> observations;
    observations.swap(sprintathon_texture_lights_next);
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
    for (auto observation : observations) {
        if (!enabled(observation.scenery) ||
            !sprintathon_refresh_scenery_emitter(observation)) continue;
        const float dx = observation.x - current_player->location.x;
        const float dy = observation.y - current_player->location.y;
        const float radius = sprintathon_light_render_radius(observation.scenery);
        if (!(observation.scenery && sprintathon_all_scenery_enabled()) &&
            dx*dx + dy*dy > radius*radius) continue;
        size_t nearest = sprintathon_texture_lights.size();
        float nearest_distance = WORLD_ONE * WORLD_ONE;
        for (size_t i = 0; i < sprintathon_texture_lights.size(); ++i) {
            const auto& source = sprintathon_texture_lights[i];
            if (!sprintathon_same_emitter(source, observation)) continue;
            const float lx = source.x - observation.x;
            const float ly = source.y - observation.y;
            const float lz = source.z - observation.z;
            const float distance = lx*lx + ly*ly + lz*lz;
            if (source.scenery || distance < nearest_distance) {
                nearest = i;
                nearest_distance = distance;
                if (source.scenery) break;
            }
        }
        if (nearest != sprintathon_texture_lights.size()) {
            auto& source = sprintathon_texture_lights[nearest];
            source.r = observation.r;
            source.g = observation.g;
            source.b = observation.b;
            source.last_seen = observation.last_seen;
            source.ambient_delta = observation.ambient_delta;
            if (source.scenery) {
                source.x = observation.x;
                source.y = observation.y;
                source.z = observation.z;
                source.scenery_object_z = observation.scenery_object_z;
            }
        } else {
            sprintathon_keep_emitter(sprintathon_texture_lights, observation);
        }
    }
    // Runs every rendered frame, including when the source surface is hidden.
    // The map simulation supplies pause/bullet-time timing and discrete changes.
    for (auto& source : sprintathon_texture_lights)
        source.shade_gain = sprintathon_texture_light_shade(source);
}

static void sprintathon_record_texture_light(TextureManager *texture,
                                             float x, float y, float z, RenderStep step, bool vertical, bool lava = false,
                                             short light_index = NONE, float ambient_delta = 0.0f,
                                             short scenery_object_index = NONE)
{
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
    if (scenery && (scenery_object_index < 0 ||
        size_t(scenery_object_index) >= ObjectList.size())) return;
    SprintathonTextureEmitter light = {x, y, z,
        rgb[0] * 0.8f, rgb[1] * 0.8f, rgb[2] * 0.8f,
        scenery, machine_tick_count(), light_index, ambient_delta, 1.0f,
        scenery_object_index, static_cast<short>(scenery ? ObjectList[scenery_object_index].permutation : NONE),
        scenery ? float(ObjectList[scenery_object_index].location.z) : 0.0f};
    // Static surfaces may arrive as different clipped fragments. Scenery already
    // has an exact object position and must never snap to a neighboring fixture.
    if (!scenery) {
        for (const auto& previous : sprintathon_texture_lights) {
            if (sprintathon_same_emitter(previous, light, 2.25f * WORLD_ONE * WORLD_ONE)) {
                light.x = previous.x;
                light.y = previous.y;
                light.z = previous.z;
                break;
            }
        }
    }
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
    sprintathon_all_scenery_count = 0;
    if (!sprintathon_all_scenery_enabled() ||
        graphics_preferences->scenery_light_intensity == 0) return;
    std::vector<float> pixels;
    const float gain = sprintathon_emitter_gain(true);
    const float inverse_radius = 1.0f / std::max(sprintathon_emitter_radius(true), 1.0f);
    for (const auto& source : sprintathon_texture_lights) {
        if (!sprintathon_is_forced_scenery(source)) continue;
        pixels.insert(pixels.end(), {source.x, source.y, source.z, inverse_radius,
            source.r * gain, source.g * gain, source.b * gain, 1.0f});
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
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA32F_ARB, 256, height, 0,
                     GL_RGBA, GL_FLOAT, pixels.data());
        sprintathon_scenery_light_texture_height = height;
    } else if (pixels != sprintathon_scenery_light_pixels) {
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, 256, height,
                        GL_RGBA, GL_FLOAT, pixels.data());
    }
    sprintathon_scenery_light_pixels.swap(pixels);
    glActiveTextureARB(active_texture);
}

// Calculate one colored tint per sprite instead of looping over lights per pixel.
static void sprintathon_set_sprite_light(Shader *shader, const rectangle_definition& rect,
                                        short type, const view_data *camera)
{
    if (!sprintathon_has_active_lighting && sprintathon_dropped_flares.empty() &&
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
        float nearest = 1.0f;
        const SprintathonTextureEmitter *source = nullptr;
        for (const auto& light : sprintathon_texture_lights) {
            if (light.shade_gain <= 0.0f || sprintathon_emitter_gain(light.scenery) <= 0.0f) continue;
            const float dx = x - light.x, dy = y - light.y, dz = z - light.z;
            const float radius = sprintathon_emitter_radius(light.scenery);
            const float normalized = (dx*dx + dy*dy + dz*dz) / (radius * radius);
            if (normalized < nearest) {
                nearest = normalized;
                source = &light;
            }
        }
        if (source) {
            const float falloff = 1.0f - nearest;
            const float amount = falloff * falloff / (1.0f + 16.0f * nearest);
            const float gain = sprintathon_emitter_gain(source->scenery) * source->shade_gain;
            rgb[0] += source->r * amount * gain;
            rgb[1] += source->g * amount * gain;
            rgb[2] += source->b * amount * gain;
        }
    }
    shader->setVector4(Shader::U_SprintathonLightColor,
                       rgb[0], rgb[1], rgb[2], 1.0f);
}

// One nearby projectile per surface bounds the shader cost and smooths the glow per pixel.
static Shader *sprintathon_surface_shader(RenderStep step, short texture_type)
{
    if (texture_type != OGL_Txtr_Wall || current_player->infravision_duration)
        return nullptr;
    const bool bump = TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap);
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
            if (source.scenery || selected.scenery) {
                if (sprintathon_same_emitter(source, selected)) return false;
                continue;
            }
            const float dx=source.x-sprintathon_view_emitters[j].x;
            const float dy=source.y-sprintathon_view_emitters[j].y;
            const float dz=source.z-sprintathon_view_emitters[j].z;
            if (dx*dx+dy*dy+dz*dz < 4.0f*WORLD_ONE*WORLD_ONE) return false;
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

static void sprintathon_set_view_emitters(Shader *shader,
                                          const SprintathonSurfaceLightBounds *bounds = nullptr)
{
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
    shader->setFloat(Shader::U_SprintathonAllSceneryCount, float(sprintathon_all_scenery_count));
    static const bool compact = [] {
        const char *setting = std::getenv("SPRINTATHON_COMPACT_SURFACE_LIGHTS");
        return !setting || setting[0] != '0';
    }();
    const float gain = graphics_preferences->colored_light_intensity / 100.0f;
    int active_count = 0;
    bool uploaded[sprintathon_emitter_slots] = {};
    auto upload = [&](int original_slot, float x, float y, float z, float radius,
                      float r, float g, float b) {
        if ((r <= 0.0f && g <= 0.0f && b <= 0.0f) ||
            !sprintathon_light_reaches_surface(bounds, x, y, z, radius)) return;
        const int slot = compact ? active_count : original_slot;
        uploaded[slot] = true;
        shader->setVector4(positions[slot], x, y, z, 1.0f / radius);
        shader->setVector4(colors[slot], r, g, b, 1.0f);
        ++active_count;
    };
    for (int i = 0; i < sprintathon_emitter_slots; ++i) {
        if (!sprintathon_previous_valid[i] ||
            (!sprintathon_dropped_flares.empty() &&
             i >= sprintathon_emitter_slots - sprintathon_flare_capacity)) continue;
        const auto& light = sprintathon_previous_emitters[i];
        if (sprintathon_is_forced_scenery(light)) continue;
        const float radius = std::max(sprintathon_emitter_radius(light.scenery), 1.0f);
        const float fade = sprintathon_emitter_fade[i] * sprintathon_emitter_gain(light.scenery) * light.shade_gain;
        upload(i, light.x, light.y, light.z, radius,
               light.r * fade, light.g * fade, light.b * fade);
    }
    for (size_t i = 0; i < sprintathon_dropped_flares.size(); ++i) {
        const auto& flare = sprintathon_dropped_flares[i];
        const float strength = sprintathon_flare_strength(flare) * gain;
        upload(sprintathon_emitter_slots - sprintathon_flare_capacity + int(i),
               flare.x, flare.y, flare.z, 5.5f * WORLD_ONE,
               strength, strength * 0.12f, strength * 0.04f);
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

static void sprintathon_clear_pixel_light(RenderStep step, short texture_type,
                                          const SprintathonSurfaceLightBounds *bounds = nullptr)
{
    Shader *shader = sprintathon_surface_shader(step, texture_type);
    if (shader) {
        shader->setVector4(Shader::U_SprintathonLightColor, 0, 0, 0, 0);
        sprintathon_set_view_emitters(shader, bounds);
    }
}

static void sprintathon_set_pixel_light(float x, float y, float z, RenderStep step, short texture_type,
                                        const SprintathonSurfaceLightBounds *bounds = nullptr)
{
    Shader *shader = sprintathon_surface_shader(step, texture_type);
    if (!shader) return;
    sprintathon_set_view_emitters(shader, bounds);
    if (!sprintathon_has_active_lighting) {
        shader->setVector4(Shader::U_SprintathonLightColor, 0, 0, 0, 0);
        return;
    }
    float radius = 4.5f * WORLD_ONE;
    float nearest = radius * radius;
    const object_data *selected = nullptr;
    float selected_z = 0;
    float color[3] = {0, 0, 0};
    for (const projectile_data& projectile : ProjectileList) {
        if (!SLOT_IS_USED(&projectile)) continue;
        float r = 0, g = 0, b = 0;
        switch (projectile.type) {
            case _projectile_fusion_bolt_minor: r=.18f; g=.24f; b=.48f; break;
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
        const float dx = x - object->location.x, dy = y - object->location.y, dz = z - object->location.z;
        const float distance_squared = dx*dx + dy*dy + dz*dz;
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
        const float dx = x-object->location.x;
        const float dy = y-object->location.y;
        const float dz = z-flame_z;
        const float distance_squared = dx*dx+dy*dy+dz*dz;
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
        const float dx = x - object->location.x;
        const float dy = y - object->location.y;
        const float dz = z - object->location.z;
        const float distance_squared = dx*dx + dy*dy + dz*dz;
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

void RenderRasterize_Shader::render_tree() {
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
	s->setFloat(Shader::U_Time, view->tick_count);
	s->setFloat(Shader::U_LogicalWidth, view->screen_width);
	s->setFloat(Shader::U_LogicalHeight, view->screen_height);
	s->setFloat(Shader::U_PixelWidth, view->screen_width * MainScreenPixelScale());
	s->setFloat(Shader::U_PixelHeight, view->screen_height * MainScreenPixelScale());
	if (blur.get()) {
		s = Shader::get(Shader::S_InvincibleBloom);
		s->enable();
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
		s->setFloat(Shader::U_FogMix, fogMix);
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
		Shader::get(Shader::S_SpriteShadow)
	};
	
	for (auto s : fog_mode_shaders) {
		s->enable();
		s->setFloat(Shader::U_FogMode, fogmode);
		s->setFloat(Shader::U_MediaFogEnabled, media_fog_enabled);
		s->setFloat(Shader::U_MediaFogTop, media_fog_top);
		s->setFloat(Shader::U_MediaFogSoftness, media_fog_softness);
	}
	
	Shader::disable();

    SprintathonGpuProfileFrame gpu_profile;
    gpu_profile.pass(0);
	render_world_diffuse();
    gpu_profile.pass(1);

	const GLsizei framebuffer_width =
		view->screen_width * MainScreenPixelScale();
	const GLsizei framebuffer_height =
		view->screen_height * MainScreenPixelScale();
	GLuint scene_depth = 0;
	GLuint ambient_occlusion_color = 0;
	const OGL_ConfigureData& ogl_config = Get_OGL_ConfigureData();
	const bool fog_effect_available = fogdata &&
		(fogdata->IsPresent || any_forced_fallback_fog) && fogmode >= 0.0f;
	const bool fog_haze_enabled = fog_effect_available &&
		ogl_config.DeepFogHaze;
	const bool rising_fog_clouds_enabled = fog_haze_enabled &&
		ogl_config.ForceFogAnimatedDensity;
	const bool fog_post_effects_enabled = fog_haze_enabled ||
		rising_fog_clouds_enabled;
	const bool shaft_source_ready = ogl_config.LandscapeLightShafts &&
		sprintathon_begin_shaft_source(framebuffer_width, framebuffer_height,
			scene_projection);
	if (shaft_source_ready)
	{
		render_world_diffuse();
		sprintathon_end_shaft_source();
	}
    sprintathon_draw_flare_smoke(view);
    sprintathon_draw_flare_stars(view);
	if (ogl_config.AmbientOcclusion || ogl_config.LandscapeLightShafts ||
		ogl_config.AnamorphicLensFlares ||
		fog_post_effects_enabled)
		scene_depth = sprintathon_capture_scene_depth(
			framebuffer_width, framebuffer_height);
	if (ogl_config.AmbientOcclusion)
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
	if (ogl_config.AnamorphicLensFlares)
	{
		// Detect emitters before deep-haze and drifting-cloud post effects. Fog
		// added by those passes can therefore cover a flare, but never create one.
		GLuint flare_color = sprintathon_capture_scene_color(
			framebuffer_width, framebuffer_height);
		sprintathon_draw_anamorphic_lens_flare(flare_color, scene_depth,
			framebuffer_width, framebuffer_height, scene_projection,
			ogl_config.AnamorphicLensFlareStrength / 100.0f, fogmode);
	}
	if (fog_post_effects_enabled)
	{
		GLuint fog_haze_color = sprintathon_capture_scene_color(
			framebuffer_width, framebuffer_height);
		sprintathon_draw_fog_haze(fog_haze_color, scene_depth,
			framebuffer_width, framebuffer_height, scene_projection,
			fogmode, 1.0f - media_fog_enabled, fog_haze_enabled,
			rising_fog_clouds_enabled,
			ogl_config.DriftingFogIntensity / 100.0f);
	}
	if (ogl_config.LandscapeLightShafts && shaft_source_ready)
	{
		// Recapture after AO so the shaft pass preserves its shaded world image.
		GLuint shaft_color = sprintathon_capture_scene_color(
			framebuffer_width, framebuffer_height);
		sprintathon_draw_landscape_light_shafts(shaft_color, scene_depth,
			sprintathon_shaft_source.color, sprintathon_shaft_source.depth,
			framebuffer_width, framebuffer_height,
			sprintathon_shaft_source.width, sprintathon_shaft_source.height,
			scene_projection,
			virtual_yaw, virtual_pitch,
			ogl_config.LandscapeLightShaftStrength / 100.0f,
			ogl_config.LandscapeLightShaftLength / 100.0f,
			(ogl_config.LandscapeLightShaftDirection - 90.0f) *
				0.017453292519943295f,
			ogl_config.LandscapeLightShaftElevation * 0.017453292519943295f);
	}
	// Draw the view weapon after world-only AO but before other whole-scene
	// effects, preserving underwater refraction and bloom behavior.
    gpu_profile.pass(2);
	render_viewer_sprite_layer(kDiffuse);
    gpu_profile.pass(3);

	if (current_player->infravision_duration == 0 &&
		TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_Blur) &&
		blur.get())
	{
		prepare_world_frustum();
		blur->begin();
		RenderRasterizerClass::render_tree(kGlow);
                render_viewer_sprite_layer(kGlow);
		blur->end();
		RasPtr->swapper->deactivate();
		blur->draw(*RasPtr->swapper);
		RasPtr->swapper->activate();
	}

    gpu_profile.pass(4);

	// Refract the completed 3D view while submerged. This runs before the HUD
	// is composited, so interface text and meters remain crisp.
	if (Get_OGL_ConfigureData().UnderwaterDistortion &&
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
					view->screen_width * MainScreenPixelScale());
				underwater->setFloat(Shader::U_PixelHeight,
					view->screen_height * MainScreenPixelScale());
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
}

// Only opaque world surfaces change order. Texture-light collection still
// runs in the original traversal, as do sprites, liquids and blended surfaces.
void RenderRasterize_Shader::render_world_diffuse()
{
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
    for (auto node = RSPtr->SortedNodes.rbegin(); node != RSPtr->SortedNodes.rend(); ++node)
        render_node(&*node, see_through_liquids, kDiffuse);
    world_surface_pass = WorldSurfacePass::remaining;
    RenderRasterizerClass::render_tree(kDiffuse);
    world_surface_pass = WorldSurfacePass::all;
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

void RenderRasterize_Shader::clip_to_window(clipping_window_data *win)
{
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
        s->setFloat(Shader::U_TransferFadeOut,((float)((uint16)rect.transfer_data))/(float)((int)FIXED_ONE));
	} else if (current_player->infravision_duration) {
		color[0] = color[1] = color[2] = 1;
		FindInfravisionVersionRGBA(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(rect.ShapeDesc)), color);
		s = Shader::get(Shader::S_SpriteInfravision);
		s->enable();
	} else if (TMgr->TransferMode == _tinted_transfer) {
		flare = -1;
		if (renderStep == kDiffuse) {
			s = Shader::get(Shader::S_Invisible);
		} else {
			s = Shader::get(Shader::S_InvisibleBloom);
		}
		s->enable();
		const float visibility = 1.0f - rect.transfer_data / 32.0f;
		if (renderStep == kDiffuse &&
			Get_OGL_ConfigureData().RefractiveInvisibility)
		{
			setup_invisibility_refraction(s,
				static_cast<GLsizei>(view->screen_width * MainScreenPixelScale()),
				static_cast<GLsizei>(view->screen_height * MainScreenPixelScale()),
				visibility);
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
	}

	if(TMgr->Setup()) {
		TMgr->RenderNormal();
	} else {
		TMgr->ShapeDesc = UNONE;
		return TMgr;
	}

	TMgr->SetupTextureMatrix();
	s->setVector4(Shader::U_ProjectileBlurVector, 0, 0, 0, 0);

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
    if (s == Shader::get(renderStep == kGlow ? Shader::S_SpriteBloom : Shader::S_Sprite))
        sprintathon_set_sprite_light(s, rect, type, view);
	glColor4f(color[0], color[1], color[2], 1);
	return TMgr;
}

// Circle constants
const double Radian2Circle = 1/TWO_PI;			// A circle is 2*pi radians
const double FullCircleReciprocal = 1/double(FULL_CIRCLE);

std::unique_ptr<TextureManager> RenderRasterize_Shader::setupWallTexture(const shape_descriptor& Texture, short transferMode, float pulsate, float wobble, float intensity, float offset, RenderStep renderStep, int16 mediaType) {

	Shader *s = NULL;

	auto TMgr = std::make_unique<TextureManager>();
	LandscapeOptions *opts = NULL;
	TMgr->ShapeDesc = Texture;
	if (TMgr->ShapeDesc == UNONE) { return TMgr; }
	get_shape_bitmap_and_shading_table(Texture, &TMgr->Texture, &TMgr->ShadingTables, _shading_normal);

	TMgr->TransferMode = _textured_transfer;
	TMgr->IsShadeless = current_player->infravision_duration ? 1 : 0;
	TMgr->TransferData = 0;

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
		} else if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
			s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
		}
		s->enable();
	}

	if(TMgr->Setup()) {
		TMgr->RenderNormal(); // must allocate first
		if (TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
			glActiveTextureARB(GL_TEXTURE1_ARB);
			TMgr->RenderBump();
			glActiveTextureARB(GL_TEXTURE0_ARB);
		}
	} else {
		TMgr->ShapeDesc = UNONE;
		return TMgr;
	}

	// Landscapes remain fully rendered. Only opaque, non-media walls can use
	// the cheap sky-occlusion path; preserve transparent/refraction rendering.
	const char *shaft_override = std::getenv("SPRINTATHON_SHAFT_FAST_SOURCE");
	const bool fast_shaft_source = sprintathon_shaft_source.active &&
		(!shaft_override || shaft_override[0] != '0') &&
		TMgr->TextureType == OGL_Txtr_Wall && !TMgr->IsBlended() &&
		mediaType == NONE && renderStep == kDiffuse;
	s->setFloat(Shader::U_SprintathonShaftSource, fast_shaft_source ? 1.0f : 0.0f);
    const char *detail_override = std::getenv("SPRINTATHON_DISTANT_SURFACE_DETAIL");
    const bool simplify_distant = graphics_preferences->simplify_distant_surfaces &&
        (!detail_override || detail_override[0] != '0');
    s->setFloat(Shader::U_SprintathonDistantSurfaceDetail, simplify_distant ? 1.0f : 0.0f);


	TMgr->SetupTextureMatrix();
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
	const float ripple_phase = static_cast<float>(std::fmod(
		ripple_seconds * 1.05 * ripple_speed, TWO_PI));
	s->setFloat(Shader::U_Time, ripple_phase);
	s->setFloat(Shader::U_PixelWidth,
		view->screen_width * MainScreenPixelScale());
	s->setFloat(Shader::U_PixelHeight,
		view->screen_height * MainScreenPixelScale());
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
		config.AnimatedMediaRipples &&
		TEST_FLAG(config.Flags, OGL_Flag_LiqSeeThru))
	{
		static GLuint media_scene_texture = 0;
		static GLsizei media_scene_width = 0;
		static GLsizei media_scene_height = 0;
		const GLsizei width = static_cast<GLsizei>(
			view->screen_width * MainScreenPixelScale());
		const GLsizei height = static_cast<GLsizei>(
			view->screen_height * MainScreenPixelScale());

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
		s->setFloat(Shader::U_Glow, TMgr->MinGlowIntensity());
		return true;
	}
	return false;
}

// Each vec4 stores an inward-facing edge (normal x/y, offset) and the
// neighboring sector's light. A negative fourth component disables the slot.
static void sprintathon_set_sector_light_edges(Shader *shader,
                                               const polygon_data *polygon,
                                               const horizontal_surface_data *surface,
                                               bool ceiling, const view_data *camera)
{
    if (!shader) return;
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
    if (polygon && surface && graphics_preferences->soft_sector_light_edges &&
        !surface->is_media) {
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
        for (short i = 0; i < polygon->vertex_count && count < 8; ++i) {
            const short adjacent_index = polygon->adjacent_polygon_indexes[i];
            if (adjacent_index == NONE) continue;
            const polygon_data *adjacent = get_polygon_data(adjacent_index);
            if (!adjacent) continue;
            const short adjacent_height = ceiling ? adjacent->ceiling_height :
                                                    adjacent->floor_height;
            if (adjacent_height != surface->height) continue;
            const short adjacent_light = ceiling ? adjacent->ceiling_lightsource_index :
                                                   adjacent->floor_lightsource_index;
            const float neighbor = get_light_intensity(adjacent_light) /
                                   float(FIXED_ONE - 1);
            if (std::abs(neighbor - own_light) < 0.025f) continue;
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
            shader->setVector4(names[count], nx, ny, offset, neighbor);
            shader->setVector4(spans[count], a.x, a.y, b.x, b.y);
            ++count;
        }
    }
    for (int i = count; i < 8; ++i)
        shader->setVector4(names[i], 0.0f, 0.0f, 0.0f, -1.0f);
}

void RenderRasterize_Shader::render_node_floor_or_ceiling(clipping_window_data *window,
	polygon_data *polygon, horizontal_surface_data *surface, bool void_present, bool ceil, RenderStep renderStep) {

    // These surfaces stay in the original back-to-front pass. In particular,
    // skip media before setupWallTexture can copy the scene for refraction.
    if (world_surface_pass == WorldSurfacePass::opaque &&
        (void_present || surface->is_media || surface->transfer_mode == _xfer_landscape ||
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
    if (outside_frustum && (world_surface_pass == WorldSurfacePass::opaque ||
                            renderStep == kGlow ||
                            !graphics_preferences->projectile_lights_per_pixel ||
                            !graphics_preferences->bright_texture_lights)) return;

	float offset = 0;

	const shape_descriptor& texture = AnimTxtr_Translate(surface->texture);
	float intensity = get_light_intensity(surface->lightsource_index) / float(FIXED_ONE - 1);
    float projectile_rgb[3] = {0.0f, 0.0f, 0.0f};
    float surface_light_x = 0.0f, surface_light_y = 0.0f;
    float surface_light_z = surface->height;
    if (polygon && polygon->vertex_count > 0) {
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
	auto TMgr = setupWallTexture(texture, surface->transfer_mode, wobble * 4.0,
		0, intensity, offset, renderStep, surface->media_type);
    if (!graphics_preferences->projectile_lights_per_pixel)
        glColor4f(std::min(1.0f, intensity + projectile_rgb[0]),
              std::min(1.0f, intensity + projectile_rgb[1]),
              std::min(1.0f, intensity + projectile_rgb[2]), 1.0f);
	if(TMgr->ShapeDesc == UNONE) { return; }
    const bool opaque_surface = !void_present && !surface->is_media && TMgr->TextureType == OGL_Txtr_Wall && !TMgr->IsBlended();
    if (world_surface_pass == WorldSurfacePass::opaque && skip_world_surface(opaque_surface)) return;
    const bool lava_surface = surface->is_media && surface->media_type == _media_lava;
    if (graphics_preferences->projectile_lights_per_pixel &&
        world_surface_pass != WorldSurfacePass::opaque && (!surface->is_media || lava_surface)) {
        sprintathon_record_texture_light(TMgr.get(), surface_light_x, surface_light_y,
            surface_light_z, renderStep, false, lava_surface, surface->lightsource_index);
        if (lava_surface && polygon && renderStep == kDiffuse) {
            // One emitter at the center of a broad pool cannot reach its banks.
            // The shared emitter limit keeps the added work bounded.
            for (short i = 0; i < polygon->vertex_count; ++i) {
                const world_point2d& bank =
                    get_endpoint_data(polygon->endpoint_indexes[i])->vertex;
                sprintathon_record_texture_light(TMgr.get(), bank.x, bank.y,
                    surface->height, renderStep, false, true);
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

    sprintathon_set_sector_light_edges(
        sprintathon_surface_shader(renderStep, TMgr->TextureType), polygon, surface, ceil, view);
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
		glVertexPointer(3, GL_FLOAT, 0, vertex_array);
		glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);

		glDrawArrays(GL_POLYGON, 0, vertex_count);

		// see note 2 above; pulsate uniform should stay set from setupWall call
		if (setupGlow(view, TMgr, 0, intensity, weaponFlare, selfLuminosity, offset, renderStep)) {
            sprintathon_set_sector_light_edges(
                sprintathon_surface_shader(renderStep, TMgr->TextureType), polygon, surface, ceil, view);
			glDrawArrays(GL_POLYGON, 0, vertex_count);
		}

		Shader::disable();
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
	}
}

void RenderRasterize_Shader::render_node_side(clipping_window_data *window, vertical_surface_data *surface, bool void_present, RenderStep renderStep) {

    // These surfaces stay in the original back-to-front pass. In particular,
    // skip media before setupWallTexture can copy the scene for refraction.
    if (world_surface_pass == WorldSurfacePass::opaque &&
        (void_present || surface->transfer_mode == _xfer_landscape ||
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
    if (outside_frustum && (world_surface_pass == WorldSurfacePass::opaque ||
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
    float projectile_rgb[3];
    if (!graphics_preferences->projectile_lights_per_pixel)
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
	auto TMgr = setupWallTexture(texture, surface->transfer_mode, pulsate, wobble, intensity, offset, renderStep);
    if (!graphics_preferences->projectile_lights_per_pixel)
        glColor4f(std::min(1.0f, intensity + projectile_rgb[0]),
              std::min(1.0f, intensity + projectile_rgb[1]),
              std::min(1.0f, intensity + projectile_rgb[2]), 1.0f);
	if(TMgr->ShapeDesc == UNONE) { return; }
    const bool opaque_surface = !void_present && TMgr->TextureType == OGL_Txtr_Wall && !TMgr->IsBlended();
    if (world_surface_pass == WorldSurfacePass::opaque && skip_world_surface(opaque_surface)) return;
    if (graphics_preferences->projectile_lights_per_pixel &&
        world_surface_pass != WorldSurfacePass::opaque)
        sprintathon_record_texture_light(TMgr.get(), surface_light_x, surface_light_y, surface_light_z, renderStep, true, false,
            surface->lightsource_index, float(surface->ambient_delta));
    // Retain emitter collection even for off-screen surfaces, then avoid
    // sector/pixel-light uniforms, clipping setup, geometry and glow draws.
    if (outside_frustum) {
        reset_skipped_world_surface();
        return;
    }
    if (world_surface_pass == WorldSurfacePass::remaining && skip_world_surface(opaque_surface)) return;

    sprintathon_set_sector_light_edges(
        sprintathon_surface_shader(renderStep, TMgr->TextureType), nullptr, nullptr, false, view);
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

			for(int i = 0; i < vertex_count; ++i) {
				float p2 = 0;
				if(i == 1 || i == 2) { p2 = surface->length; }

				*vp++ = vertices[i].x;
				*vp++ = vertices[i].y;
				*vp++ = vertices[i].z;
				*tp++ = (tOffset - vertices[i].z) / static_cast<float>(div);
				*tp++ = (x0+p2) / static_cast<float>(div);
			}
			glVertexPointer(3, GL_FLOAT, 0, vertex_array);
			glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);
			
			glDrawArrays(GL_QUADS, 0, vertex_count);

			if (setupGlow(view, TMgr, wobble, intensity, weaponFlare, selfLuminosity, offset, renderStep)) {
            sprintathon_set_sector_light_edges(
                sprintathon_surface_shader(renderStep, TMgr->TextureType), nullptr, nullptr, false, view);
				glDrawArrays(GL_QUADS, 0, vertex_count);
			}

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
		const float visibility =
			1.0f - RenderRectangle.transfer_data / 32.0f;
		if (renderStep == kDiffuse &&
			Get_OGL_ConfigureData().RefractiveInvisibility)
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
		if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
			s = Shader::get(renderStep == kGlow ? Shader::S_BumpBloom : Shader::S_Bump);
		} else {
			s = Shader::get(renderStep == kGlow ? Shader::S_WallBloom : Shader::S_Wall);
		}
		s->enable();
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

	if(TEST_FLAG(Get_OGL_ConfigureData().Flags, OGL_Flag_BumpMap)) {
		glActiveTextureARB(GL_TEXTURE1_ARB);
		if(ModelPtr->Use(CLUT,OGL_SkinManager::Bump)) {
			LoadModelSkin(SkinPtr->OffsetImg, Collection, CLUT);
		}
		if (!SkinPtr->OffsetImg.IsPresent()) {
			FlatBumpTexture();
		}
		glActiveTextureARB(GL_TEXTURE0_ARB);
	}

	glDrawElements(GL_TRIANGLES,(GLsizei)ModelPtr->Model.NumVI(),GL_UNSIGNED_SHORT,ModelPtr->Model.VIBase());

	if (canGlow && SkinPtr->GlowImg.IsPresent()) {
		glEnable(GL_BLEND);
		setupBlendFunc(SkinPtr->GlowBlend);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.001);

		s->enable();
		s->setFloat(Shader::U_Glow, SkinPtr->MinGlowIntensity);
		if (renderStep == kGlow) {
			s->setFloat(Shader::U_BloomScale, SkinPtr->GlowBloomScale);
			s->setFloat(Shader::U_BloomShift, SkinPtr->GlowBloomShift);
		}

		if(ModelPtr->Use(CLUT,OGL_SkinManager::Glowing)) {
			LoadModelSkin(SkinPtr->GlowImg, Collection, CLUT);
		}
		glDrawElements(GL_TRIANGLES,(GLsizei)ModelPtr->Model.NumVI(),GL_UNSIGNED_SHORT,ModelPtr->Model.VIBase());
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

void RenderRasterize_Shader::_render_node_object_helper(render_object_data *object, RenderStep renderStep) {

	rectangle_definition& rect = object->rectangle;
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

		RenderModel(rect, collection, clut, weaponFlare, selfLuminosity,
			renderStep,
			static_cast<GLsizei>(view->screen_width * MainScreenPixelScale()),
			static_cast<GLsizei>(view->screen_height * MainScreenPixelScale()));
		glPopMatrix();
		return;
	}

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


	if (object->is_scenery && renderStep == kDiffuse) {
        float bright_u, bright_v, bright_rgb[3];
        if (graphics_preferences->bright_scenery_lights &&
            TMgr->GetBrightEmission(bright_u, bright_v, bright_rgb)) {
            // Scale the height from the actual scenery frame. Its horizontal
            // hotspot stays at the object location for camera-facing sprites.
            const float bottom = rect.WorldBottom * rect.Scale;
            const float top = rect.WorldTop * rect.Scale;
            const float light_z = pos.z + bottom + (top - bottom) * (1.0f - bright_v);
            sprintathon_record_texture_light(TMgr.get(), pos.x, pos.y, light_z, renderStep, true,
                false, NONE, 0.0f, object->scenery_object_index);
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

    if (renderStep == kDiffuse && !sprintathon_shaft_source.active &&
        graphics_preferences->projectile_motion_blur &&
        rect.transfer_mode == _textured_transfer)
    {
        float motion[3];
        bool moving = sprintathon_projectile_blur_motion(object->projectile_index, motion);
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
                glGetFloatv(GL_TEXTURE_MATRIX, matrix);
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
                glGetFloatv(GL_CURRENT_COLOR, color);
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
                glDrawArrays(GL_QUADS, 0, 4);
                smear->setVector4(Shader::U_ProjectileBlurVector, 0, 0, 0, 0);
                glPopAttrib();
                glVertexPointer(3, GL_FLOAT, 0, vertex_array);
                glTexCoordPointer(2, GL_FLOAT, 0, texcoord_array);
            }
        }
    }

	glDrawArrays(GL_QUADS, 0, 4);

	if (setupGlow(view, TMgr, 0, 1, weaponFlare, selfLuminosity, offset, renderStep)) {
		glDrawArrays(GL_QUADS, 0, 4);
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
				glDrawArrays(GL_QUADS, 0, 4);
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
        glDrawArrays(GL_POLYGON,0,4);

        if (setupGlow(view, TMgr, 0, 1, weaponFlare, selfLuminosity, 0, renderStep)) {
            glDrawArrays(GL_QUADS, 0, 4);
	}
	
	glEnable(GL_DEPTH_TEST);
        Shader::disable();
	TMgr->RestoreTextureMatrix();

}

#endif
