#ifndef SPRINTATHON_UPSCALE_CACHE_H
#define SPRINTATHON_UPSCALE_CACHE_H
#include "FileHandler.h"
#include "ImageLoader.h"
#include "SprintathonCacheCodec.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>
#include <cerrno>
#include <chrono>
// Rebuildable CPU pixels, independent of GL objects and scenario IDs.
// Increment the format version and directory when the upscale algorithm changes.
namespace SprintathonUpscaleCache {
constexpr uint32 version=3;
// Disk space only; the existing 256 MiB RAM cache remains unchanged.
inline uint64_t budget(){
 static const uint64_t value=[](){
  const char* text=std::getenv("SPRINTATHON_UPSCALE_DISK_MB");
  if(!text || !*text)return uint64_t(512)*1024*1024;
  char* end=nullptr;errno=0;const unsigned long long mb=std::strtoull(text,&end,10);
  if(errno || end==text || *end || *text=='-' || mb<64 || mb>65536)
   return uint64_t(512)*1024*1024;
  return uint64_t(mb)*1024*1024;
 }();return value;
}
constexpr size_t entryLimit=size_t(64)*1024*1024;
struct Entry{FileSpecifier file;size_t bytes;TimeType date;};
struct State{
 bool initialized=false,ready=false;
 FileSpecifier directory;
 std::list<Entry> entries;
 std::unordered_map<std::string,std::list<Entry>::iterator> index;
 std::unordered_set<std::string> requested;
 // Pin resident entries touched this launch. A sequential preload larger than
 // the budget then keeps a stable useful subset instead of evicting itself.
 std::unordered_set<std::string> pinned;
 bool admissionBlocked=false;
 unsigned long long skipped=0;
 double hashMs=0,loadMs=0,saveMs=0;
 uint64_t bytes=0,requestedBytes=0,rawRequestedBytes=0;
 unsigned long long hits=0,misses=0,writes=0,evictions=0;
};
inline State& state(){static State s;return s;}
struct Timer{
 double& elapsed;std::chrono::steady_clock::time_point start;
 explicit Timer(double& e):elapsed(e),start(std::chrono::steady_clock::now()){}
 ~Timer(){elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
};
inline uint64_t hashPixels(const uint32* p,size_t count){
 uint64_t h=14695981039346656037ULL;
 for(size_t i=0;i<count;++i){h^=p[i];h*=1099511628211ULL;}
 return h;
}
inline bool cacheName(const std::string& n){
 if(n.size()!=68 || n.substr(64)!=".suc")return false;
 return n.substr(0,64).find_first_not_of("0123456789abcdef")==std::string::npos;
}
inline bool prepare(){
 auto& s=state();if(s.initialized)return s.ready;s.initialized=true;
 s.directory.SetToImageCacheDir();
 if(!s.directory.IsDir() && !s.directory.MakeDirectory())return false;
 // Discard only this feature's obsolete, rebuildable v1 files.
 FileSpecifier legacy=s.directory;legacy+="Sprintathon Upscale v1";
 std::vector<dir_entry> old;
 if(legacy.IsDir() && legacy.ReadDirectory(old)){
  for(const auto& e:old){
   if(!e.is_directory && e.name.size()==20 && e.name.substr(16)==".suc" &&
      e.name.substr(0,16).find_first_not_of("0123456789abcdef")==std::string::npos){
    FileSpecifier f=legacy;f+=e.name;f.Delete();
   }
  }
 }
 FileSpecifier oldCompressed=s.directory;oldCompressed+="Sprintathon Upscale v2";
 old.clear();
 if(oldCompressed.IsDir() && oldCompressed.ReadDirectory(old)){
  for(const auto& e:old)if(!e.is_directory && cacheName(e.name)){
   FileSpecifier f=oldCompressed;f+=e.name;f.Delete();
  }
 }
 s.directory += "Sprintathon Upscale v3";
 if(!s.directory.IsDir() && !s.directory.MakeDirectory())return false;
 std::vector<dir_entry> entries;
 if(!s.directory.ReadDirectory(entries))return false;
 for(const auto& e:entries){
  if(e.is_directory || !cacheName(e.name))continue;
  FileSpecifier file=s.directory;file+=e.name;
  OpenedFile opened;int32 length=0;
  if(file.Open(opened) && opened.GetLength(length) && length>=0){
   s.entries.push_back({file,size_t(length),e.date});s.bytes+=size_t(length);
  }
 }
 // Oldest-created entries go first; reads avoid rewriting files.
 s.entries.sort([](const Entry& a,const Entry& b){return a.date<b.date;});
 for(auto it=s.entries.begin();it!=s.entries.end();++it)s.index[it->file.GetPath()]=it;
 while(!s.entries.empty() && (s.bytes>budget() || s.entries.size()>65536)){
  auto& oldest=s.entries.front();
  if(!oldest.file.Delete())break;
  s.bytes-=oldest.bytes;s.index.erase(oldest.file.GetPath());s.entries.pop_front();++s.evictions;
 }
 s.ready=true;
 std::fprintf(stderr,"Upscale disk cache: %s\n",s.directory.GetPath());
 return true;
}
inline bool sizeFor(const ImageDescriptor& source,size_t& count,size_t& bytes){
 if(source.GetWidth()<=0 || source.GetHeight()<=0 ||
    source.GetWidth()>8192 || source.GetHeight()>8192)return false;
 count=size_t(source.GetWidth())*size_t(source.GetHeight());
 bytes=count*16; // decoded upscaled RGBA only
 return bytes<=entryLimit;
}
// Independent, non-cryptographic 64-bit fingerprint; unaligned loads use memcpy.
// Combined with the already computed FNV fingerprint and explicit image metadata.
inline uint64_t fingerprint(const void* data,size_t size){
 const auto* p=static_cast<const unsigned char*>(data);
 const uint64_t m=0xc6a4a7935bd1e995ULL;
 uint64_t h=0x8a5cd789635d2dffULL^(uint64_t(size)*m);
 while(size>=8){
  uint64_t k;std::memcpy(&k,p,8);k*=m;k^=k>>47;k*=m;
  h^=k;h*=m;p+=8;size-=8;
 }
 uint64_t tail=0;for(size_t i=0;i<size;++i)tail|=uint64_t(p[i])<<(i*8);
 h^=tail;if(size)h*=m;h^=h>>47;h*=m;h^=h>>47;return h;
}
inline std::string keyFor(const ImageDescriptor& source,bool wrap,uint64_t memoryHash){
 Timer timing(state().hashMs);
 const size_t count=size_t(source.GetWidth())*source.GetHeight();
 const uint64_t second=fingerprint(source.GetBuffer(),count*4);
 const uint64_t dimensions=(uint64_t(source.GetWidth())<<32)|uint32(source.GetHeight());
 const uint64_t settings=(uint64_t(version)<<32)|uint64_t(wrap)|(uint64_t(source.IsPremultiplied())<<1);
 char key[65];
 std::snprintf(key,sizeof(key),"%016llx%016llx%016llx%016llx",
  static_cast<unsigned long long>(memoryHash),static_cast<unsigned long long>(second),
  static_cast<unsigned long long>(dimensions),static_cast<unsigned long long>(settings));
 return key;
}
inline FileSpecifier pathFor(const std::string& key){
 auto path=state().directory;path+=key+".suc";return path;
}
inline void account(const std::string& key,size_t packed,size_t raw){
 auto& s=state();
 if(s.requested.insert(key).second){s.requestedBytes+=packed;s.rawRequestedBytes+=raw;}
}
inline ImageDescriptor* load(const ImageDescriptor& source,bool wrap,uint64_t memoryHash,std::string* preparedKey=nullptr){
 size_t count,raw;if(!sizeFor(source,count,raw) || !prepare())return nullptr;
 auto& s=state();const std::string key=keyFor(source,wrap,memoryHash);auto file=pathFor(key);
 if(preparedKey)*preparedKey=key;
 Timer timing(s.loadMs);
 OpenedFile opened;int32 length=0;uint32 header[12]={};char storedKey[64];
 ++s.misses;
 if(!file.Open(opened) || !opened.GetLength(length) || length<112 ||
    size_t(length)>entryLimit+112 || !opened.Read(sizeof(header),header) ||
    !opened.Read(64,storedKey))return nullptr;
 if(header[0]!=0x53554333 || header[1]!=version ||
    header[2]!=uint32(source.GetWidth()) || header[3]!=uint32(source.GetHeight()) ||
    header[4]!=(uint32(wrap)|(uint32(source.IsPremultiplied())<<1)) ||
    header[5]!=0x01020304 || header[8]!=size_t(length)-112 ||
    header[9]!=raw || header[10]>1 || header[11]!=0 ||
    !std::equal(key.begin(),key.end(),storedKey))return nullptr;
 std::vector<unsigned char> packed(header[8]);
 if(!opened.Read(int32(packed.size()),packed.data()))return nullptr;
 std::unique_ptr<uint32[]> output(new uint32[count*4]);
 if(header[10]){
  if(!SprintathonCacheCodec::decompress(packed.data(),packed.size(),
     reinterpret_cast<unsigned char*>(output.get()),raw))return nullptr;
 }else{
  if(packed.size()!=raw)return nullptr;
  std::memcpy(output.get(),packed.data(),raw);
 }
 const uint64_t checksum=uint64_t(header[6])|(uint64_t(header[7])<<32);
 if(hashPixels(output.get(),count*4)!=checksum)return nullptr;
 auto* result=new ImageDescriptor(source.GetWidth()*2,source.GetHeight()*2,output.release());
 result->PremultipliedAlpha=source.IsPremultiplied();
 auto entry=s.index.find(file.GetPath());
 if(entry!=s.index.end())s.entries.splice(s.entries.end(),s.entries,entry->second);
 s.pinned.insert(file.GetPath());
 account(key,size_t(length),raw);
 ++s.hits;--s.misses;return result;
}
inline void save(const ImageDescriptor& source,bool wrap,uint64_t memoryHash,const ImageDescriptor& result,const std::string* preparedKey=nullptr){
 size_t count,raw;if(!sizeFor(source,count,raw) || !prepare())return;
 if(result.GetWidth()!=source.GetWidth()*2 || result.GetHeight()!=source.GetHeight()*2)return;
 auto& s=state();
 if(s.admissionBlocked){++s.skipped;return;}
 const std::string key=preparedKey && !preparedKey->empty()?*preparedKey:keyFor(source,wrap,memoryHash);
 auto file=pathFor(key);Timer timing(s.saveMs);
 const auto* pixels=reinterpret_cast<const unsigned char*>(result.GetBuffer());
 auto packed=SprintathonCacheCodec::compress(pixels,raw);
 const bool compressed=packed.size()<raw;
 if(!compressed)packed.assign(pixels,pixels+raw);
 const size_t bytes=112+packed.size();
 account(key,bytes,raw);
 auto existing=s.index.find(file.GetPath());
 if(existing!=s.index.end()){
  auto it=existing->second;
  if(!it->file.Delete())return;
  s.bytes-=it->bytes;s.pinned.erase(it->file.GetPath());s.entries.erase(it);s.index.erase(existing);
 }
 while(!s.entries.empty() && (s.bytes+bytes>budget() || s.entries.size()>=65536)){
  auto victim=s.entries.begin();
  while(victim!=s.entries.end() && s.pinned.count(victim->file.GetPath()))++victim;
  if(victim==s.entries.end()){
   s.admissionBlocked=true;++s.skipped;return;
  }
  if(!victim->file.Delete())return;
  s.bytes-=victim->bytes;s.index.erase(victim->file.GetPath());s.entries.erase(victim);++s.evictions;
 }
 if(s.bytes+bytes>budget())return;
 FileSpecifier temporary;temporary.SetTempName(file);
 const uint64_t checksum=hashPixels(result.GetBuffer(),count*4);
 uint32 header[]={0x53554333,version,uint32(source.GetWidth()),uint32(source.GetHeight()),
  uint32(wrap)|(uint32(source.IsPremultiplied())<<1),0x01020304,
  uint32(checksum),uint32(checksum>>32),uint32(packed.size()),uint32(raw),uint32(compressed),0};
 OpenedFile opened;
 if(!temporary.Open(opened,true))return;
 bool ok=opened.Write(sizeof(header),header) &&
  opened.Write(64,const_cast<char*>(key.data())) &&
  opened.Write(int32(packed.size()),packed.data());
 ok=opened.Close() && ok;
 if(!ok || !temporary.Rename(file)){temporary.Delete();return;}
 s.entries.push_back({file,bytes,file.GetDate()});
 auto newest=s.entries.end();--newest;s.index[file.GetPath()]=newest;
 s.bytes+=bytes;s.pinned.insert(file.GetPath());++s.writes;
}
inline void report(bool reset){
 auto& s=state();
 if(!reset && s.initialized){
  std::fprintf(stderr,"Upscale disk timing: hash %.1f ms; read/decode %.1f ms; compress/write %.1f ms; %llu admissions skipped\n",
   s.hashMs,s.loadMs,s.saveMs,s.skipped);
  std::fprintf(stderr,
   "Upscale disk cache: %llu hits, %llu misses, %llu writes, %llu evictions; %.1f / %.0f MiB; %zu entries\n",
   s.hits,s.misses,s.writes,s.evictions,double(s.bytes)/(1024*1024),double(budget())/(1024*1024),s.entries.size());
  std::fprintf(stderr,
   "Upscale disk working set: %zu unique images, %.1f MiB stored / %.1f MiB decoded in accounted requests%s\n",
   s.requested.size(),double(s.requestedBytes)/(1024*1024),double(s.rawRequestedBytes)/(1024*1024),
   s.requestedBytes>budget() || s.requested.size()>65536?" (exceeds cache capacity)":(s.skipped?" (partial: uncached admissions skipped)":""));
 }
 s.hits=s.misses=s.writes=s.evictions=s.skipped=0;
 s.hashMs=s.loadMs=s.saveMs=0;
 s.requested.clear();s.requestedBytes=s.rawRequestedBytes=0;
}
}
#endif
