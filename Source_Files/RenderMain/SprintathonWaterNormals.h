#ifndef SPRINTATHON_WATER_NORMALS_H
#define SPRINTATHON_WATER_NORMALS_H
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

// Two independent seamless slope fields in RG and BA. Generated once on the
// CPU, cached and mipmapped; no scenario texture assets are required.
namespace SprintathonWaterNormals {
struct Cache { GLuint texture=0; SDL_GLContext context=nullptr; };
inline Cache& cache() { static Cache c; return c; }
inline void reset() {
    auto& c=cache();
    if(c.texture && c.context==SDL_GL_GetCurrentContext()) glDeleteTextures(1,&c.texture);
    c=Cache{};
}
inline const std::vector<unsigned char>& pixels() {
    static const std::vector<unsigned char> data=[] {
        const int size=128;
        std::vector<double> slopes(size*size*4,0.0);
        uint32_t seed=0x715abc31u;
        auto random=[&]() { seed=seed*1664525u+1013904223u;return double(seed>>8)/16777216.0; };
        for(int field=0;field<2;++field) {
            for(int wave=0;wave<40;++wave) {
                int kx=int(random()*35)-17,ky=int(random()*35)-17;
                if(kx==0 && ky==0)kx=1;
                double length=std::sqrt(double(kx*kx+ky*ky));
                double phase=random()*6.283185307179586;
                double amplitude=1.0/(1.0+length*length*0.055);
                for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
                    double slope=std::cos(6.283185307179586*(kx*x+ky*y)/size+phase)*amplitude;
                    int i=(y*size+x)*4+field*2;
                    slopes[i]+=slope*kx/length;
                    slopes[i+1]+=slope*ky/length;
                }
            }
        }
        double peak=0;
        for(double x:slopes)peak=std::max(peak,std::abs(x));
        std::vector<unsigned char> result(slopes.size());
        for(size_t i=0;i<slopes.size();++i)
            result[i]=static_cast<unsigned char>(std::floor(127.5+slopes[i]*(120.0/peak)+0.5));
        return result;
    }();
    return data;
}
inline void bind() {
    auto& c=cache();
    SDL_GLContext context=SDL_GL_GetCurrentContext();
    if(!context)return;
    if(c.context!=context){c.texture=0;c.context=context;}
    glActiveTextureARB(GL_TEXTURE0_ARB+5);
    if(!c.texture) {
        glGenTextures(1,&c.texture);glBindTexture(GL_TEXTURE_2D,c.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        std::vector<unsigned char> level=pixels();
        for(int size=128,mip=0;;size/=2,++mip) {
            glTexImage2D(GL_TEXTURE_2D,mip,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,level.data());
            if(size==1)break;
            int next=size/2;
            std::vector<unsigned char> smaller(next*next*4);
            for(int y=0;y<next;++y)for(int x=0;x<next;++x)for(int ch=0;ch<4;++ch) {
                int total=0;
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                    total+=level[((y*2+dy)*size+x*2+dx)*4+ch];
                smaller[(y*next+x)*4+ch]=static_cast<unsigned char>((total+2)/4);
            }
            level.swap(smaller);
        }
    } else glBindTexture(GL_TEXTURE_2D,c.texture);
    glActiveTextureARB(GL_TEXTURE0_ARB);
}
}
#endif
