#ifndef SPRINTATHON_CUBE_SKY_H
#define SPRINTATHON_CUBE_SKY_H
// Experimental six-face sky. Uses SDL's BMP reader: no SDL_image dependency.
#include <SDL2/SDL.h>
#include "preferences.h"
#include "SprintathonLandscapeCube.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstring>
namespace SprintathonCubeSky {
struct Uniforms {GLint enabled=-1,sampler=-1,stars=-1,time=-1;};
struct Cache {
 SDL_GLContext context=nullptr;GLuint texture=0;bool attempted=false;
 std::map<GLhandleARB,Uniforms> uniforms;
};
inline Cache& cache(){static Cache c;return c;}
inline void reset(){auto& c=cache();
 if(c.texture && c.context==SDL_GL_GetCurrentContext())glDeleteTextures(1,&c.texture);
 c=Cache{};
}
inline bool load(const char* directory){
 auto& c=cache();c.attempted=true;
 const char* names[]={"right.bmp","left.bmp","top.bmp","bottom.bmp","front.bmp","back.bmp"};
 using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)>;
 std::vector<Surface> faces;int size=0;
 GLint limit=0;glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE,&limit);
 for(const char* name:names){
  const std::string path=std::string(directory)+"/"+name;
  Surface source(SDL_LoadBMP(path.c_str()),SDL_FreeSurface);
  if(!source){std::fprintf(stderr,"Cubemap sky: cannot load %s: %s; using original landscape\n",path.c_str(),SDL_GetError());return false;}
  if(source->w!=source->h || source->w<1 || source->w>limit || source->w>2048 || (size && source->w!=size)){
   std::fprintf(stderr,"Cubemap sky: faces must be equal square images, at most 2048 pixels and within GPU limits; using original landscape\n");return false;
  }
  size=source->w;
  Surface rgba(SDL_ConvertSurfaceFormat(source.get(),SDL_PIXELFORMAT_ABGR8888,0),SDL_FreeSurface);
  if(!rgba){std::fprintf(stderr,"Cubemap sky: image conversion failed; using original landscape\n");return false;}
  faces.push_back(std::move(rgba));
 }
 GLint active=0,buffer=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
 glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&buffer);
 glActiveTextureARB(GL_TEXTURE6_ARB);glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
 glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
 glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
 glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
 glGenTextures(1,&c.texture);glBindTexture(GL_TEXTURE_CUBE_MAP,c.texture);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
 glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 std::vector<unsigned char> pixels(size_t(size)*size*4);bool ok=true;
 for(int face=0;face<6 && ok;++face){
  SDL_Surface* rgba=faces[face].get();
  if(SDL_LockSurface(rgba)<0){ok=false;break;}
  // Row zero is the top of each face. Cube sampling maps this to t=-1.
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){
   Uint32 value;std::memcpy(&value,static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch+x*4,4);
   Uint8 r,g,b,a;SDL_GetRGBA(value,rgba->format,&r,&g,&b,&a);
   const size_t at=(size_t(y)*size+x)*4;
   pixels[at]=r;pixels[at+1]=g;pixels[at+2]=b;pixels[at+3]=255;
  }
  SDL_UnlockSurface(rgba);
  const GLenum target=GL_TEXTURE_CUBE_MAP_POSITIVE_X+face;
  glTexImage2D(target,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  GLint uploaded=0;glGetTexLevelParameteriv(target,0,GL_TEXTURE_WIDTH,&uploaded);ok=uploaded==size;
 }
 glPopClientAttrib();glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,GLuint(buffer));
 glActiveTextureARB(active);
 if(!ok){glDeleteTextures(1,&c.texture);c.texture=0;
  std::fprintf(stderr,"Cubemap sky: upload failed; using original landscape\n");return false;}
 std::fprintf(stderr,"Cubemap sky: loaded six %dx%d faces from %s (%.1f MiB)\n",size,size,directory,6.*size*size*4/(1024.*1024.));
 return true;
}
inline void bind(int landscapeDescriptor=-1){
 const auto context=SDL_GL_GetCurrentContext();if(!context)return;
 auto& c=cache();if(c.context!=context){c=Cache{};c.context=context;}
 const GLhandleARB program=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);if(!program)return;
 auto found=c.uniforms.find(program);
 if(found==c.uniforms.end()){
  Uniforms u;u.enabled=glGetUniformLocationARB(program,"sprintathonCubeSkyEnabled");
  u.sampler=glGetUniformLocationARB(program,"sprintathonCubeSky");
  u.stars=glGetUniformLocationARB(program,"sprintathonStarsEnabled");
  u.time=glGetUniformLocationARB(program,"sprintathonStarsTime");
  found=c.uniforms.emplace(program,u).first;
 }
 const Uniforms& u=found->second;if(u.enabled<0)return;
 // Keep sampler2D and samplerCube on distinct units, even when disabled.
 glUniform1iARB(u.sampler,6);
 const char* starsOption=std::getenv("SPRINTATHON_PROCEDURAL_STARS");
 // Explicit 0/1 environment overrides remain useful for comparisons.
 const bool starsRequested=starsOption && (starsOption[0]=='0' || starsOption[0]=='1')
     ? starsOption[0]=='1'
     : graphics_preferences && graphics_preferences->procedural_space_sky;
 // M2/Infinity's fourth landscape is Space (collection 30). M1 uses
 // song_index for music, so it must not participate in this selection.
 const bool spaceLandscape=LandscapesLoaded && static_world &&
     static_world->song_index==(_collection_landscape4-_collection_landscape1);
 const bool stars=starsRequested && spaceLandscape;
 glUniform1iARB(u.stars,stars ? 1 : 0);
 // Simulation time keeps twinkling paused and slowed with gameplay. Periodic
 // phase wrapping would cause a discontinuity, so retain the level clock.
 glUniform1fARB(u.time,dynamic_world ? float(dynamic_world->tick_count)/30.0f : 0.0f);
 const char* directory=std::getenv("SPRINTATHON_CUBEMAP");
 const bool requested=!starsRequested && directory && *directory;
 if(requested && !c.attempted)load(directory);
 const GLuint generated=(!stars && !requested && landscapeDescriptor>=0)
     ? SprintathonLandscapeCube::find(landscapeDescriptor) : 0;
 const GLuint selected=requested ? c.texture : generated;
 glUniform1iARB(u.enabled,selected ? 1 : 0);
 if(selected){
  GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);
  glActiveTextureARB(GL_TEXTURE6_ARB);glBindTexture(GL_TEXTURE_CUBE_MAP,selected);
  glActiveTextureARB(active);
 }
}
}
#endif
