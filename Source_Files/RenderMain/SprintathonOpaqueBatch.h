#ifndef SPRINTATHON_OPAQUE_BATCH_H
#define SPRINTATHON_OPAQUE_BATCH_H

// Conservative first batching experiment. No sorting: only consecutive draws
// with exactly the same material, uniforms, vertex basis and portal are merged.
// The caller limits this to the opaque diffuse pass. No GL state readbacks.
namespace SprintathonOpaqueBatch {
#if defined(_WIN32) || defined(__WIN32__)
#define SPRINTATHON_BATCH_CALL __stdcall
#elif defined(APIENTRY)
#define SPRINTATHON_BATCH_CALL APIENTRY
#else
#define SPRINTATHON_BATCH_CALL
#endif
using MultiDraw = void (SPRINTATHON_BATCH_CALL *)(GLenum, const GLint*, const GLsizei*, GLsizei);
#undef SPRINTATHON_BATCH_CALL
static MultiDraw multiDraw = nullptr;
static SDL_GLContext procContext = nullptr;
static bool active = false;

struct State {
    Shader* shader = nullptr;
    Shader::UniformState uniforms;
    GLuint texture = 0, bump = 0, bake = 0;
    bool swapped = false;
    GLenum mode = GL_POLYGON;
    std::array<float, 11> attributes{}; // color, normal, tangent
    std::array<int32_t, 4> portal{}; // left/right normals in view space
    bool operator==(const State& b) const {
        return shader == b.shader && texture == b.texture && bump == b.bump &&
            bake == b.bake && swapped == b.swapped && mode == b.mode &&
            attributes == b.attributes && portal == b.portal && uniforms == b.uniforms;
    }
    bool valid() const {
        if (!shader || uniforms.matrix) return false;
        for (float f : attributes) if (!std::isfinite(f)) return false;
        for (const auto& v : uniforms.value)
            for (float f : v) if (!std::isfinite(f)) return false;
        return true;
    }
    template<class Clip> void apply(Clip clip) const {
        shader->enable();
        shader->restoreUniformState(uniforms);
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glBindTexture(GL_TEXTURE_2D, texture);
        if (bump) {
            glActiveTextureARB(GL_TEXTURE1_ARB);
            glBindTexture(GL_TEXTURE_2D, bump);
        }
        if (bake) {
            glActiveTextureARB(GL_TEXTURE0_ARB+4);
            glBindTexture(GL_TEXTURE_2D, bake);
        }
        glActiveTextureARB(GL_TEXTURE0_ARB);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        if (swapped) { glRotatef(90,0,0,1); glScalef(1,-1,1); }
        glMatrixMode(GL_MODELVIEW);
        glColor4fv(attributes.data());
        glNormal3fv(attributes.data()+4);
        glMultiTexCoord4fARB(GL_TEXTURE1_ARB, attributes[7], attributes[8], attributes[9], attributes[10]);
        glDisable(GL_BLEND);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER,0.5f);
        clip(portal);
    }
};

static State pending;
static std::array<GLint, 128> firsts{};
static std::array<GLsizei, 128> counts{};
static size_t count = 0;
static unsigned surfaces = 0, calls = 0, merged = 0;
static unsigned frames = 0;
static unsigned seen = 0, glowSkipped = 0, transferSkipped = 0, fallback = 0;
inline void inspectSurface(bool glow, bool supportedTransfer) {
    if (!active) return;
    ++seen;
    if (glow) ++glowSkipped;
    else if (!supportedTransfer) ++transferSkipped;
}

inline void begin() {
    count=0; surfaces=calls=merged=0;
    seen=glowSkipped=transferSkipped=fallback=0;
    active=Shader::opaqueBatchRequested() && SprintathonOpaqueGeometry::enabled() &&
        SprintathonOpaqueGeometry::buffer &&
        SprintathonOpaqueGeometry::context==SDL_GL_GetCurrentContext();
    if (!active) return;
    if (procContext!=SDL_GL_GetCurrentContext()) {
        procContext=SDL_GL_GetCurrentContext();
        multiDraw=reinterpret_cast<MultiDraw>(SDL_GL_GetProcAddress("glMultiDrawArrays"));
        if (!multiDraw)
            multiDraw=reinterpret_cast<MultiDraw>(SDL_GL_GetProcAddress("glMultiDrawArraysEXT"));
    }
    active=multiDraw!=nullptr;
}

template<class Clip, class Trace> void flush(Clip clip, Trace trace) {
    if (!count) return;
    pending.apply(clip);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB,SprintathonOpaqueGeometry::buffer);
    glVertexPointer(3,GL_FLOAT,5*sizeof(GLfloat),nullptr);
    glTexCoordPointer(2,GL_FLOAT,5*sizeof(GLfloat),reinterpret_cast<const void*>(3*sizeof(GLfloat)));
    trace();
    if (count==1) glDrawArrays(pending.mode,firsts[0],counts[0]);
    else { multiDraw(pending.mode,firsts.data(),counts.data(),static_cast<GLsizei>(count)); merged+=unsigned(count-1); }
    ++calls; count=0;
    glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
    glVertexPointer(3,GL_FLOAT,0,nullptr);
    glTexCoordPointer(2,GL_FLOAT,0,nullptr);
    Shader::disable();
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
    glDisable(GL_CLIP_PLANE0); glDisable(GL_CLIP_PLANE1);
}

template<class Clip, class Trace> void submit(const State& state, size_t offset,
    GLsizei vertices, Clip clip, Trace trace) {
    if (count && (!(state==pending) || count==firsts.size())) {
        flush(clip,trace);
        // Restore the current surface after replaying the older batch. This
        // also keeps Shader's uniform cache synchronized with actual GL state.
        state.apply(clip);
    }
    if (!count) pending=state;
    firsts[count]=static_cast<GLint>(offset/(5*sizeof(GLfloat)));
    counts[count++]=vertices;
    ++surfaces;
}

inline void end() {
    if (active && (++frames==1 || frames%120==0))
        std::fprintf(stderr,"Opaque batch: %u queued surfaces -> %u draw calls; %u saved (other draws unchanged); opaque seen %u, excluded glow %u / transfer %u, cache-or-state fallback %u\n",surfaces,calls,merged,seen,glowSkipped,transferSkipped,fallback);
    active=false;
}
}
#endif
