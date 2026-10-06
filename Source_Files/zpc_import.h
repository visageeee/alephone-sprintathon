#ifndef SPRINTATHON_ZPC_IMPORT_H
#define SPRINTATHON_ZPC_IMPORT_H
// Experimental ZPC exploration importer. GPL-3.0-or-later.
// Kept header-only so the same implementation is built by autotools, Xcode and MSVC.
// No engine structures or global shape-descriptor changes: Marathon stays unchanged.
#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <iterator>

namespace zpc_import {
using Bytes = std::vector<uint8_t>;
using Tags = std::map<std::string, Bytes>;
inline void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error("ZPC import: " + message);
}
inline void bounds(const Bytes& b, size_t p, size_t n) {
    require(p <= b.size() && n <= b.size() - p, "truncated or invalid data range");
}
inline uint32_t get(const Bytes& b, size_t p, size_t n) {
    bounds(b,p,n); uint32_t v=0;
    for (size_t i=0;i<n;++i) v=(v<<8)|b[p+i];
    return v;
}
inline void put(Bytes& b,size_t p,uint32_t v,size_t n) {
    bounds(b,p,n);
    for(size_t i=n;i>0;--i) { b[p+i-1]=uint8_t(v); v>>=8; }
}
inline Bytes slice(const Bytes& b,size_t p,size_t n) {
    bounds(b,p,n); return Bytes(b.begin()+p,b.begin()+p+n);
}
inline void append(Bytes& b,const Bytes& a) { b.insert(b.end(),a.begin(),a.end()); }
inline void number(Bytes& b,uint32_t v,size_t n) {
    size_t p=b.size(); b.resize(p+n); put(b,p,v,n);
}
inline Bytes bytes(const std::string& s) { return Bytes(s.begin(),s.end()); }
inline Bytes collection(const Bytes& b,unsigned i,unsigned variant=0) {
    size_t h=i*32+4+variant*8;
    uint32_t p=get(b,h,4), n=get(b,h+4,4);
    if(p==0xffffffff) return {};
    require(p>=1024 && n>=544,"invalid Shapes collection");
    Bytes c=slice(b,p,n);
    require(get(c,0,2)==3 && get(c,34,4)==n,"unsupported Shapes collection version");
    return c;
}
inline Bytes landscapes(const Bytes& source,const Bytes& raw) {
    require(raw.size()==8*900*400,"landscap.lnd must contain eight 900 x 400 indexed images");
    Bytes sky=collection(source,23);
    unsigned colors=get(sky,6,2);
    require(colors>0 && colors<=256 && get(sky,8,2)==1,"unsupported landscape palette");
    Bytes palette=slice(sky,get(sky,10,4),colors*8);
    Bytes low=slice(sky,get(sky,get(sky,22,4),4),36);
    const unsigned width=1024,height=512,count=8;
    Bytes out(544); append(out,palette);
    size_t lowtable=out.size(); out.resize(out.size()+count*4);
    for(unsigned i=0;i<count;++i) {
        put(out,lowtable+i*4,out.size(),4);
        put(low,6,i,2); append(out,low);
    }
    size_t bitmaptable=out.size(); out.resize(out.size()+count*4);
    for(unsigned i=0;i<count;++i) {
        put(out,bitmaptable+i*4,out.size(),4);
        Bytes bitmap(30+width*4);
        put(bitmap,0,width,2); put(bitmap,2,height,2); put(bitmap,4,height,2);
        put(bitmap,6,0x8000,2); put(bitmap,8,8,2); append(out,bitmap);
        for(unsigned x=0;x<width;++x) for(unsigned y=0;y<height;++y)
            out.push_back(raw[i*900*400+(y*400/height)*900+x*900/width]);
    }
    put(out,0,3,2); put(out,2,1,2); put(out,6,colors,2); put(out,8,1,2);
    put(out,10,544,4); put(out,20,count,2); put(out,22,lowtable,4);
    put(out,26,count,2); put(out,28,bitmaptable,4); put(out,32,8,2);
    put(out,34,out.size(),4); return out;
}
inline Bytes shapes(const Bytes& original,const Bytes& support,const Bytes& raw) {
    // Explicit opt-in format check: ZPC has 64 header slots, not Marathon's 32.
    bounds(original,0,2048);
    require(get(original,4,4)>=2048 && !collection(original,32).empty(),
            "Shapes does not match the supported ZPC layout");
    std::map<unsigned,Bytes> replacements;
    for(unsigned i=5;i<=12;++i) {
        Bytes c=collection(original,i);
        require(!c.empty(),"missing ZPC wall collection");
        // Wall-only collections contain invalid dummy animation records.
        put(c,14,0,2); put(c,16,0,4); replacements[i+12]=c;
    }
    replacements[27]=landscapes(original,raw);
    Bytes out(1024);
    for(unsigned i=0;i<32;++i) for(unsigned v=0;v<2;++v) {
        Bytes c;
        if(replacements.count(i)) { if(v==0) c=replacements.at(i); }
        else {
            c=collection(support,i,v);
            if(c.empty() && v==0 && (i==13 || i==25)) c=collection(support,i==13?12:26);
            require(v!=0 || !c.empty(),"missing Marathon support collection "+std::to_string(i));
        }
        put(out,i*32+4+v*8,c.empty()?0xffffffff:uint32_t(out.size()),4);
        put(out,i*32+8+v*8,c.size(),4); append(out,c);
    }
    return out;
}
inline uint16_t texture(uint16_t value) {
    if(value==0xffff) return value;
    unsigned c=value>>8,f=value&255;
    if(c>=5 && c<=12 && f<40) return uint16_t(((c+12)<<8)|f);
    // Inferred from eight consecutive map landscape collections and eight images.
    if(c>=23 && c<=30 && f==0) return uint16_t((27<<8)|(c-23));
    throw std::runtime_error("ZPC import: unsupported surface texture "+std::to_string(value));
}
inline std::string lua() {
    return "Triggers = {}\n"
           "function Triggers.init(restoring)\n"
           " Players.print('ZPC exploration mode: ' .. Level.name)\n"
           " Players.print('Console: .1 through .36, .next, .open')\nend\n"
           "function Triggers.idle()\n for p in Players() do\n"
           " p.weapons.active = false\n p.energy = 150\n p.oxygen = 10800\n end\nend\n";
}
inline Bytes embedded_lua() {
    Bytes b; number(b,1,2); number(b,0,4);
    Bytes name(66); auto s=bytes("ZPC exploration controls"); std::copy(s.begin(),s.end(),name.begin());
    append(b,name); auto script=bytes(lua()); number(b,script.size(),4); append(b,script); return b;
}
inline Tags convert(const Tags& src) {
    Tags out;
    for(const auto& k:{"LINS","POLY","SIDS","LITE","NOTE","EPNT","iidx","PLAT"})
        if(src.count(k)) out[k]=src.at(k);
    for(const auto& k:{"LINS","POLY","SIDS","EPNT","Minf","OBJS"})
        require(src.count(k)!=0,std::string("missing map chunk ")+k);
    auto& poly=out.at("POLY"); auto& sides=out.at("SIDS");
    require(poly.size()%128==0 && sides.size()%64==0,"invalid polygon or side record size");
    const std::set<unsigned> allowed{0,1,2,3,5,6,7,8,9,11,12,17};
    for(size_t p=0;p<poly.size();p+=128) {
        for(unsigned f:{40,42}) put(poly,p+f,texture(get(poly,p+f,2)),2);
        if(!allowed.count(get(poly,p,2))) put(poly,p,0,2);
        for(unsigned f:{56,116,118,120,122,124}) put(poly,p+f,0xffff,2);
    }
    for(size_t p=0;p<sides.size();p+=64) {
        for(unsigned f:{8,14,20}) put(sides,p+f,texture(get(sides,p+f,2)),2);
        put(sides,p+2,get(sides,p+2,2)&~2u,2); put(sides,p+38,0,4);
    }
    auto& platforms=out["PLAT"];
    require(platforms.size()%140==0,"invalid platform record size");
    for(size_t p=0;p<platforms.size();p+=140) put(platforms,p,0,2);
    const auto& objects=src.at("OBJS"); require(objects.size()%16==0,"invalid object record size");
    for(size_t p=0;p<objects.size();p+=16)
        if(get(objects,p,2)==3) append(out["OBJS"],slice(objects,p,16));
    require(!out["OBJS"].empty(),"map has no player start");
    out["Minf"]=src.at("Minf"); auto& info=out["Minf"];
    require(info.size()==88,"invalid map info");
    std::fill(info.begin(),info.begin()+10,0); put(info,84,1,4);
    out["plac"]=Bytes(1536); out["LUAS"]=embedded_lua(); return out;
}
inline uint32_t crc(const Bytes& b) {
    uint32_t c=0xffffffff;
    for(auto x:b) { c^=x; for(int i=0;i<8;++i) c=(c>>1)^(0xedb88320u & (0u-(c&1))); }
    return ~c;
}
inline Bytes maps(const Bytes& src) {
    require(get(src,0,2)==2 && get(src,2,2)==1 && get(src,80,2)==16 && get(src,82,2)==10,
            "unsupported map WAD version");
    unsigned dir=get(src,72,4), count=get(src,76,2), app=get(src,78,2);
    require(app==74,"unsupported map directory metadata"); bounds(src,dir,size_t(count)*84);
    Bytes out(128), directory; unsigned levels=0;
    for(unsigned i=0;i<count;++i) {
        size_t entry=dir+i*84;
        unsigned offset=get(src,entry,4),size=get(src,entry+4,4),index=get(src,entry+8,2);
        require(offset>=128 && offset<=dir && size<=dir-offset,"invalid map WAD bounds");
        Bytes wad=slice(src,offset,size); Tags tags; std::set<unsigned> visited; unsigned pos=0;
        do {
            require(visited.insert(pos).second,"cyclic map chunk list");
            Bytes key=slice(wad,pos,4); std::string tag(key.begin(),key.end());
            require(get(wad,pos+12,4)==0 && !tags.count(tag),"unsupported map patch or duplicate chunk");
            tags[tag]=slice(wad,size_t(pos)+16,get(wad,pos+8,4)); pos=get(wad,pos+4,4);
        } while(pos);
        if(!tags.count("POLY")) continue; // ZPC picture resources are not playable levels.
        require(index==levels && levels<36,"unsupported level numbering");
        Tags converted=convert(tags); size_t start=out.size();
        for(auto it=converted.begin();it!=converted.end();++it) {
            append(out,bytes(it->first));
            number(out,std::next(it)==converted.end()?0:uint32_t(out.size()-4-start+16+it->second.size()),4);
            number(out,it->second.size(),4); number(out,0,4); append(out,it->second);
        }
        number(directory,start,4); number(directory,out.size()-start,4); number(directory,index,2);
        number(directory,0,2); number(directory,0,2); number(directory,1,4);
        append(directory,slice(converted.at("Minf"),18,66)); ++levels;
    }
    require(levels==36,"expected 36 ZPC levels");
    put(out,0,2,2); put(out,2,1,2); auto name=bytes("ZPC exploration mode");
    std::copy(name.begin(),name.end(),out.begin()+4);
    put(out,72,out.size(),4); put(out,76,levels,2); put(out,78,74,2); put(out,80,16,2); put(out,82,10,2);
    append(out,directory); put(out,68,crc(out),4); return out;
}
inline std::string mml() {
    std::string s="<?xml version=\"1.0\"?>\n<marathon>\n"
        " <scenario name=\"ZPC Exploration\" id=\"zpc-native-v1\" version=\"0.1\"/>\n"
        " <console use_lua_console=\"true\">\n"
        "  <macro input=\"next\" output=\"Players[0]:teleport_to_level((Level.index + 1) % 36)\"/>\n"
        "  <macro input=\"open\" output=\"for p in Platforms() do p.contracting = true; p.active = true end\"/>\n";
    for(unsigned i=0;i<36;++i) s+="  <macro input=\""+std::to_string(i+1)+"\" output=\"Players[0]:teleport_to_level("+std::to_string(i)+")\"/>\n";
    return s+" </console>\n <texture_loading landscapes=\"false\">\n"
        "  <texture_env index=\"0\" which=\"0\" coll=\"17\"/>\n"
        "  <texture_env index=\"0\" which=\"1\" coll=\"-1\"/>\n </texture_loading>\n</marathon>\n";
}
} // namespace zpc_import
#endif
