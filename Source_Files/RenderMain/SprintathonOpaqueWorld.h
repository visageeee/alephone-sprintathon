#ifndef SPRINTATHON_OPAQUE_WORLD_H
#define SPRINTATHON_OPAQUE_WORLD_H
#include <array>
#include <algorithm>
#include <tuple>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <SDL_image.h>

// Optional geometry-throughput prototype with static baked lighting.
// Includes bump/parallax, sector-edge blending and connected floor light pools.
namespace SprintathonOpaqueWorld {
inline bool requested(){
    const char* v=std::getenv("SPRINTATHON_OPAQUE_WORLD");
    return v?v[0]=='1':graphics_preferences->batched_world_rendering;
}
#if defined(_WIN32) || defined(__WIN32__)
#define SPRINTATHON_WORLD_CALL __stdcall
#elif defined(APIENTRY)
#define SPRINTATHON_WORLD_CALL APIENTRY
#else
#define SPRINTATHON_WORLD_CALL
#endif
using ArrayImage3D=void(SPRINTATHON_WORLD_CALL *)(GLenum,GLint,GLint,GLsizei,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
using ArraySubImage3D=void(SPRINTATHON_WORLD_CALL *)(GLenum,GLint,GLint,GLint,GLint,GLsizei,GLsizei,GLsizei,GLenum,GLenum,const void*);
using MultiDraw=void(SPRINTATHON_WORLD_CALL *)(GLenum,const GLint*,const GLsizei*,GLsizei);
#undef SPRINTATHON_WORLD_CALL
static MultiDraw multiDraw=nullptr;
static ArrayImage3D arrayImage3D=nullptr;
static ArraySubImage3D arraySubImage3D=nullptr;
static constexpr GLenum arrayTarget=0x8C1A,arrayBinding=0x8C1D,arrayMaxLayers=0x88FF;
static bool arrayMode=false;
static GLint arrayLocation=-1;
struct ArrayBinding {GLuint source=0,sourceBump=0,texture=0,bump=0;int layer=0;};
static std::map<shape_descriptor,ArrayBinding> arrayMaterials;
static std::vector<GLuint> arrayTextures;
inline void preloadArrays();
static SDL_GLContext context=nullptr;
static GLuint buffer=0;
static std::array<GLuint,3> parameters{},dynamicTextures{};
static GLuint sceneryTexture=0;
static std::map<std::array<float,12>,int> dynamicIds;
static std::vector<float> dynamicData;
static constexpr int dynamicLimit=8192;
inline int storeLight(const std::array<float,12>& record){
    if(record[7]==0)return 0;
    for(float v:record)if(!std::isfinite(v))return -1;
    auto found=dynamicIds.find(record);if(found!=dynamicIds.end())return found->second;
    const int id=int(dynamicData.size()/12);if(id>=dynamicLimit)return -1;
    dynamicData.insert(dynamicData.end(),record.begin(),record.end());dynamicIds.emplace(record,id);return id;
}
inline bool prepareDynamic(int,const float*,bool,float*,const view_data*);
static unsigned parameterCount=0,parameterSlot=0;
// Set to 0 for an otherwise identical single-texture comparison.
inline bool rotateParameters(){static const bool on=[](){
    const char* v=std::getenv("SPRINTATHON_OPAQUE_PARAMETER_RING");return !v || v[0]!='0';
}();return on;}
static GLhandleARB program=0;
static bool active=false,earlyClip=false;
inline bool earlyClipRequested(){static const bool on=[](){
    const char* v=std::getenv("SPRINTATHON_OPAQUE_EARLY_CLIP");return !v || v[0]!='0';
}();return on;}
static constexpr size_t bufferBytes=8*1024*1024;
static size_t usedBytes=0;
static unsigned rowLimit=0,uploadRows=0;
static unsigned seen=0,queued=0,drawCalls=0,uploads=0,fallback=0;
static unsigned skippedPlatforms=0,skippedMaterials=0;
static unsigned arrayQueued=0,arrayDrawCalls=0;
static std::chrono::steady_clock::time_point beginTime;
static float fogSettings[4]={-1,0,0,1};
static float playerLight=0;
static long_vector2d outerLeft{},outerRight{};
static GLint fogLocation=-1,lightLocation=-1;

struct GeometryKey {
    int count=0;
    std::array<float,80> points{};
    bool operator<(const GeometryKey& b)const{return count!=b.count?count<b.count:points<b.points;}
};
struct InstanceKey {
    GeometryKey geometry;
    unsigned instance=0;
    bool operator<(const InstanceKey& b)const {
        if(geometry<b.geometry)return true;
        if(b.geometry<geometry)return false;
        return instance<b.instance;
    }
};
struct Entry{GLint first=0;GLsizei count=0;unsigned row=0;};
struct Group{std::vector<GLint> first;std::vector<GLsizei> count;};
struct Material{std::unique_ptr<TextureManager> manager;GLuint texture=0,bump=0;bool swapped=false;GLuint arrayTexture=0,arrayBump=0;int layer=0;};
static std::map<InstanceKey,Entry> geometry;
static std::map<GeometryKey,unsigned> instances;
struct GroupKey {
    GLuint texture=0,bump=0;int page=-1;bool arrays=false;
    bool operator<(const GroupKey& b)const{return arrays!=b.arrays?arrays<b.arrays:(texture!=b.texture?texture<b.texture:(bump!=b.bump?bump<b.bump:page<b.page));}
};
static std::map<GroupKey,Group> groups;
static std::map<shape_descriptor,Material> materials;
static std::vector<float> table;
static constexpr int surfaceColumns=44,surfaceFloats=surfaceColumns*4;
struct SurfaceRecord {
    std::array<float,112> values{};
    SurfaceRecord(){for(int i=0;i<8;++i)values[8+i*4+3]=-1;values[106]=1;values[109]=1;values[111]=-1;}
};
// Opt-in records survive frames. Dynamic light selection and cone pools do not.
// Experimental distance LOD: use the closest point of the surface bounds,
// so a large floor stretching towards the camera stays fully detailed.
inline bool distantWorldEnabled(){static const bool on=[](){const char* p=std::getenv("SPRINTATHON_DISTANT_WORLD");return p && p[0]=='1';}();return on;}
static unsigned distantFull=0,distantTransition=0;
inline float surfaceDetail(int count,const float* vertices,const view_data* view){
    if(!distantWorldEnabled())return 1.0f;
    float low[3]={vertices[0],vertices[1],vertices[2]},high[3]={low[0],low[1],low[2]};
    for(int i=1;i<count;++i)for(int j=0;j<3;++j){low[j]=std::min(low[j],vertices[i*3+j]);high[j]=std::max(high[j],vertices[i*3+j]);}
    const float camera[3]={float(view->origin.x),float(view->origin.y),float(view->origin.z)};
    float squared=0;
    for(int j=0;j<3;++j){const float d=std::max(low[j]-camera[j],std::max(0.0f,camera[j]-high[j]));squared+=d*d;}
    const float t=std::max(0.0f,std::min(1.0f,(std::sqrt(squared)-16384.0f)/8192.0f));
    return 1.0f-t*t*(3.0f-2.0f*t);
}
inline bool retainedEnabled(){static const bool on=[](){const char* p=std::getenv("SPRINTATHON_RETAINED_SURFACES");return p && p[0]=='1';}();return on;}
struct BakeEntry;
using RetainedKey=std::array<int64_t,20>;
struct RetainedEdge {
    std::array<float,3> plane;std::array<float,4> span;
    short light;const side_data* side;
};
struct RetainedEdges {
    std::vector<RetainedEdge> candidates;
    std::vector<std::pair<const polygon_data*,unsigned>> dependencies;
    bool ready=false,blend=false;int softness=0;
};
static std::map<const polygon_data*,unsigned> retainedPolygonVersions;
static unsigned retainedEdgeBuilds=0,retainedEdgeHits=0;
inline void edgeDependency(RetainedEdges* edges,const polygon_data* p){
    if(edges && p)edges->dependencies.emplace_back(p,retainedPolygonVersions[p]);
}
struct RetainedSurface {
    int count=0,yExtent=0;std::array<float,48> vertices{};std::array<float,32> uv{};
    GeometryKey geometry;SurfaceRecord detail;RetainedEdges edges;const BakeEntry* bake=nullptr;
};
static std::map<RetainedKey,RetainedSurface> retainedSurfaces;
static std::vector<int64_t> retainedState,retainedNextState;
static std::vector<std::vector<int64_t>> retainedPolygonState;
static unsigned retainedLocalInvalidations=0;
static unsigned retainedHits=0,retainedMisses=0,retainedInvalidations=0;
static double retainedValidationMs=0;
inline void beginRetainedFrame();
inline void prepareRetainedDetail(RetainedSurface&,SurfaceRecord&,const polygon_data*,
    const horizontal_surface_data*,bool,const vertical_surface_data*,const view_data*);
inline RetainedSurface* findRetained(const RetainedKey& key){
    auto found=retainedSurfaces.find(key);
    if(found==retainedSurfaces.end()){++retainedMisses;return nullptr;}
    ++retainedHits;return &found->second;
}
inline void rememberSurface(const RetainedKey& key,int count,const float* vertices,const float* uv,
    const SurfaceRecord& detail,const BakeEntry* bake){
    if(retainedSurfaces.size()>=8192)return;
    RetainedSurface record;record.count=count;record.detail=detail;record.bake=bake;
    // Pool positions, colors and connectivity are refreshed on every use.
    std::fill(record.detail.values.begin()+72,record.detail.values.begin()+104,0.0f);
    record.geometry.count=count;
    for(int i=0;i<count;++i){
        for(int j=0;j<3;++j)record.geometry.points[i*5+j]=record.vertices[i*3+j]=vertices[i*3+j];
        for(int j=0;j<2;++j)record.geometry.points[i*5+3+j]=record.uv[i*2+j]=uv[i*2+j];
    }
    float y0=vertices[1],y1=y0;
    for(int i=1;i<count;++i){y0=std::min(y0,vertices[i*3+1]);y1=std::max(y1,vertices[i*3+1]);}
    record.yExtent=int(y1-y0);
    retainedSurfaces.emplace(key,std::move(record));
}
// Reset per rendered view: light IDs, interpolation and camera state are view-local.
inline bool reusePreparation(){static const bool on=[](){
    const char* value=std::getenv("SPRINTATHON_REUSE_SURFACE_PREP");return value && value[0]=='1';
}();return on;}
static std::map<std::vector<int64_t>,SurfaceRecord> surfacePreparation;
static std::map<std::array<float,7>,std::array<float,32>> dynamicPreparation;
static unsigned surfacePrepHits=0,surfacePrepMisses=0,dynamicPrepHits=0,dynamicPrepMisses=0;
inline void prepareSurface(SurfaceRecord&,const polygon_data*,const horizontal_surface_data*,bool,
    const vertical_surface_data*,const view_data*);
static GLint bumpLocation=-1,detailLocation=-1;

// Atlas construction is loading-only. Missing/invalid entries fall back to the
// normal surface path; never read textures back or repack during gameplay.
static constexpr int atlasSize=1024,atlasLimit=8;
struct AtlasPage {GLuint sky=0,light=0;};
struct BakeEntry {
    int page=-1;GLuint source=0;std::array<float,4> u{},v{},rect{};
    int layers=1;bool path=false,textureLight=false;
};
using BakeKey=std::tuple<int,int,int,int>;
static std::vector<AtlasPage> atlasPages;
static std::map<BakeKey,BakeEntry> atlasEntries;
static unsigned long long atlasGeneration=0;
static GLint sunLocation=-1,ambientLocation=-1,sectorLocation=-1;
inline void clearAtlas(){
    if(context==SDL_GL_GetCurrentContext())for(auto& p:atlasPages){
        glDeleteTextures(1,&p.sky);glDeleteTextures(1,&p.light);
    }
    atlasPages.clear();atlasEntries.clear();
}
// Debug-only PNG output from the CPU upload pixels, with no GPU readback.
inline bool exportAtlasPNG(const std::string& path,const std::vector<unsigned char>& pixels){
    if(pixels.size()!=size_t(atlasSize)*atlasSize*3)return false;
    SDL_Surface* image=SDL_CreateRGBSurfaceWithFormat(0,atlasSize,atlasSize,24,SDL_PIXELFORMAT_RGB24);
    if(!image){std::fprintf(stderr,"Atlas export: %s\n",SDL_GetError());return false;}
    // PNG rows run downwards; OpenGL atlas v coordinates run upwards.
    for(int row=0;row<atlasSize;++row)
        std::copy_n(pixels.data()+size_t(atlasSize-1-row)*atlasSize*3,atlasSize*3,
                    static_cast<unsigned char*>(image->pixels)+size_t(row)*image->pitch);
    const bool ok=IMG_SavePNG(image,path.c_str())==0;
    if(!ok)std::fprintf(stderr,"Atlas export failed: %s: %s\n",path.c_str(),IMG_GetError());
    else std::fprintf(stderr,"Atlas exported: %s\n",path.c_str());
    SDL_FreeSurface(image);return ok;
}
// Plan placement before reading pixels. Tall rectangles establish shelves;
// smaller ones fill leftover shelf width, including on earlier pages.
// Keep orientation and the one-texel duplicated border unchanged.
struct AtlasPlacement {
    BakeKey key;int w=0,h=0,page=-1,x=0,y=0;
};
inline void packAtlas(std::vector<AtlasPlacement>& entries){
    std::stable_sort(entries.begin(),entries.end(),[](const AtlasPlacement& a,const AtlasPlacement& b){
        return a.h!=b.h ? a.h>b.h : a.w>b.w;
    });
    struct Shelf {int y,height,used;};
    struct Page {int usedHeight=0;std::vector<Shelf> shelves;};
    std::vector<Page> pages;
    for(auto& entry:entries){
        entry.page=-1;
        if(entry.w<1 || entry.h<1 || entry.w>atlasSize-2 || entry.h>atlasSize-2)continue;
        const int w=entry.w+2,h=entry.h+2;
        for(size_t page=0;page<pages.size() && entry.page<0;++page){
            auto& target=pages[page];
            for(auto& shelf:target.shelves){
                if(h<=shelf.height && shelf.used+w<=atlasSize){
                    entry.page=int(page);entry.x=shelf.used;entry.y=shelf.y;
                    shelf.used+=w;break;
                }
            }
            if(entry.page<0 && target.usedHeight+h<=atlasSize){
                entry.page=int(page);entry.x=0;entry.y=target.usedHeight;
                target.shelves.push_back({target.usedHeight,h,w});target.usedHeight+=h;
            }
        }
        if(entry.page<0 && pages.size()<atlasLimit){
            entry.page=int(pages.size());entry.x=entry.y=0;
            Page page;page.usedHeight=h;page.shelves.push_back({0,h,w});pages.push_back(page);
        }
        // If full, continue: a later smaller rectangle may still fit a gap.
    }
    // Upload one page at a time, keeping only one pair of CPU pixel buffers.
    std::stable_sort(entries.begin(),entries.end(),[](const AtlasPlacement& a,const AtlasPlacement& b){return a.page<b.page;});
}
inline void preloadAtlas(){
    using namespace SprintathonSkyBake;
    atlasGeneration=mapGeneration;
    if(!enabled() || !available)return;
    GLint maxSize=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);if(maxSize<atlasSize)return;
    glPushAttrib(GL_TEXTURE_BIT);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    GLint pack=0,unpack=0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB,&pack);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&unpack);
    glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,0);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    // Directory must already exist. Each load gets its own filename prefix.
    std::string exportPrefix;
    const char* exportDirectory=std::getenv("SPRINTATHON_ATLAS_EXPORT");
    if(exportDirectory && exportDirectory[0]) {
        exportPrefix=exportDirectory;
        if(exportPrefix.back()!='/' && exportPrefix.back()!='\\')exportPrefix+='/';
        exportPrefix+="atlas-"+std::to_string(SDL_GetPerformanceCounter())+"-";
    }
    std::vector<unsigned char> skyPixels,lightPixels;
    int x=0,y=0;
    auto upload=[&](){
        if(atlasPages.empty() || skyPixels.empty())return;
        auto& page=atlasPages.back();
        auto send=[&](GLuint& texture,const std::vector<unsigned char>& pixels){
            glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,atlasSize,atlasSize,0,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            GLint width=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
            if(width!=atlasSize){glDeleteTextures(1,&texture);texture=0;}
        };
        send(page.sky,skyPixels);send(page.light,lightPixels);
        if(!exportPrefix.empty()) {
            const std::string prefix=exportPrefix+"page-"+std::to_string(atlasPages.size()-1);
            exportAtlasPNG(prefix+"-landscape.png",skyPixels);
            exportAtlasPNG(prefix+"-texture-scenery.png",lightPixels);
        }
    };
    auto copy=[&](std::vector<unsigned char>& dst,const std::vector<unsigned char>& src,int w,int h){
        for(int j=-1;j<=h;++j)for(int i=-1;i<=w;++i)for(int c=0;c<3;++c)
            dst[((y+1+j)*atlasSize+x+1+i)*3+c]=src[(std::max(0,std::min(h-1,j))*w+std::max(0,std::min(w-1,i)))*3+c];
    };
    std::vector<AtlasPlacement> placements;
    placements.reserve(maps.size());
    for(const auto& item:maps){
        const auto& m=item.second;const int face=std::get<1>(item.first);
        if(face>=16 || !m.texture || m.layers<1)continue;
        glBindTexture(GL_TEXTURE_2D,m.texture);GLint w=0,h=0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
        if(w<1 || h<1 || w>atlasSize-2 || h>atlasSize-2 || h%m.layers)continue;
        AtlasPlacement entry;entry.key=item.first;entry.w=w;entry.h=h;placements.push_back(entry);
    }
    packAtlas(placements);
    for(const auto& placement:placements){
        if(placement.page<0)continue; // Normal renderer handles atlas overflow.
        const auto& item=*maps.find(placement.key);
        const auto& m=item.second;const int w=placement.w,h=placement.h;
        if(placement.page!=int(atlasPages.size())-1){
            upload();
            atlasPages.push_back(AtlasPage{});
            skyPixels.assign(atlasSize*atlasSize*3,0);lightPixels.assign(atlasSize*atlasSize*3,0);
        }
        x=placement.x;y=placement.y;
        std::vector<unsigned char> pixels(size_t(w)*h*3);
        glBindTexture(GL_TEXTURE_2D,m.texture);glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        copy(skyPixels,pixels,w,h);
        bool textureLight=false;
        if(m.textureLight){
            glBindTexture(GL_TEXTURE_2D,m.textureLight);GLint tw=0,th=0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&tw);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&th);
            if(tw==w && th==h/m.layers){
                pixels.resize(size_t(tw)*th*3);glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
                copy(lightPixels,pixels,tw,th);textureLight=true;
            }
        }
        BakeEntry e;e.page=int(atlasPages.size())-1;e.source=m.texture;e.layers=m.layers;e.path=m.sunPath;e.textureLight=textureLight;
        e.u={{float(m.u.x*m.us),float(m.u.y*m.us),float(m.u.z*m.us),float(m.u0)}};
        e.v={{float(m.v.x*m.vs),float(m.v.y*m.vs),float(m.v.z*m.vs),float(m.v0)}};
        e.rect={{float(x+1)/atlasSize,float(y+1)/atlasSize,float(w)/atlasSize,float(h)/atlasSize}};
        atlasEntries.emplace(item.first,e);
    }
    upload();
    if(!exportPrefix.empty()) {
        const std::string path=exportPrefix+"regions.csv";
        FILE* file=std::fopen(path.c_str(),"w");
        if(!file)std::fprintf(stderr,"Atlas export: cannot write %s\n",path.c_str());
        else {
            std::fprintf(file,"polygon,face,z,height,page,png_x,png_y,width,height_pixels,sun_layers,texture_scenery,png_light_y,light_height\n");
            for(const auto& item:atlasEntries) {
                const auto& e=item.second;
                const int x=int(std::lround(e.rect[0]*atlasSize)),y=int(std::lround(e.rect[1]*atlasSize));
                const int w=int(std::lround(e.rect[2]*atlasSize)),h=int(std::lround(e.rect[3]*atlasSize));
                std::fprintf(file,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                    std::get<0>(item.first),std::get<1>(item.first),std::get<2>(item.first),std::get<3>(item.first),
                    e.page,x,atlasSize-y-h,w,h,e.layers,int(e.textureLight),atlasSize-y-h/e.layers,h/e.layers);
            }
            bool ok=std::ferror(file)==0;
            if(std::fclose(file)!=0)ok=false;
            std::fprintf(stderr,"Atlas regions %s: %s\n",ok?"exported":"write failed",path.c_str());
        }
    }
    glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,pack);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,unpack);
    glPopClientAttrib();glPopAttrib();
    std::fprintf(stderr,"Opaque baked atlas: %u maps, %u pages, %.1f MiB; built during loading\n",
        unsigned(atlasEntries.size()),unsigned(atlasPages.size()),atlasPages.size()*atlasSize*atlasSize*6.0/(1024*1024));
}
inline const BakeEntry* baked(const polygon_data* polygon,int face,int z,int height){
    using namespace SprintathonSkyBake;
    if(!enabled() || !available || atlasGeneration!=mapGeneration || !polygon)return nullptr;
    const int room=int(polygon-get_polygon_data(0));
    if(room<0 || room>=int(rooms.size()))return nullptr;
    if(face<0 || face>=16)return nullptr;
    if(face>=2 && face-2>=int(rooms[room].edges.size()))return nullptr;
    if(face>=2){z=int(std::lround(rooms[room].floor));height=int(std::lround(rooms[room].ceiling-rooms[room].floor));}
    auto it=atlasEntries.find(std::make_tuple(room,face,z,height));
    if(it==atlasEntries.end())return nullptr;
    const auto& e=it->second;auto source=maps.find(it->first);
    if(source==maps.end() || source->second.texture!=e.source || e.page<0 ||
       !atlasPages[e.page].sky || !atlasPages[e.page].light)return nullptr;
    // A missing baked texture-light layer must not silently lose static light.
    if(textureBakeActive() && source->second.textureLight && !e.textureLight)return nullptr;
    return &e;
}

