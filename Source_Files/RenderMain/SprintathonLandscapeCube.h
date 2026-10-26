#ifndef SPRINTATHON_LANDSCAPE_CUBE_H
#define SPRINTATHON_LANDSCAPE_CUBE_H
#include "SprintathonLandscapeGradient.h"
#include "preferences.h"
#include <map>
#include <set>
#include <cstdlib>
#include <cstdio>
namespace SprintathonLandscapeCube {
struct Cache {SDL_GLContext context=nullptr;std::map<int,GLuint> textures;std::set<int> attempted;};
inline Cache& cache(){static Cache c;return c;}
inline bool requested(){const char* p=std::getenv("SPRINTATHON_LANDSCAPE_CUBEMAP");return p && (p[0]=='0' || p[0]=='1') ? p[0]=='1' : (graphics_preferences && graphics_preferences->landscape_cubemaps);}
inline void reset(){auto& c=cache();
 if(c.context==SDL_GL_GetCurrentContext())for(const auto& item:c.textures)glDeleteTextures(1,&item.second);
 c=Cache{};c.context=SDL_GL_GetCurrentContext();
}
inline GLuint find(int descriptor){auto& c=cache();
 if(c.context!=SDL_GL_GetCurrentContext())reset();
 if(!requested())return 0;
 auto it=c.textures.find(descriptor);return it==c.textures.end()?0:it->second;
}
// Called only by texture preloading, with the landscape bound on texture unit 0.
inline void prepare(int descriptor,double repeats,double azimuth,double centre,double slope){
 if(!requested() || std::abs(slope)<1e-9)return;
 auto& c=cache();if(c.context!=SDL_GL_GetCurrentContext())reset();
 if(!c.attempted.insert(descriptor).second)return;
 if(c.textures.size()>=8){std::fprintf(stderr,"Landscape cubemap: cache limit reached; retaining original landscape\n");return;}
 GLint width=0,height=0,limit=0;
 glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
 glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
 glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE,&limit);
 if(width<1 || height<1 || width>8192 || height>8192 || size_t(width)*height>16*1024*1024 || limit<512)return;
 SprintathonLandscapeGradient::Panorama p;
 p.width=width;p.height=height;p.repeats=repeats;p.azimuth=azimuth;p.centre=centre;p.slope=slope;
 p.pixels.resize(size_t(width)*height*4);
 GLint pack=0,unpack=0,active=0,binding=0;
 glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_ARB,&pack);
 glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&unpack);
 glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
 glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,0);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
 glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
 glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
 glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
 glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE);
 glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,p.pixels.data());
 p.edgeMeans();
 glActiveTextureARB(GL_TEXTURE6_ARB);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&binding);
 GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
 glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
 glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
 glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_FALSE);
 const int size=512;std::vector<unsigned char> pixels(size*size*4);bool ok=true;
 for(int face=0;face<6 && ok;++face){
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){
   const auto color=SprintathonLandscapeGradient::cubePixel(p,face,(x+.5)*2/size-1,(y+.5)*2/size-1);
   const size_t i=(size_t(y)*size+x)*4;
   pixels[i]=static_cast<unsigned char>(std::lround(std::max(0.,std::min(1.,color.r))*255));
   pixels[i+1]=static_cast<unsigned char>(std::lround(std::max(0.,std::min(1.,color.g))*255));
   pixels[i+2]=static_cast<unsigned char>(std::lround(std::max(0.,std::min(1.,color.b))*255));pixels[i+3]=255;
  }
  const GLenum target=GL_TEXTURE_CUBE_MAP_POSITIVE_X+face;
  glTexImage2D(target,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  GLint uploaded=0;glGetTexLevelParameteriv(target,0,GL_TEXTURE_WIDTH,&uploaded);ok=uploaded==size;
 }
 glBindTexture(GL_TEXTURE_CUBE_MAP,GLuint(binding));glActiveTextureARB(active);
 glPopClientAttrib();glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,GLuint(pack));glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,GLuint(unpack));
 if(ok){c.textures.emplace(descriptor,texture);std::fprintf(stderr,"Landscape cubemap: prepared descriptor %d from %dx%d panorama during loading (6 MiB)\n",descriptor,width,height);}
 else {glDeleteTextures(1,&texture);std::fprintf(stderr,"Landscape cubemap: upload failed; retaining original landscape\n");}
}
}
#endif
