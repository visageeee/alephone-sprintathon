#ifndef SPRINTATHON_TERRAIN_COLOR_H
#define SPRINTATHON_TERRAIN_COLOR_H
// Included only in the OpenGL rasterizer. SDL's BMP reader is enough; there
// is no SDL_image dependency and no scenario-wide texture replacement.
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
namespace SprintathonTerrainColor {
inline const char* path(){return std::getenv("SPRINTATHON_TERRAIN_COLOR");}
inline bool requested(){const char* p=path();return p && *p;}
struct Cache {GLuint texture=0;SDL_GLContext context=nullptr;bool failed=false;};
inline Cache& cache(){static Cache c;return c;}
inline void reset(){auto& c=cache();if(c.texture && c.context==SDL_GL_GetCurrentContext())glDeleteTextures(1,&c.texture);c=Cache{};}
inline bool bind(){
    auto& c=cache();const auto context=SDL_GL_GetCurrentContext();
    if(!context || !requested())return false;
    if(c.context!=context){c=Cache{};c.context=context;}
    if(c.failed)return false;
    glActiveTextureARB(GL_TEXTURE0_ARB);
    if(c.texture){glBindTexture(GL_TEXTURE_2D,c.texture);return true;}
    SDL_Surface* source=SDL_LoadBMP(path());
    if(!source){std::fprintf(stderr,"Terrain colour: %s\n",SDL_GetError());c.failed=true;return false;}
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    if(source->w<1 || source->h<1 || source->w>limit || source->h>limit || source->w>8192 || source->h>8192){
        SDL_FreeSurface(source);std::fprintf(stderr,"Terrain colour exceeds texture size limit\n");c.failed=true;return false;}
    SDL_Surface* rgba=SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_ABGR8888,0);SDL_FreeSurface(source);
    if(!rgba){c.failed=true;std::fprintf(stderr,"Terrain colour conversion: %s\n",SDL_GetError());return false;}
    int w=rgba->w,h=rgba->h;
    std::vector<unsigned char> pixels(size_t(w)*h*4);
    if(SDL_LockSurface(rgba)<0){SDL_FreeSurface(rgba);c.failed=true;return false;}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        // SDL_GetRGBA keeps this portable to either byte order. BMP/SDL rows
        // are top-down; OBJ/OpenGL V=0 is the bottom of the image.
        Uint32 value;std::memcpy(&value,static_cast<unsigned char*>(rgba->pixels)+(h-1-y)*rgba->pitch+x*4,4);
        Uint8 r,g,b,a;SDL_GetRGBA(value,rgba->format,&r,&g,&b,&a);
        size_t i=(size_t(y)*w+x)*4;pixels[i]=r;pixels[i+1]=g;pixels[i+2]=b;pixels[i+3]=255;
    }
    SDL_UnlockSurface(rgba);SDL_FreeSurface(rgba);
    GLint unpackBuffer=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_ARB,&unpackBuffer);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,0);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glGenTextures(1,&c.texture);glBindTexture(GL_TEXTURE_2D,c.texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    for(int mip=0;;++mip){
        glTexImage2D(GL_TEXTURE_2D,mip,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(w==1 && h==1)break;
        int nw=std::max(1,w/2),nh=std::max(1,h/2);
        std::vector<unsigned char> next(size_t(nw)*nh*4);
        for(int y=0;y<nh;++y)for(int x=0;x<nw;++x)for(int ch=0;ch<4;++ch){
            unsigned sum=0,count=0;
            for(int sy=y*h/nh;sy<(y+1)*h/nh;++sy)for(int sx=x*w/nw;sx<(x+1)*w/nw;++sx){sum+=pixels[(size_t(sy)*w+sx)*4+ch];++count;}
            next[(size_t(y)*nw+x)*4+ch]=static_cast<unsigned char>((sum+count/2)/count);
        }
        pixels.swap(next);w=nw;h=nh;
    }
    GLint uploaded=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&uploaded);
    glPopClientAttrib();glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,GLuint(unpackBuffer));
    if(!uploaded){glDeleteTextures(1,&c.texture);c.texture=0;c.failed=true;std::fprintf(stderr,"Terrain colour upload failed\n");return false;}
    return true;
}
}
#endif