static const char* vertexSource=R"GLSL(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect surfaceData;
varying vec2 uv;
varying vec3 eyePosition;
#ifndef SPRINTATHON_EARLY_CLIP
varying vec2 portalDistance;
#endif
varying float sectorShade;
varying float classicDepth;
varying float worldHeight;
varying vec3 dynamicWorld;
varying vec3 tangentView;
varying float surfaceRow;
varying float surfaceDetailWeight;
varying vec2 bakeUV;
varying vec4 bakeRect;
varying vec4 bakeLayers;
varying vec2 bakeFlags;
#ifdef SPRINTATHON_TEXTURE_ARRAYS
varying float materialLayer;
#endif
void main(){
    float row=gl_MultiTexCoord0.z+0.5;
    surfaceRow=row;dynamicWorld=gl_Vertex.xyz;
    vec4 surface=texture2DRect(surfaceData,vec2(0.5,row));
    vec4 left=texture2DRect(surfaceData,vec2(1.5,row));
    vec4 right=texture2DRect(surfaceData,vec2(2.5,row));
#ifdef SPRINTATHON_EARLY_CLIP
    gl_ClipDistance[0]=dot(gl_Vertex,left);
    gl_ClipDistance[1]=dot(gl_Vertex,right);
#else
    portalDistance=vec2(dot(gl_Vertex,left),dot(gl_Vertex,right));
#endif
    gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;
    gl_Position.z+=surface.y*gl_Position.z/65536.0;
    classicDepth=gl_Position.z/8192.0;
    uv=surface.z>0.5?gl_MultiTexCoord0.yx:gl_MultiTexCoord0.xy;
    sectorShade=surface.x;
    bakeUV=vec2(dot(gl_Vertex,texture2DRect(surfaceData,vec2(3.5,row))),
                dot(gl_Vertex,texture2DRect(surfaceData,vec2(4.5,row))));
    bakeRect=texture2DRect(surfaceData,vec2(5.5,row));
    bakeFlags=texture2DRect(surfaceData,vec2(6.5,row)).xy;
    surfaceDetailWeight=texture2DRect(surfaceData,vec2(6.5,row)).w;
#ifdef SPRINTATHON_TEXTURE_ARRAYS
    materialLayer=texture2DRect(surfaceData,vec2(6.5,row)).z;
#endif
    bakeLayers=texture2DRect(surfaceData,vec2(7.5,row));
    eyePosition=(gl_ModelViewMatrix*gl_Vertex).xyz;
    vec3 n=normalize(gl_NormalMatrix*texture2DRect(surfaceData,vec2(42.5,row)).xyz);
    vec4 tangent=texture2DRect(surfaceData,vec2(43.5,row));
    vec3 t=normalize(gl_NormalMatrix*tangent.xyz);
    vec3 b=normalize(cross(n,t)*tangent.w);
    tangentView=-vec3(dot(t,eyePosition),dot(b,eyePosition),dot(n,eyePosition));
    worldHeight=gl_Vertex.z;
}
)GLSL";
static const char* fragmentSource=R"GLSL(#version 120
#extension GL_ARB_texture_rectangle : enable
uniform sampler2D texture0;
uniform sampler2D texture1;
#ifdef SPRINTATHON_TEXTURE_ARRAYS
uniform sampler2DArray colorArray;
uniform sampler2DArray bumpArray;
uniform float useArrays;
#endif
uniform float useBump;
uniform float distantDetail;
uniform sampler2DRect surfaceData;
uniform sampler2DRect dynamicLights;
uniform sampler2DRect sceneryLights;
uniform vec4 fogSettings;
uniform float playerLight;
uniform sampler2D bakedSky;
uniform sampler2D bakedTexture;
uniform vec4 bakedSun;
uniform vec4 bakedAmbient;
uniform float bakedSector;
varying vec2 uv;
varying vec3 eyePosition;
#ifndef SPRINTATHON_EARLY_CLIP
varying vec2 portalDistance;
#endif
varying float sectorShade;
varying float classicDepth;
varying float worldHeight;
varying vec3 dynamicWorld;
varying vec3 tangentView;
varying float surfaceRow;
varying float surfaceDetailWeight;
varying vec2 bakeUV;
varying vec4 bakeRect;
varying vec4 bakeLayers;
varying vec2 bakeFlags;
#ifdef SPRINTATHON_TEXTURE_ARRAYS
varying float materialLayer;
#endif
vec4 surfaceColor(vec2 coord){
#ifdef SPRINTATHON_TEXTURE_ARRAYS
    if(useArrays>.5)return texture(colorArray,vec3(coord,materialLayer));
#endif
    return texture2D(texture0,coord);
}
vec4 surfaceBump(vec2 coord){
#ifdef SPRINTATHON_TEXTURE_ARRAYS
    if(useArrays>.5)return texture(bumpArray,vec3(coord,materialLayer));
#endif
    return texture2D(texture1,coord);
}
float liquidCaustic(float tag,vec3 source){
    float phase=tag-(tag>=10.0?10.0:2.0);
    vec3 relative=dynamicWorld-source;
    float scale=mix(.60,1.80,smoothstep(0.0,4.0,max(relative.z/1024.0,0.0)));
    vec3 p=relative/(768.0*scale);vec2 q=p.xy+p.z*vec2(.37,-.29);
    float a=sin(q.x*5.1+q.y*2.7+phase*2.0),b=sin(q.x*-3.2+q.y*5.8-phase);
    float ridge=1.0-smoothstep(.06,.38,abs(a+b));
    return (tag>=10.0?.5:1.0)*.85*ridge*ridge;
}
void pointLight(inout vec3 intensity,float id){
    if(id<.5 || all(greaterThanEqual(intensity,vec3(1.0))))return;
    vec4 position=texture2DRect(dynamicLights,vec2(.5,id+.5));
    vec3 delta=(dynamicWorld-position.xyz)*position.w;float d2=dot(delta,delta);
    if(d2<1.0){vec3 color=texture2DRect(dynamicLights,vec2(1.5,id+.5)).rgb;
        intensity=clamp(intensity+color*(1.0-d2)*(1.0-d2),0.0,1.0);}
}
void surfaceLights(inout vec3 intensity,out vec3 caustic){
    caustic=vec3(0.0);
    vec4 info=texture2DRect(surfaceData,vec2(8.5,surfaceRow));
    vec4 liquid=texture2DRect(surfaceData,vec2(15.5,surfaceRow));
    for(int i=0;i<20;++i){
        if(float(i)>=info.x || all(greaterThanEqual(intensity,vec3(1.0))))break;
        vec4 ids=texture2DRect(surfaceData,vec2(9.5+floor(float(i)/4.0),surfaceRow));
        int component=i-(i/4)*4;float id=ids[component];if(id<.5)continue;
        vec4 position=texture2DRect(dynamicLights,vec2(.5,id+.5));
        vec3 delta=(dynamicWorld-position.xyz)*position.w;float d2=dot(delta,delta);
        if(d2>=1.0)continue;
        vec4 color=texture2DRect(dynamicLights,vec2(1.5,id+.5));
        vec4 plane=texture2DRect(dynamicLights,vec2(2.5,id+.5));
        float facing=smoothstep(-32.0,96.0,dot(dynamicWorld,plane.xyz)+plane.w);
        vec3 contribution=color.rgb*((1.0-d2)*(1.0-d2)/(1.0+16.0*d2)*facing);
        float gain=1.0;
        if(color.a>=2.0){
            gain=color.a>=10.0?1.0:liquid.x;
            float peak=max(contribution.r,max(contribution.g,contribution.b))*liquid.y;
            if(peak>.002)caustic+=contribution*liquid.y*smoothstep(.002,.01,peak)*liquidCaustic(color.a,position.xyz);
        }
        intensity=clamp(intensity+contribution*gain,0.0,1.0);
    }
    for(float i=info.y;i<info.z;i+=1.0){
        if(all(greaterThanEqual(intensity,vec3(1.0))))break;
        vec2 coordinate=vec2(mod(i,128.0)*2.0+.5,floor(i/128.0)+.5);
        vec4 position=texture2DRect(sceneryLights,coordinate);
        vec3 delta=(dynamicWorld-position.xyz)*position.w;float d2=dot(delta,delta);
        if(d2<1.0){
            vec4 color=texture2DRect(sceneryLights,coordinate+vec2(1.0,0.0));
            if(bakeFlags.y>.5 && color.a<0.0)continue;
            intensity=clamp(intensity+color.rgb*((1.0-d2)*(1.0-d2)/(1.0+16.0*d2)),0.0,1.0);
        }
    }
}
vec3 blendSector(vec3 intensity,float lamp,float diffuse,float detail){
    vec4 settings=texture2DRect(surfaceData,vec2(16.5,surfaceRow));
    if(detail<=0.0 || settings.y<.5)return intensity;
    vec4 axis=texture2DRect(surfaceData,vec2(17.5,surfaceRow));
    vec2 position=dynamicWorld.xy;
    if(axis.w>.5)position=vec2(dot(dynamicWorld.xy,axis.xy)+axis.z,dynamicWorld.z);
    float weight=0.0,sum=0.0,width=settings.x;
    for(int i=0;i<8;++i){
        if(float(i)>=settings.y)break;
        vec4 edge=texture2DRect(surfaceData,vec2(18.5+float(i),surfaceRow));
        if(edge.w<0.0)continue;
        float distance=dot(position,edge.xy)+edge.z;
        vec4 span=texture2DRect(surfaceData,vec2(26.5+float(i),surfaceRow));
        vec2 delta=span.zw-span.xy;float length2=dot(delta,delta);
        if(distance>=0.0 && distance<width+.0625 && length2>1.0){
            float t=clamp(dot(position-span.xy,delta)/length2,0.0,1.0);
            float separation=length(position-(span.xy+t*delta));
            if(separation<width){
                float amount=.5*(1.0-smoothstep(0.0,width,separation));
                float shade=edge.w>lamp?edge.w+lamp*.5:edge.w*.5+lamp;
                sum+=clamp(shade*diffuse,0.0,1.0)*amount;weight+=amount;
            }
        }
    }
    if(weight>0.0)intensity=mix(intensity,vec3(sum/weight),min(weight,.5)*detail);
    return intensity;
}
void lightPools(inout vec3 intensity){
    for(int i=0;i<4;++i){
        vec4 color=texture2DRect(surfaceData,vec2(38.5+float(i),surfaceRow));
        if(color.a<=0.0)continue;
        vec4 pool=texture2DRect(surfaceData,vec2(34.5+float(i),surfaceRow));
        vec2 delta=(dynamicWorld.xy-pool.xy)*pool.w;float distance2=dot(delta,delta);
        if(distance2<1.0)intensity=clamp(intensity+color.rgb*(1.0-smoothstep(0.0,1.0,sqrt(distance2))),0.0,1.0);
    }
}
void main(){
#ifndef SPRINTATHON_EARLY_CLIP
    if(min(portalDistance.x,portalDistance.y)<0.0)discard;
#endif
    float distance=length(eyePosition);
    float detail=distantDetail>.5?1.0-smoothstep(16384.0,32768.0,distance):1.0;
    detail*=surfaceDetailWeight;
    vec2 texUV=uv;float diffuse=1.0;
    if(useBump>.5 && surfaceDetailWeight>0.0){
        vec3 viewv=normalize(tangentView);
        if(detail>0.0)for(int i=0;i<4;++i){
            float height=(surfaceBump(texUV).a*.010-.005)*detail;
            texUV.x+=height*viewv.x;texUV.y-=height*viewv.y;
        }
        diffuse=.5+abs(viewv.z)*.5;
        if(detail>0.0){
            vec3 normal=(surfaceBump(texUV).rgb-.5)*2.0;
            diffuse=mix(diffuse,.5+abs(dot(normal,viewv))*.5,detail);
        }
    }
    diffuse=mix(1.0,diffuse,surfaceDetailWeight);
    vec4 texel=surfaceColor(texUV);
    float lamp=clamp(playerLight-(useBump>.5?distance/8192.0:classicDepth),0.0,1.0);
    float sectorLamp=bakeFlags.x>.5?0.0:lamp;
    float shade=sectorShade>sectorLamp?sectorShade+sectorLamp*.5:sectorShade*.5+sectorLamp;
    vec3 intensity=blendSector(vec3(clamp(shade*diffuse,0.0,1.0)),sectorLamp,diffuse,detail);
    if(bakeFlags.x>0.5){
        vec2 halfTexel=vec2(0.5/1024.0);
        vec2 lowUV=vec2(bakeUV.x,(bakeUV.y+bakeLayers.x)*bakeLayers.w);
        vec2 highUV=vec2(bakeUV.x,(bakeUV.y+bakeLayers.y)*bakeLayers.w);
        vec2 lo=bakeRect.xy+halfTexel,hi=bakeRect.xy+bakeRect.zw-halfTexel;
        vec2 light=texture2D(bakedSky,clamp(bakeRect.xy+lowUV*bakeRect.zw,lo,hi)).rg;
        if(bakeLayers.z>0.0 && bakeLayers.x!=bakeLayers.y)
            light=mix(light,texture2D(bakedSky,clamp(bakeRect.xy+highUV*bakeRect.zw,lo,hi)).rg,bakeLayers.z);
        intensity=vec3(bakedAmbient.a)+light.r*bakedSun.rgb+light.g*bakedAmbient.rgb
                  +intensity*clamp(bakedSector,0.0,1.0);
        if(lamp>0.0)intensity=intensity.r>lamp?intensity+vec3(lamp*0.5):intensity*0.5+vec3(lamp);
        intensity=clamp(intensity,0.0,1.0);
    }
    if(surfaceDetailWeight>0.0){
    vec3 beforePools=intensity;lightPools(intensity);
    vec4 pointIds=texture2DRect(surfaceData,vec2(14.5,surfaceRow));
    pointLight(intensity,pointIds.y);pointLight(intensity,pointIds.x);
    intensity=mix(beforePools,intensity,surfaceDetailWeight);
    }
    if(bakeFlags.x>0.5){
        vec2 halfTexel=vec2(0.5/1024.0),lo=bakeRect.xy+halfTexel;
        if(bakeFlags.y>0.5){
            vec2 extent=vec2(bakeRect.z,bakeRect.w*bakeLayers.w);
            vec2 pos=clamp(bakeRect.xy+bakeUV*extent,lo,bakeRect.xy+extent-halfTexel);
            intensity=clamp(intensity+texture2D(bakedTexture,pos).rgb,0.0,1.0);
        }
    }
    vec3 caustic=vec3(0.0);
    if(surfaceDetailWeight>0.0){
        vec3 beforeLights=intensity;surfaceLights(intensity,caustic);
        intensity=mix(beforeLights,intensity,surfaceDetailWeight);caustic*=surfaceDetailWeight;
    }
    vec3 color=clamp(texel.rgb*intensity,0.0,1.0);
    caustic*=dot(texel.rgb,vec3(.299,.587,.114));
    vec3 headroom=(vec3(1.0)-color)/max(caustic,vec3(.00001));
    color+=caustic*clamp(min(headroom.r,min(headroom.g,headroom.b)),0.0,1.0);
    float fog=1.0;
    if(fogSettings.x==0.0)fog=clamp((gl_Fog.end-distance)/(gl_Fog.end-gl_Fog.start),0.0,1.0);
    else if(fogSettings.x==1.0)fog=clamp(exp(-gl_Fog.density*distance),0.0,1.0);
    else if(fogSettings.x==2.0)fog=clamp(exp(-gl_Fog.density*gl_Fog.density*distance*distance),0.0,1.0);
    if(fogSettings.y>0.0){
        float heightFog=clamp((fogSettings.z-worldHeight)/max(fogSettings.w,0.0001),0.0,1.0);
        fog=1.0-(1.0-fog)*mix(1.0,heightFog,fogSettings.y);
    }
    gl_FragColor=vec4(mix(gl_Fog.color.rgb,color,fog),texel.a);
}
)GLSL";

