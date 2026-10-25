#ifndef SPRINTATHON_CACHE_CODEC_H
#define SPRINTATHON_CACHE_CODEC_H
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
namespace SprintathonCacheCodec {
inline uint32_t rotr(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
inline std::string sha256(const void* data,size_t size){
 static const uint32_t k[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 const auto* in=static_cast<const unsigned char*>(data);
 const size_t blocks=(size+9+63)/64;
 for(size_t block=0;block<blocks;++block){
  uint32_t w[64]={};
  for(size_t j=0;j<64;++j){
   size_t pos=block*64+j;unsigned char b=0;
   if(pos<size)b=in[pos];else if(pos==size)b=128;
   else if(pos>=blocks*64-8)b=static_cast<unsigned char>((uint64_t(size)*8)>>((blocks*64-1-pos)*8));
   w[j/4]|=uint32_t(b)<<(24-(j%4)*8);
  }
  for(int i=16;i<64;++i){
   uint32_t a=w[i-15],b=w[i-2];
   w[i]=w[i-16]+(rotr(a,7)^rotr(a,18)^(a>>3))+w[i-7]+(rotr(b,17)^rotr(b,19)^(b>>10));
  }
  uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
  for(int i=0;i<64;++i){
   uint32_t t1=v+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
   uint32_t t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));
   v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
  }
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
 }
 const char* hex="0123456789abcdef";std::string out;
 for(auto value:h)for(int i=7;i>=0;--i)out+=hex[(value>>(i*4))&15];
 return out;
}
// Bounded LZ stream: a 16-bit little-endian length, followed by literals
// or a 16-bit backward distance when bit 15 of the length is set.
inline void word(std::vector<unsigned char>& out,unsigned n){out.push_back(n&255);out.push_back((n>>8)&255);}
inline std::vector<unsigned char> compress(const unsigned char* in,size_t size){
 std::vector<unsigned char> out;
 std::vector<int32_t> table(65536,-1);
 auto literal=[&](size_t a,size_t b){
  while(a<b){size_t n=std::min(size_t(32767),b-a);word(out,unsigned(n));out.insert(out.end(),in+a,in+a+n);a+=n;}
 };
 size_t pos=0,start=0;
 while(pos+4<=size){
  uint32_t value;std::memcpy(&value,in+pos,4);
  unsigned slot=(value*2654435761u)>>16;
  int32_t old=table[slot];table[slot]=int32_t(pos);
  if(old>=0 && pos-size_t(old)<=65535 && std::memcmp(in+old,in+pos,4)==0){
   literal(start,pos);size_t n=4;
   while(n<32767 && pos+n<size && in[size_t(old)+n]==in[pos+n])++n;
   word(out,0x8000u|unsigned(n));word(out,unsigned(pos-size_t(old)));
   pos+=n;start=pos;
  }else ++pos;
 }
 literal(start,size);return out;
}
inline bool decompress(const unsigned char* in,size_t size,unsigned char* out,size_t capacity){
 size_t read=0,written=0;
 while(read<size){
  if(size-read<2)return false;
  unsigned control=unsigned(in[read])|(unsigned(in[read+1])<<8);read+=2;
  size_t n=control&32767;
  if(!n || n>capacity-written)return false;
  if(control&0x8000){
   if(n<4 || size-read<2)return false;
   size_t distance=size_t(in[read])|(size_t(in[read+1])<<8);read+=2;
   if(!distance || distance>written)return false;
   for(size_t i=0;i<n;++i)out[written+i]=out[written+i-distance];
  }else{
   if(n>size-read)return false;
   std::memcpy(out+written,in+read,n);read+=n;
  }
  written+=n;
 }
 return written==capacity;
}
}
#endif
