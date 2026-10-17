#ifndef SPRINTATHON_OPAQUE_GEOMETRY_H
#define SPRINTATHON_OPAQUE_GEOMETRY_H
#include <array>
#include <map>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

// Stage one: retain immutable position/UV data without changing portal draws.
namespace SprintathonOpaqueGeometry {
static GLuint buffer=0;
static SDL_GLContext context=nullptr;
static size_t used=0;
static const size_t capacity=8*1024*1024,entryLimit=16384;
struct Key {
 int count;
 std::array<GLfloat,80> data;
 bool operator<(const Key& other)const {
  return count!=other.count ? count<other.count : data<other.data;
 }
};
static std::map<Key,size_t> entries;
struct CpuKey {
 int count=0;
 std::array<double,48> inputs{};
 bool operator<(const CpuKey& other)const{return count!=other.count?count<other.count:inputs<other.inputs;}
};
struct CpuEntry {
 std::array<GLfloat,48> vertices{};
 std::array<GLfloat,32> uv{};
 size_t gpuOffset=size_t(-1);
};
static std::map<CpuKey,CpuEntry> prepared;

inline bool enabled(){const char* p=std::getenv("SPRINTATHON_OPAQUE_VBO");return p && p[0]=='1';}
inline void preload(){
 if(buffer && context==SDL_GL_GetCurrentContext())glDeleteBuffersARB(1,&buffer);
 buffer=0;used=0;entries.clear();prepared.clear();context=SDL_GL_GetCurrentContext();
 if(!enabled())return;
 GLint previous=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB,&previous);
 glGenBuffersARB(1,&buffer);glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);
 glBufferDataARB(GL_ARRAY_BUFFER_ARB,capacity,nullptr,GL_STATIC_DRAW_ARB);
 GLint allocated=0;glGetBufferParameterivARB(GL_ARRAY_BUFFER_ARB,GL_BUFFER_SIZE_ARB,&allocated);
 glBindBufferARB(GL_ARRAY_BUFFER_ARB,previous);
 if(allocated!=int(capacity)){glDeleteBuffersARB(1,&buffer);buffer=0;return;}
 fprintf(stderr,"Opaque geometry cache: reserved 8 MiB; portal draws unchanged.\n");
}
inline bool bind(int count,const GLfloat* vertices,const GLfloat* uv,size_t* savedOffset=nullptr){
 if(!enabled() || !buffer || context!=SDL_GL_GetCurrentContext() || count<3 || count>16)return false;
 Key key={count,{}};
 for(int i=0;i<count;++i){
  for(int j=0;j<3;++j)key.data[5*i+j]=vertices[3*i+j];
  for(int j=0;j<2;++j)key.data[5*i+3+j]=uv[2*i+j];
 }
 for(int i=0;i<count*5;++i)if(!std::isfinite(key.data[i]))return false;
 auto found=entries.find(key);
 if(found==entries.end()){
  size_t bytes=size_t(count)*5*sizeof(GLfloat);
  // Keep old entries instead of flushing the cache or stalling to grow it.
  if(entries.size()>=entryLimit || used+bytes>capacity)return false;
  glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);
  glBufferSubDataARB(GL_ARRAY_BUFFER_ARB,used,bytes,key.data.data());
  found=entries.emplace(key,used).first;used+=bytes;
 }else glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);
 const size_t offset=found->second;
 if(savedOffset)*savedOffset=offset;
 glVertexPointer(3,GL_FLOAT,5*sizeof(GLfloat),reinterpret_cast<const void*>(static_cast<uintptr_t>(offset)));
 glTexCoordPointer(2,GL_FLOAT,5*sizeof(GLfloat),reinterpret_cast<const void*>(static_cast<uintptr_t>(offset+3*sizeof(GLfloat))));
 return true;
}
inline bool bindPrepared(const CpuEntry& entry){
 if(!buffer || context!=SDL_GL_GetCurrentContext() || entry.gpuOffset==size_t(-1))return false;
 glBindBufferARB(GL_ARRAY_BUFFER_ARB,buffer);
 glVertexPointer(3,GL_FLOAT,5*sizeof(GLfloat),reinterpret_cast<const void*>(static_cast<uintptr_t>(entry.gpuOffset)));
 glTexCoordPointer(2,GL_FLOAT,5*sizeof(GLfloat),reinterpret_cast<const void*>(static_cast<uintptr_t>(entry.gpuOffset+3*sizeof(GLfloat))));
 return true;
}
inline CpuEntry* findPrepared(const CpuKey& key){
 if(!enabled() || !buffer || key.count<3 || key.count>16)return nullptr;
 for(double value:key.inputs)if(!std::isfinite(value))return nullptr;
 auto it=prepared.find(key);return it==prepared.end()?nullptr:&it->second;
}
inline CpuEntry* storePrepared(const CpuKey& key,const GLfloat* vertices,const GLfloat* uv){
 if(!enabled() || !buffer || prepared.size()>=entryLimit || key.count<3 || key.count>16)return nullptr;
 for(double value:key.inputs)if(!std::isfinite(value))return nullptr;
 CpuEntry entry;
 for(int i=0;i<key.count*3;++i)entry.vertices[i]=vertices[i];
 for(int i=0;i<key.count*2;++i)entry.uv[i]=uv[i];
 return &prepared.emplace(key,entry).first->second;
}
inline void restore(const GLfloat* vertices,const GLfloat* uv){
 // These callers normally draw from client arrays and enter with buffer zero.
 glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
 glVertexPointer(3,GL_FLOAT,0,vertices);glTexCoordPointer(2,GL_FLOAT,0,uv);
}
}
#endif