inline GLhandleARB compile(GLenum type,const char* source){
    std::string selected(source);
    if(earlyClip || arrayMode)selected.replace(0,selected.find('\n')+1,
        std::string("#version 130\n")+(earlyClip?"#define SPRINTATHON_EARLY_CLIP 1\n":"")+
        (arrayMode?"#define SPRINTATHON_TEXTURE_ARRAYS 1\n":""));
    source=selected.c_str();
    GLhandleARB shader=glCreateShaderObjectARB(type);
    glShaderSourceARB(shader,1,&source,nullptr);glCompileShaderARB(shader);
    GLint okay=0;glGetObjectParameterivARB(shader,GL_OBJECT_COMPILE_STATUS_ARB,&okay);
    if(!okay){char log[2048]={};glGetInfoLogARB(shader,sizeof(log),nullptr,log);
        std::fprintf(stderr,"Opaque world shader failed: %s\n",log);glDeleteObjectARB(shader);return 0;}
    return shader;
}
inline void preload(){
    retainedSurfaces.clear();retainedState.clear();retainedNextState.clear();retainedPolygonState.clear();retainedPolygonVersions.clear();
    clearAtlas();
    if(context==SDL_GL_GetCurrentContext()){
        if(!arrayTextures.empty())glDeleteTextures(GLsizei(arrayTextures.size()),arrayTextures.data());
        if(buffer)glDeleteBuffersARB(1,&buffer);
        glDeleteTextures(GLsizei(parameters.size()),parameters.data());
        glDeleteTextures(GLsizei(dynamicTextures.size()),dynamicTextures.data());
        if(program)glDeleteObjectARB(program);
    }
    context=SDL_GL_GetCurrentContext();buffer=0;parameters.fill(0);dynamicTextures.fill(0);program=0;
    parameterCount=parameterSlot=0;earlyClip=false;arrayMode=false;
    arrayTextures.clear();arrayMaterials.clear();
    geometry.clear();materials.clear();groups.clear();instances.clear();table.clear();usedBytes=0;active=false;
    if(!requested())return;
    GLint vertexUnits=0,units=0,maxSize=0;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,&vertexUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&units);
    glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE_ARB,&maxSize);
    multiDraw=reinterpret_cast<MultiDraw>(SDL_GL_GetProcAddress("glMultiDrawArrays"));
    if(!multiDraw)multiDraw=reinterpret_cast<MultiDraw>(SDL_GL_GetProcAddress("glMultiDrawArraysEXT"));
    if(vertexUnits<1 || units<8 || maxSize<dynamicLimit || !multiDraw || !SDL_GL_ExtensionSupported("GL_ARB_texture_float")){
        std::fprintf(stderr,"Opaque world: unsupported vertex textures/float texture/multidraw; using original renderer\n");return;
    }
    // GL_CLIP_PLANE0/1 and GL_CLIP_DISTANCE0/1 share the same enum values.
    // GLSL 1.30 supplies per-vertex clip distances on OpenGL 3+ compatibility contexts.
    int major=0,minor=0;
    const char* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if(version)std::sscanf(version,"%d.%d",&major,&minor);
    earlyClip=earlyClipRequested() && major>=3;
    const char* arrays=std::getenv("SPRINTATHON_WORLD_TEXTURE_ARRAYS");
    GLint fragmentUnits=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragmentUnits);
    arrayImage3D=reinterpret_cast<ArrayImage3D>(SDL_GL_GetProcAddress("glTexImage3D"));
    arraySubImage3D=reinterpret_cast<ArraySubImage3D>(SDL_GL_GetProcAddress("glTexSubImage3D"));
    arrayMode=arrays && arrays[0]=='1' && major>=3 && units>=9 && fragmentUnits>=9 && arrayImage3D && arraySubImage3D;
    if(arrays && arrays[0]=='1' && !arrayMode)std::fprintf(stderr,"World texture arrays unsupported; using ordinary materials\n");
    std::fprintf(stderr,"Opaque world portal clipping: %s\n",
        earlyClip?"before rasterization":"fragment discard");
    auto vs=compile(GL_VERTEX_SHADER_ARB,vertexSource),fs=compile(GL_FRAGMENT_SHADER_ARB,fragmentSource);
    if((!vs || !fs) && arrayMode){
        if(vs)glDeleteObjectARB(vs);if(fs)glDeleteObjectARB(fs);
        arrayMode=false;std::fprintf(stderr,"World texture array shader unavailable; using ordinary materials\n");
        vs=compile(GL_VERTEX_SHADER_ARB,vertexSource);fs=compile(GL_FRAGMENT_SHADER_ARB,fragmentSource);
    }
    if(!vs || !fs){if(vs)glDeleteObjectARB(vs);if(fs)glDeleteObjectARB(fs);return;}
    program=glCreateProgramObjectARB();glAttachObjectARB(program,vs);glAttachObjectARB(program,fs);
    glLinkProgramARB(program);glDeleteObjectARB(vs);glDeleteObjectARB(fs);
    GLint okay=0;glGetObjectParameterivARB(program,GL_OBJECT_LINK_STATUS_ARB,&okay);
    if(!okay){char log[2048]={};glGetInfoLogARB(program,sizeof(log),nullptr,log);
        std::fprintf(stderr,"Opaque world link failed: %s\n",log);glDeleteObjectARB(program);program=0;return;}
    const auto oldProgram=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glUseProgramObjectARB(program);
    glUniform1iARB(glGetUniformLocationARB(program,"texture0"),0);
    glUniform1iARB(glGetUniformLocationARB(program,"texture1"),1);
    arrayLocation=glGetUniformLocationARB(program,"useArrays");
    if(arrayMode){
        glUniform1iARB(glGetUniformLocationARB(program,"colorArray"),2);
        glUniform1iARB(glGetUniformLocationARB(program,"bumpArray"),8);
    }
    bumpLocation=glGetUniformLocationARB(program,"useBump");
    detailLocation=glGetUniformLocationARB(program,"distantDetail");
    glUniform1iARB(glGetUniformLocationARB(program,"surfaceData"),6);
    glUniform1iARB(glGetUniformLocationARB(program,"dynamicLights"),7);
    glUniform1iARB(glGetUniformLocationARB(program,"sceneryLights"),3);
    glUniform1iARB(glGetUniformLocationARB(program,"bakedSky"),4);
    glUniform1iARB(glGetUniformLocationARB(program,"bakedTexture"),5);
    sunLocation=glGetUniformLocationARB(program,"bakedSun");
    ambientLocation=glGetUniformLocationARB(program,"bakedAmbient");
    sectorLocation=glGetUniformLocationARB(program,"bakedSector");
    fogLocation=glGetUniformLocationARB(program,"fogSettings");lightLocation=glGetUniformLocationARB(program,"playerLight");
    glUseProgramObjectARB(oldProgram);
    GLint oldBuffer=0,oldActive=0,oldTexture=0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB,&oldBuffer);glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&oldActive);
    glGenBuffersARB(1,&buffer);glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB,bufferBytes,nullptr,GL_STATIC_DRAW_ARB);
    GLint allocated=0;glGetBufferParameterivARB(GL_ARRAY_BUFFER_ARB,GL_BUFFER_SIZE_ARB,&allocated);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB,oldBuffer);
    if(allocated!=int(bufferBytes)){glDeleteBuffersARB(1,&buffer);buffer=0;return;}
    rowLimit=std::min(8192,maxSize);table.resize(size_t(rowLimit)*surfaceFloats,0);
    glActiveTextureARB(GL_TEXTURE0_ARB+6);glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&oldTexture);
    parameterCount=rotateParameters()?unsigned(parameters.size()):1;
    glGenTextures(GLsizei(parameterCount),parameters.data());
    bool allocatedTextures=true;
    for(unsigned i=0;i<parameterCount;++i){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,parameters[i]);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA32F_ARB,surfaceColumns,rowLimit,0,GL_RGBA,GL_FLOAT,nullptr);
        GLint width=0;glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&width);
        if(!parameters[i] || width!=surfaceColumns)allocatedTextures=false;
    }
    glGenTextures(GLsizei(parameterCount),dynamicTextures.data());
    for(unsigned i=0;i<parameterCount;++i){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dynamicTextures[i]);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA32F_ARB,3,std::min(dynamicLimit,maxSize),0,GL_RGBA,GL_FLOAT,nullptr);
        GLint width=0;glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&width);
        if(!dynamicTextures[i] || width!=3)allocatedTextures=false;
    }
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,oldTexture);glActiveTextureARB(oldActive);
    if(!allocatedTextures){
        glDeleteTextures(GLsizei(parameters.size()),parameters.data());parameters.fill(0);
        glDeleteTextures(GLsizei(dynamicTextures.size()),dynamicTextures.data());dynamicTextures.fill(0);parameterCount=0;return;
    }
    preloadAtlas();
    if(arrayMode)preloadArrays();
    std::fprintf(stderr,"Opaque world prototype: baked + dynamic lighting; persistent 8 MiB geometry, %u surface rows; shader compiled during loading; parameter textures %u\n",rowLimit,parameterCount);
}
inline bool begin(){
    active=requested() && program && buffer && parameterCount && parameters[0] && context==SDL_GL_GetCurrentContext();
    materials.clear();groups.clear();instances.clear();uploadRows=0;
    surfacePreparation.clear();dynamicPreparation.clear();distantFull=distantTransition=0;
    surfacePrepHits=surfacePrepMisses=dynamicPrepHits=dynamicPrepMisses=0;
    dynamicIds.clear();dynamicData.assign(12,0.0f);sceneryTexture=0;
    seen=queued=drawCalls=uploads=fallback=skippedPlatforms=skippedMaterials=0;
    arrayQueued=arrayDrawCalls=0;
    retainedHits=retainedMisses=retainedInvalidations=retainedLocalInvalidations=0;retainedEdgeHits=retainedEdgeBuilds=0;retainedValidationMs=0;
    beginTime=std::chrono::steady_clock::now();
    if(active && retainedEnabled())beginRetainedFrame();
    return active;
}
inline Material* material(shape_descriptor shape){
    auto it=materials.find(shape);if(it!=materials.end())return it->second.texture?&it->second:nullptr;
    Material m;m.manager=std::make_unique<TextureManager>();auto& t=*m.manager;t.ShapeDesc=shape;
    if(shape!=UNONE){
        get_shape_bitmap_and_shading_table(shape,&t.Texture,&t.ShadingTables,_shading_normal);
        t.TextureType=OGL_Txtr_Wall;t.TransferMode=_textured_transfer;t.TransferData=0;t.IsShadeless=false;
        if(t.Texture && t.Setup() && !t.IsBlended() && !t.IsGlowMapped()){
            t.RenderNormal();
            const bool bump=TEST_FLAG(Get_OGL_ConfigureData().Flags,OGL_Flag_BumpMap);
            if(bump){glActiveTextureARB(GL_TEXTURE1_ARB);t.RenderBump();glActiveTextureARB(GL_TEXTURE0_ARB);}
            if(!t.GetOpaqueBatchMaterial(m.texture,m.bump,m.swapped))m.texture=0;
            if(!bump)m.bump=0;
        }
    }
    auto binding=arrayMaterials.find(shape);
    if(binding!=arrayMaterials.end() && binding->second.source==m.texture && binding->second.sourceBump==m.bump){
        m.arrayTexture=binding->second.texture;m.arrayBump=binding->second.bump;m.layer=binding->second.layer;
    }
    auto result=materials.emplace(shape,std::move(m)).first;
    return result->second.texture?&result->second:nullptr;
}
// Loading-only material copies. Originals remain available to the normal renderer.
struct ArraySpec {
    int width=0,height=0,levels=0,format=GL_RGBA8;
    GLint minFilter=0,magFilter=0,wrapS=0,wrapT=0;
    GLfloat minLod=0,maxLod=0,bias=0,anisotropy=1;
    size_t bytes=0;
    std::vector<float> key()const{return {float(width),float(height),float(levels),float(format),
        float(minFilter),float(magFilter),float(wrapS),float(wrapT),minLod,maxLod,bias,anisotropy};}
};
inline bool arraySpec(GLuint texture,ArraySpec& spec,bool anisotropy){
    if(!texture)return false;
    glBindTexture(GL_TEXTURE_2D,texture);
    GLint format=0,base=0,maxLevel=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&spec.width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&spec.height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
    // Decode ordinary 8-bit/compressed colour to RGBA8; retain sRGB decoding.
    switch(format){
        case GL_RGB:case GL_RGBA:case GL_RGB8:case GL_RGBA8:
        case 0x84ED:case 0x84EE: // generic compressed RGB/RGBA
        case 0x83F0:case 0x83F1:case 0x83F2:case 0x83F3:break; // S3TC
        case 0x8C41:case 0x8C43:case 0x8C48:case 0x8C49:
        case 0x8C4C:case 0x8C4D:case 0x8C4E:case 0x8C4F:spec.format=0x8C43;break;
        default:return false;
    }
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&base);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&maxLevel);
    if(base!=0 || spec.width<1 || spec.height<1)return false;
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&spec.minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&spec.magFilter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&spec.wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&spec.wrapT);
    auto supportedWrap=[](GLint v){return v==GL_REPEAT || v==GL_MIRRORED_REPEAT || v==GL_CLAMP_TO_EDGE;};
    if(!supportedWrap(spec.wrapS) || !supportedWrap(spec.wrapT))return false;
    glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,&spec.minLod);
    glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,&spec.maxLod);
    glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,&spec.bias);
    if(anisotropy)glGetTexParameterfv(GL_TEXTURE_2D,0x84FE,&spec.anisotropy);
    int w=spec.width,h=spec.height;
    const bool mips=spec.minFilter!=GL_NEAREST && spec.minFilter!=GL_LINEAR;
    for(int level=0;;++level){
        GLint mw=0,mh=0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&mw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&mh);
        if(mw!=w || mh!=h)return false;
        ++spec.levels;spec.bytes+=size_t(w)*h*4;
        if(!mips || (w==1 && h==1) || level>=maxLevel)break;
        w=std::max(1,w/2);h=std::max(1,h/2);
    }
    return true;
}
inline GLuint makeArray(const ArraySpec& spec,const std::vector<GLuint>& sources,bool anisotropy){
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(arrayTarget,texture);
    glTexParameteri(arrayTarget,GL_TEXTURE_MIN_FILTER,spec.minFilter);
    glTexParameteri(arrayTarget,GL_TEXTURE_MAG_FILTER,spec.magFilter);
    glTexParameteri(arrayTarget,GL_TEXTURE_WRAP_S,spec.wrapS);glTexParameteri(arrayTarget,GL_TEXTURE_WRAP_T,spec.wrapT);
    glTexParameteri(arrayTarget,GL_TEXTURE_MAX_LEVEL,spec.levels-1);
    glTexParameterf(arrayTarget,GL_TEXTURE_MIN_LOD,spec.minLod);glTexParameterf(arrayTarget,GL_TEXTURE_MAX_LOD,spec.maxLod);
    glTexParameterf(arrayTarget,GL_TEXTURE_LOD_BIAS,spec.bias);
    if(anisotropy)glTexParameterf(arrayTarget,0x84FE,spec.anisotropy);
    int w=spec.width,h=spec.height;
    for(int level=0;level<spec.levels;++level){
        arrayImage3D(arrayTarget,level,spec.format,w,h,GLsizei(sources.size()),0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        GLint actualWidth=0,actualDepth=0;
        glGetTexLevelParameteriv(arrayTarget,level,GL_TEXTURE_WIDTH,&actualWidth);
        glGetTexLevelParameteriv(arrayTarget,level,GL_TEXTURE_DEPTH,&actualDepth);
        if(actualWidth!=w || actualDepth!=int(sources.size())){glDeleteTextures(1,&texture);return 0;}
        std::vector<unsigned char> pixels(size_t(w)*h*4);
        for(size_t layer=0;layer<sources.size();++layer){
            glBindTexture(GL_TEXTURE_2D,sources[layer]);glGetTexImage(GL_TEXTURE_2D,level,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            arraySubImage3D(arrayTarget,level,0,0,GLint(layer),w,h,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        }
        w=std::max(1,w/2);h=std::max(1,h/2);
    }
    return texture;
}
inline void preloadArrays(){
    // At most 128 MiB extra, never allocate or upload arrays during gameplay.
    const size_t budget=128u*1024u*1024u;size_t used=0;unsigned banks=0;
    GLint maxLayers=0;glGetIntegerv(arrayMaxLayers,&maxLayers);if(maxLayers<2)return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    GLint oldActive=0,oldArray=0,pack=0,unpack=0;
    glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&oldActive);glActiveTextureARB(GL_TEXTURE0_ARB);
    glGetIntegerv(arrayBinding,&oldArray);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&unpack);
    glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,0);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,0);glPixelStorei(GL_UNPACK_SKIP_IMAGES,0);
    std::map<shape_descriptor,bool> shapes;
    auto add=[&](shape_descriptor shape){if(shape!=UNONE && !AnimTxtr_IsAnimated(shape))shapes[shape]=true;};
    for(int i=0;i<dynamic_world->polygon_count;++i){
        const auto* p=get_polygon_data(i);add(p->floor_texture);add(p->ceiling_texture);
        for(int e=0;e<p->vertex_count;++e)if(p->side_indexes[e]!=NONE){
            const auto* side=get_side_data(p->side_indexes[e]);add(side->primary_texture.texture);add(side->secondary_texture.texture);
        }
    }
    struct Candidate {shape_descriptor shape;GLuint color,bump;ArraySpec colorSpec,bumpSpec;};
    std::map<std::vector<float>,std::vector<Candidate>> compatible;
    const bool aniso=SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic");
    for(const auto& shape:shapes){
        auto* m=material(shape.first);if(!m)continue;
        Candidate c;c.shape=shape.first;c.color=m->texture;c.bump=m->bump;
        if(!arraySpec(c.color,c.colorSpec,aniso) || (c.bump && !arraySpec(c.bump,c.bumpSpec,aniso)))continue;
        auto key=c.colorSpec.key();key.push_back(c.bump?1.0f:0.0f);
        if(c.bump){auto normalKey=c.bumpSpec.key();key.insert(key.end(),normalKey.begin(),normalKey.end());}
        compatible[key].push_back(c);
    }
    for(const auto& group:compatible){
        const auto& candidates=group.second;
        const size_t bytes=candidates.front().colorSpec.bytes+candidates.front().bumpSpec.bytes;
        for(size_t start=0;start<candidates.size();){
            const size_t count=std::min(std::min(size_t(std::min(64,maxLayers)),candidates.size()-start),(budget-used)/bytes);
            if(count<2)break;
            std::vector<GLuint> colors,normals;
            for(size_t i=0;i<count;++i){colors.push_back(candidates[start+i].color);normals.push_back(candidates[start+i].bump);}
            const GLuint color=makeArray(candidates[start].colorSpec,colors,aniso);
            const GLuint normal=color && normals[0]?makeArray(candidates[start].bumpSpec,normals,aniso):0;
            if(!color || (normals[0] && !normal)){
                if(color)glDeleteTextures(1,&color);
                std::fprintf(stderr,"World texture arrays: allocation failed; retaining ordinary materials\n");break;
            }
            arrayTextures.push_back(color);if(normal)arrayTextures.push_back(normal);
            for(size_t i=0;i<count;++i){
                const auto& c=candidates[start+i];ArrayBinding binding;
                binding.source=c.color;binding.sourceBump=c.bump;binding.texture=color;binding.bump=normal;binding.layer=int(i);
                arrayMaterials[c.shape]=binding;
            }
            used+=count*bytes;++banks;start+=count;
        }
    }
    materials.clear(); // Recreate per-view managers with validated array bindings.
    glActiveTextureARB(GL_TEXTURE0_ARB);glBindTexture(arrayTarget,oldArray);
    glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,pack);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,unpack);
    glPopClientAttrib();glPopAttrib();glActiveTextureARB(oldActive);
    std::fprintf(stderr,"World texture arrays: %u materials in %u banks, %.1f MiB extra; built during loading\n",
        unsigned(arrayMaterials.size()),banks,used/(1024.0*1024.0));
}

