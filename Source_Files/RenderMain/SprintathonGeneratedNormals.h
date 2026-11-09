#ifndef SPRINTATHON_GENERATED_NORMALS_H
#define SPRINTATHON_GENERATED_NORMALS_H

// CPU-only preparation. Call during level loading, never from a draw path.
// Alpha is the existing bump-map neutral height (128); RGB stores tangent normals.
#include "FileHandler.h"
#include "ImageLoader.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include <unordered_map>

namespace SprintathonGeneratedNormals {
constexpr uint32_t version = 2;
constexpr size_t diskBudget = 128u * 1024u * 1024u;
constexpr size_t maxEntry = 32u * 1024u * 1024u;
constexpr int maxDimension = 2048;

struct Level { int width, height; size_t offset; };
struct Image { std::vector<unsigned char> pixels; std::vector<Level> levels; };
struct PreparationStats {
    unsigned candidates=0, authored=0, ineligible=0, unavailable=0;
    unsigned existing=0, uploaded=0, gpuErrors=0, priorGpuErrors=0;
};
inline PreparationStats& preparationStats() { static PreparationStats data; return data; }

// Loading-only staging for array uploads; discarded after material preloading.
inline std::unordered_map<uint32_t,Image>& staging() { static std::unordered_map<uint32_t,Image> data; return data; }
inline void releaseStaging() { staging().clear(); }

struct DiskEntry { FileSpecifier file; size_t bytes; TimeType date; };
struct Cache {
    bool initialized = false, ready = false;
    FileSpecifier directory;
    std::vector<DiskEntry> entries;
    size_t bytes = 0;
};
inline Cache& cache() { static Cache result; return result; }
inline uint64_t hash(const unsigned char* data, size_t size, uint64_t seed) {
    uint64_t value = seed;
    for (size_t i=0; i<size; ++i) { value ^= data[i]; value *= 1099511628211ULL; }
    return value;
}
inline bool filename(const std::string& name) {
    return name.size()==68 && name.substr(64)==".snm" &&
        name.substr(0,64).find_first_not_of("0123456789abcdef")==std::string::npos;
}
inline bool prepareCache() {
    auto& c=cache();
    if (c.initialized) return c.ready;
    c.initialized=true;
    c.directory.SetToImageCacheDir();
    if (!c.directory.IsDir() && !c.directory.MakeDirectory()) return false;
    c.directory += "Sprintathon Generated Normals v1";
    if (!c.directory.IsDir() && !c.directory.MakeDirectory()) return false;
    std::vector<dir_entry> entries;
    if (!c.directory.ReadDirectory(entries)) return false;
    for (const auto& entry : entries) {
        if (entry.is_directory || !filename(entry.name)) continue;
        FileSpecifier file=c.directory; file+=entry.name;
        OpenedFile opened; int32 length=0;
        if (file.Open(opened) && opened.GetLength(length) && length>0) {
            c.entries.push_back({file,size_t(length),entry.date}); c.bytes+=size_t(length);
        }
    }
    std::sort(c.entries.begin(),c.entries.end(),[](const DiskEntry& a,const DiskEntry& b){return a.date<b.date;});
    while (!c.entries.empty() && (c.bytes>diskBudget || c.entries.size()>8192)) {
        if (!c.entries.front().file.Delete()) break;
        c.bytes-=c.entries.front().bytes; c.entries.erase(c.entries.begin());
    }
    c.ready=true;
    return true;
}
inline std::vector<Level> layout(int width,int height,size_t& total) {
    std::vector<Level> levels; total=0;
    while (true) {
        const size_t size=size_t(width)*size_t(height)*4;
        if (size>maxEntry-total) { levels.clear(); total=0; return levels; }
        levels.push_back({width,height,total}); total+=size;
        if (width==1 && height==1) break;
        width=std::max(1,width/2); height=std::max(1,height/2);
    }
    return levels;
}
inline int wrap(int n,int size) { n%=size; return n<0?n+size:n; }
inline void blur(const std::vector<float>& input,std::vector<float>& output,
                 int width,int height,int radius) {
    std::vector<float> horizontal(input.size()); output.resize(input.size());
    const float inv=1.0f/float(radius*2+1);
    for (int y=0;y<height;++y) for(int x=0;x<width;++x) {
        float sum=0; for(int k=-radius;k<=radius;++k) sum+=input[size_t(y)*width+wrap(x+k,width)];
        horizontal[size_t(y)*width+x]=sum*inv;
    }
    for (int y=0;y<height;++y) for(int x=0;x<width;++x) {
        float sum=0; for(int k=-radius;k<=radius;++k) sum+=horizontal[size_t(wrap(y+k,height))*width+x];
        output[size_t(y)*width+x]=sum*inv;
    }
}
inline unsigned char encode(float value) {
    return static_cast<unsigned char>(std::max(0,std::min(255,int(std::lround((value*0.5f+0.5f)*255.0f)))));
}
inline void normal(unsigned char* out,float x,float y,float z) {
    const float length=std::sqrt(x*x+y*y+z*z);
    if (length>0.000001f) { x/=length; y/=length; z/=length; }
    else { x=0; y=0; z=1; }
    out[0]=encode(x); out[1]=encode(y); out[2]=encode(z); out[3]=128;
}
inline Image generate(const ImageDescriptor& source) {
    Image image; const int width=source.GetWidth(),height=source.GetHeight();
    size_t bytes=0; image.levels=layout(width,height,bytes);
    if (image.levels.empty()) return image;
    image.pixels.resize(bytes);
    const auto* rgba=reinterpret_cast<const unsigned char*>(source.GetBuffer());
    const size_t count=size_t(width)*height;
    std::vector<float> luminance(count),smooth,broad;
    for(size_t i=0;i<count;++i)
        luminance[i]=(0.2126f*rgba[i*4]+0.7152f*rgba[i*4+1]+0.0722f*rgba[i*4+2])/255.0f;
    // A small blur suppresses pixel noise; subtracting a wider blur suppresses
    // broad painted lighting and large colour gradients before differentiation.
    blur(luminance,smooth,width,height,1);
    blur(smooth,broad,width,height,4);
    for(size_t i=0;i<count;++i) smooth[i]-=broad[i];
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const float dx=smooth[size_t(y)*width+wrap(x+1,width)]-smooth[size_t(y)*width+wrap(x-1,width)];
        const float dy=smooth[size_t(wrap(y+1,height))*width+x]-smooth[size_t(wrap(y-1,height))*width+x];
        // The prior 0.5 multiplier produced almost-flat normals after the
        // noise and broad-gradient filters, especially with mipmapping. Keep
        // those filters but preserve enough slope to react to moving light.
        normal(&image.pixels[(size_t(y)*width+x)*4],-2.0f*dx,-2.0f*dy,1.0f);
    }
    // Average decoded vectors and renormalize at every level; alpha stays flat.
    for(size_t level=1;level<image.levels.size();++level) {
        const auto& previous=image.levels[level-1]; const auto& current=image.levels[level];
        for(int y=0;y<current.height;++y) for(int x=0;x<current.width;++x) {
            float nx=0,ny=0,nz=0;
            for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
                const int px=wrap(x*2+i,previous.width),py=wrap(y*2+j,previous.height);
                const auto* v=&image.pixels[previous.offset+(size_t(py)*previous.width+px)*4];
                nx+=v[0]/127.5f-1; ny+=v[1]/127.5f-1; nz+=v[2]/127.5f-1;
            }
            normal(&image.pixels[current.offset+(size_t(y)*current.width+x)*4],nx,ny,nz);
        }
    }
    return image;
}
inline Image prepare(const ImageDescriptor& source) {
    const int width=source.GetWidth(),height=source.GetHeight();
    if (source.GetFormat()!=ImageDescriptor::RGBA8 || !source.GetBuffer() ||
        width<1 || height<1 || width>maxDimension || height>maxDimension) return {};
    const size_t inputBytes=size_t(width)*height*4;
    const auto* pixels=reinterpret_cast<const unsigned char*>(source.GetBuffer());
    // Do not infer geometry from transparent or cut-out textures.
    for(size_t i=3;i<inputBytes;i+=4) if(pixels[i]!=255) return {};
    size_t bytes=0; const auto levels=layout(width,height,bytes);
    if(levels.empty())return {};
    const uint64_t first=hash(pixels,inputBytes,14695981039346656037ULL);
    const uint64_t second=hash(pixels,inputBytes,0x8a5cd789635d2dffULL);
    char key[65];
    std::snprintf(key,sizeof(key),"%016llx%016llx%016llx%016llx",
        static_cast<unsigned long long>(first),static_cast<unsigned long long>(second),
        static_cast<unsigned long long>((uint64_t(width)<<32)|uint32_t(height)),
        static_cast<unsigned long long>((uint64_t(version)<<32)|1u)); // generator version and wrapping mode
    FileSpecifier file; bool available=prepareCache();
    if(available) {
        file=cache().directory; file+=std::string(key)+".snm";
        OpenedFile input; int32 length=0; uint32_t header[12]={};
        if(file.Open(input) && input.GetLength(length) && size_t(length)==sizeof(header)+bytes &&
           input.Read(sizeof(header),header) && header[0]==0x534e4d31 && header[1]==version &&
           header[2]==uint32_t(width) && header[3]==uint32_t(height) &&
           header[4]==uint32_t(first) && header[5]==uint32_t(first>>32) &&
           header[6]==uint32_t(second) && header[7]==uint32_t(second>>32) &&
           header[8]==bytes && header[11]==0x01020304) {
            Image image; image.levels=levels; image.pixels.resize(bytes);
            const uint64_t checksum=uint64_t(header[9]) | (uint64_t(header[10])<<32);
            if(input.Read(int32(bytes),image.pixels.data()) &&
               hash(image.pixels.data(),bytes,14695981039346656037ULL)==checksum) return image;
        }
    }
    Image result=generate(source);
    if(!available || result.pixels.size()!=bytes || bytes+48>diskBudget) return result;
    auto& c=cache();
    for(auto it=c.entries.begin();it!=c.entries.end();++it) if(std::strcmp(it->file.GetPath(),file.GetPath())==0) {
        if(!it->file.Delete())return result;
        c.bytes-=it->bytes;c.entries.erase(it);break;
    }
    // Remove a corrupt file even if it was too short to enter the index.
    if(file.Exists() && !file.Delete())return result;
    const size_t total=sizeof(uint32_t)*12+bytes;
    while(!c.entries.empty() && (c.bytes+total>diskBudget || c.entries.size()>=8192)) {
        if(!c.entries.front().file.Delete())return result;
        c.bytes-=c.entries.front().bytes;c.entries.erase(c.entries.begin());
    }
    if(c.bytes+total>diskBudget)return result;
    const uint64_t checksum=hash(result.pixels.data(),bytes,14695981039346656037ULL);
    uint32_t header[]={0x534e4d31,version,uint32_t(width),uint32_t(height),uint32_t(first),uint32_t(first>>32),
        uint32_t(second),uint32_t(second>>32),uint32_t(bytes),uint32_t(checksum),uint32_t(checksum>>32),0x01020304};
    FileSpecifier temp;temp.SetTempName(file);OpenedFile output;
    if(!temp.Open(output,true))return result;
    bool ok=output.Write(sizeof(header),header) && output.Write(int32(bytes),result.pixels.data());
    ok=output.Close() && ok;
    if(!ok || !temp.Rename(file)){temp.Delete();return result;}
    c.entries.push_back({file,total,file.GetDate()});c.bytes+=total;
    return result;
}
}
#endif