inline bool stationary(const polygon_data* p){
    if(!p || p->type==_polygon_is_platform)return false;
    for(int i=0;i<p->vertex_count;++i)if(p->adjacent_polygon_indexes[i]!=NONE &&
        get_polygon_data(p->adjacent_polygon_indexes[i])->type==_polygon_is_platform)return false;
    return true;
}
inline void plane(float* out,const long_vector2d& n,float degrees,const view_data* view){
    const double a=degrees*0.017453292519943295;
    const double length=std::hypot(double(n.i),double(n.j));
    if(length<1e-12){out[0]=out[1]=out[2]=0;out[3]=1;return;}
    const double x=(std::cos(a)*n.i-std::sin(a)*n.j)/length;
    const double y=(std::sin(a)*n.i+std::cos(a)*n.j)/length;
    out[0]=float(x);out[1]=float(y);out[2]=0;
    out[3]=float(-x*view->origin.x-y*view->origin.y);
}
inline bool submit(Material& m,int count,const float* vertices,const float* uv,float shade,float depth,
                   clipping_window_data* window,const view_data* view,const BakeEntry* bake=nullptr,const SurfaceRecord* detail=nullptr,const GeometryKey* preparedGeometry=nullptr,float detailWeight=1.0f){
    if(count<3 || count>16)return false;
    GeometryKey temporary;
    if(!preparedGeometry){
        temporary.count=count;
        for(int i=0;i<count;++i){
            for(int j=0;j<3;++j)temporary.points[i*5+j]=vertices[i*3+j];
            for(int j=0;j<2;++j)temporary.points[i*5+3+j]=uv[i*2+j];
        }
    }
    const GeometryKey& key=preparedGeometry?*preparedGeometry:temporary;
    for(float x:key.points)if(!std::isfinite(x))return false;
    if(!std::isfinite(shade)||!std::isfinite(depth))return false;
    InstanceKey instance{key,instances[key]++};
    auto found=geometry.find(instance);
    if(found==geometry.end()){
        const int triangles=(count-2)*3;const size_t bytes=size_t(triangles)*6*sizeof(float);
        if(geometry.size()>=rowLimit || usedBytes+bytes>bufferBytes)return false;
        Entry entry;entry.row=unsigned(geometry.size());entry.first=GLint(usedBytes/(6*sizeof(float)));entry.count=triangles;
        std::vector<float> data;data.reserve(triangles*6);
        for(int t=1;t<count-1;++t)for(int v:{0,t,t+1}){
            for(int j=0;j<5;++j)data.push_back(key.points[v*5+j]);
            data.push_back(float(entry.row));
        }
        glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);glBufferSubDataARB(GL_ARRAY_BUFFER_ARB,usedBytes,bytes,data.data());
        glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);usedBytes+=bytes;++uploads;
        found=geometry.emplace(instance,entry).first;
    }
    const auto& e=found->second;float* row=table.data()+size_t(e.row)*surfaceFloats;
    row[0]=shade;row[1]=depth;row[2]=m.swapped?1:0;row[3]=0;
    const float yaw=view->yaw*(360.0f/FULL_CIRCLE)+90;
    plane(row+4,window->left,yaw-0.1f,view);plane(row+8,window->right,yaw+0.1f,view);
    if(window->left.i==outerLeft.i && window->left.j==outerLeft.j){row[4]=row[5]=row[6]=0;row[7]=1;}
    if(window->right.i==outerRight.i && window->right.j==outerRight.j){row[8]=row[9]=row[10]=0;row[11]=1;}
    std::fill(row+12,row+surfaceFloats,0.0f);
    row[31]=1.0f;
    if(bake){
        std::copy(bake->u.begin(),bake->u.end(),row+12);std::copy(bake->v.begin(),bake->v.end(),row+16);
        std::copy(bake->rect.begin(),bake->rect.end(),row+20);
        row[24]=1;row[25]=bake->textureLight && SprintathonSkyBake::textureBakeActive()?1:0;
        row[31]=1.0f/bake->layers;
        if(bake->path){auto blend=SprintathonLuaSun::pathBlend();row[28]=blend.lo;row[29]=blend.hi;row[30]=blend.fraction;}
    }
    row[26]=float(m.layer);row[27]=detailWeight;
    if(detailWeight<=0.0f)++distantFull;else if(detailWeight<1.0f)++distantTransition;
    const SurfaceRecord neutral;
    const auto& detailData=detail?*detail:neutral;
    std::copy(detailData.values.begin(),detailData.values.end(),row+64);
    if(detailWeight>0.0f && !prepareDynamic(count,vertices,row[25]>.5f,row,view))return false;
    uploadRows=std::max(uploadRows,e.row+1);
    if(m.arrayTexture)++arrayQueued;
    auto& g=groups[GroupKey{m.arrayTexture?m.arrayTexture:m.texture,m.arrayTexture?m.arrayBump:m.bump,bake?bake->page:-1,m.arrayTexture!=0}];g.first.push_back(e.first);g.count.push_back(e.count);++queued;return true;
}
inline void finish(void (*traceDraw)()=nullptr){
    if(!active)return;
    const auto submitStart=std::chrono::steady_clock::now();
    if(queued){
        glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        GLint oldActive=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&oldActive);
        glActiveTextureARB(GL_TEXTURE0_ARB+6);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,parameters[parameterSlot]);
        // Upload all referenced rows to this slot, including rows last used with another
        // texture. A three-slot ring reduces immediate reuse; it does not guarantee
        // stall-free updates if the GPU is more than three frames behind.
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,surfaceColumns,uploadRows,GL_RGBA,GL_FLOAT,table.data());
        glActiveTextureARB(GL_TEXTURE0_ARB+7);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dynamicTextures[parameterSlot]);
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,3,GLsizei(dynamicData.size()/12),GL_RGBA,GL_FLOAT,dynamicData.data());
        glActiveTextureARB(GL_TEXTURE0_ARB+3);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,sceneryTexture);
        glPopClientAttrib();
        glUseProgramObjectARB(program);glUniform4fvARB(fogLocation,1,fogSettings);glUniform1fARB(lightLocation,playerLight);
        const auto sun=SprintathonSkyBake::sunColor,ambient=SprintathonSkyBake::ambientColor;
        glUniform4fARB(sunLocation,float(sun.x),float(sun.y),float(sun.z),0);
        glUniform4fARB(ambientLocation,float(ambient.x),float(ambient.y),float(ambient.z),float(SprintathonSkyBake::ambientFloor()));
        glUniform1fARB(sectorLocation,SprintathonSkyBake::sectorStrength());
        const char* detail=std::getenv("SPRINTATHON_DISTANT_SURFACE_DETAIL");
        glUniform1fARB(detailLocation,graphics_preferences->simplify_distant_surfaces && (!detail || detail[0]!='0')?1.0f:0.0f);
        if(earlyClip){glEnable(GL_CLIP_PLANE0);glEnable(GL_CLIP_PLANE1);}
        else{glDisable(GL_CLIP_PLANE0);glDisable(GL_CLIP_PLANE1);}
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.5);
        glActiveTextureARB(GL_TEXTURE0_ARB);glEnable(GL_TEXTURE_2D);
        glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3,GL_FLOAT,6*sizeof(float),nullptr);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(3,GL_FLOAT,6*sizeof(float),reinterpret_cast<const void*>(3*sizeof(float)));
        for(const auto& pair:groups){
            if(pair.first.page>=0){
                const auto& page=atlasPages[pair.first.page];
                glActiveTextureARB(GL_TEXTURE0_ARB+4);glBindTexture(GL_TEXTURE_2D,page.sky);
                glActiveTextureARB(GL_TEXTURE0_ARB+5);glBindTexture(GL_TEXTURE_2D,page.light);
            }
            if(arrayMode)glUniform1fARB(arrayLocation,pair.first.arrays?1.0f:0.0f);
            if(pair.first.arrays){
                glActiveTextureARB(GL_TEXTURE0_ARB+2);glBindTexture(arrayTarget,pair.first.texture);
                glActiveTextureARB(GL_TEXTURE0_ARB+8);glBindTexture(arrayTarget,pair.first.bump);
            }
            glActiveTextureARB(GL_TEXTURE1_ARB);glBindTexture(GL_TEXTURE_2D,pair.first.arrays?0:pair.first.bump);
            glUniform1fARB(bumpLocation,pair.first.bump?1.0f:0.0f);
            glActiveTextureARB(GL_TEXTURE0_ARB);
            glBindTexture(GL_TEXTURE_2D,pair.first.arrays?0:pair.first.texture);const auto& g=pair.second;
            if(pair.first.arrays)++arrayDrawCalls;
            if(traceDraw)traceDraw();
            multiDraw(GL_TRIANGLES,g.first.data(),g.count.data(),GLsizei(g.first.size()));++drawCalls;
        }
        glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);Shader::disable();glPopClientAttrib();glPopAttrib();glActiveTextureARB(oldActive);
        parameterSlot=(parameterSlot+1)%parameterCount;
    }
    if(SprintathonStartupTiming::get().active() && (SprintathonStartupTiming::get().frame_id%60==0 || SprintathonStartupTiming::get().frame_id==1)){
        const auto now=std::chrono::steady_clock::now();
        if(retainedEnabled())std::fprintf(stderr,"Retained surfaces: %u hits / %u builds; %u records; invalidations %u; local removals %u; validation %.3f ms; edges %u reused / %u rebuilt\n",
            retainedHits,retainedMisses,unsigned(retainedSurfaces.size()),retainedInvalidations,retainedLocalInvalidations,retainedValidationMs,retainedEdgeHits,retainedEdgeBuilds);
        if(arrayMode)std::fprintf(stderr,"World texture arrays: %u of %u surfaces, %u of %u material draws\n",
            arrayQueued,queued,arrayDrawCalls,drawCalls);
        std::fprintf(stderr,"Opaque world frame %llu: seen %u queued %u -> %u material draws; cache %u uploads %u; fallback %u platform %u material %u; opaque stage %.3f ms flush %.3f ms; parameter textures %u next slot %u; LIT OPAQUE PROTOTYPE (bump/sector edges/pools)\n",
            (unsigned long long)SprintathonStartupTiming::get().frame_id,seen,queued,drawCalls,unsigned(geometry.size()),uploads,
            fallback,skippedPlatforms,skippedMaterials,std::chrono::duration<double,std::milli>(now-beginTime).count(),
            std::chrono::duration<double,std::milli>(now-submitStart).count(),parameterCount,parameterSlot);
    }
    if(SprintathonStartupTiming::get().active() && SprintathonStartupTiming::get().frame_id%60==0){
        if(distantWorldEnabled())std::fprintf(stderr,"Distant world: %u simplified / %u transitioning / %u queued surfaces\n",distantFull,distantTransition,queued);
        std::fprintf(stderr,"Opaque dynamic frame %llu: %u shared light records, one upload per view\n",
            (unsigned long long)SprintathonStartupTiming::get().frame_id,unsigned(dynamicData.size()/12)-1);
    }
    if(SprintathonStartupTiming::get().active() && SprintathonStartupTiming::get().frame_id%60==0)
        std::fprintf(stderr,"Opaque preparation frame %llu: reuse %s; surface %u hits / %u builds; dynamic %u hits / %u builds\n",
            (unsigned long long)SprintathonStartupTiming::get().frame_id,reusePreparation()?"on":"off",
            surfacePrepHits,surfacePrepMisses,dynamicPrepHits,dynamicPrepMisses);
    active=false;
}
}
#endif
