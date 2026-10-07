#ifndef SPRINTATHON_ZPC_IMPORT_H
#define SPRINTATHON_ZPC_IMPORT_H
// Experimental ZPC exploration importer. GPL-3.0-or-later.
// Kept header-only so the same implementation is built by autotools, Xcode and MSVC.
// No engine structures or global shape-descriptor changes: Marathon stays unchanged.
#include "zpc_static_data.h"
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
        // Marathon landscapes store transposed dimensions; each stored column
        // is one horizontal scanline of the panorama.
        Bytes bitmap(30+height*4);
        put(bitmap,0,height,2); put(bitmap,2,width,2); put(bitmap,4,width,2);
        put(bitmap,6,0x8000,2); put(bitmap,8,8,2); append(out,bitmap);
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
            // Reverse panorama rows to match the engine landscape orientation.
            out.push_back(raw[i*900*400+((height-1-y)*400/height)*900+x*900/width]);
    }
    put(out,0,3,2); put(out,2,1,2); put(out,6,colors,2); put(out,8,1,2);
    put(out,10,544,4); put(out,20,count,2); put(out,22,lowtable,4);
    put(out,26,count,2); put(out,28,bitmaptable,4); put(out,32,8,2);
    put(out,34,out.size(),4); return out;
}
// Static displays share banks while retaining each source palette and viewing angle.
struct StaticBank {
    std::vector<Bytes> palettes, high, low, bitmap;
};
struct StaticDisplays {
    std::map<unsigned,Bytes> collections;
    std::map<unsigned,unsigned> descriptors; // key: saved-object type * 256 + index
};
inline Bytes pack_static_bank(const StaticBank& bank) {
    Bytes out(544);
    for(const auto& p:bank.palettes) append(out,p);
    auto table=[&](const std::vector<Bytes>& records) {
        size_t offset=out.size(); out.resize(out.size()+4*records.size());
        for(size_t i=0;i<records.size();++i) {
            put(out,offset+4*i,out.size(),4); append(out,records[i]);
        }
        return offset;
    };
    size_t high=table(bank.high),low=table(bank.low),bitmap=table(bank.bitmap);
    put(out,0,3,2); put(out,2,2,2); // sprite collection
    put(out,6,256,2); put(out,8,bank.palettes.size(),2); put(out,10,544,4);
    put(out,14,bank.high.size(),2); put(out,16,high,4);
    put(out,20,bank.low.size(),2); put(out,22,low,4);
    put(out,26,bank.bitmap.size(),2); put(out,28,bitmap,4);
    put(out,32,8,2); put(out,34,out.size(),4); return out;
}
inline StaticDisplays static_displays(const Bytes& original) {
    const unsigned slots[]={7,8,9,10,11,12,13,14,15,16,25,26,28,29,30,31};
    std::vector<StaticBank> banks(1); StaticDisplays result;
    for(unsigned type=0;type<2;++type) {
        unsigned count=type?153:54;
        for(unsigned id=0;id<count;++id) {
            if(!type && (id==0 || (id>=44 && id<=51))) continue; // player and unused entries without supplied artwork
            const auto& src=type?static_scenery[id]:static_monsters[id];
            auto c=collection(original,src.collection);
            require(!c.empty() && src.sequence<get(c,14,2),"static sprite sequence missing");
            require(get(c,6,2)==256 && src.clut<get(c,8,2),"static sprite palette missing");
            auto palette=slice(c,get(c,10,4)+src.clut*2048,2048);
            size_t p=get(c,get(c,16,4)+src.sequence*4,4);
            // The 90-byte struct includes its first 2-byte frame index.
            // The fixed header on disk is only 88 bytes.
            Bytes high=slice(c,p,88);
            unsigned kind=get(high,38,2),frames=get(high,40,2);
            unsigned views=(kind==1 || kind==10)?1:(kind==3 || kind==4)?4:(kind==9 || kind==11)?5:8;
            require(frames>0 && (kind==1 || kind==2 || kind==3 || kind==4 || kind==5 || kind==8 || kind==9 || kind==10 || kind==11),"unsupported static sprite views");
            auto palette_it=std::find(banks.back().palettes.begin(),banks.back().palettes.end(),palette);
            if(banks.back().low.size()+views>256 || (palette_it==banks.back().palettes.end() && banks.back().palettes.size()==8))
                banks.emplace_back();
            require(banks.size()<=sizeof(slots)/sizeof(slots[0]),"too many static sprite banks");
            auto& bank=banks.back();
            palette_it=std::find(bank.palettes.begin(),bank.palettes.end(),palette);
            unsigned clut=palette_it-bank.palettes.begin();
            if(palette_it==bank.palettes.end()) bank.palettes.push_back(palette);
            unsigned descriptor=((slots[banks.size()-1]|(clut<<5))<<8)|bank.high.size();
            result.descriptors[type*256+id]=descriptor;
            put(high,40,1,2); put(high,42,32767,2); put(high,44,0,2);
            put(high,46,0,2); put(high,48,0,2);
            for(unsigned field:{50,52,54}) put(high,field,0xffff,2);
            put(high,58,0,2);
            for(unsigned v=0;v<views;++v) {
                unsigned lowid=get(c,p+88+2*v*frames,2);
                require(lowid<get(c,20,2),"invalid static low-level frame");
                Bytes low=slice(c,get(c,get(c,22,4)+lowid*4,4),36);
                unsigned bitmapid=get(low,6,2);
                require(bitmapid<get(c,26,2),"invalid static bitmap");
                unsigned bt=get(c,28,4),begin=get(c,bt+bitmapid*4,4),end=c.size();
                for(unsigned j=0;j<get(c,26,2);++j) {
                    unsigned next=get(c,bt+j*4,4); if(next>begin) end=std::min(end,next);
                }
                number(high,bank.low.size(),2);
                put(low,6,bank.bitmap.size(),2); bank.low.push_back(low);
                bank.bitmap.push_back(slice(c,begin,end-begin));
            }
            bank.high.push_back(high);
        }
    }
    for(size_t i=0;i<banks.size();++i) result.collections[slots[i]]=pack_static_bank(banks[i]);
    return result;
}
inline Bytes script_chunk(const std::string& text) {
    Bytes b;number(b,1,2);number(b,0,4);b.resize(b.size()+66);
    number(b,text.size(),4);append(b,bytes(text));return b;
}
inline void add_static_objects(const Tags& src,Tags& out,const StaticDisplays& displays) {
    const auto& objects=src.at("OBJS");
    std::map<unsigned,unsigned> types;
    std::string mml="<marathon><scenery>";
    for(size_t p=0;p<objects.size();p+=16) {
        unsigned type=get(objects,p,2),id=get(objects,p+2,2),key=type*256+id;
        if(type>1 || !displays.descriptors.count(key)) continue;
        if(!types.count(key)) {
            unsigned index=types.size(),d=displays.descriptors.at(key); types[key]=index;
            require(index<61,"too many static types in one level");
            mml+="<object index=\""+std::to_string(index)+"\" flags=\"0\" radius=\"0\" height=\"0\" destruction=\"-1\"><normal><shape coll=\""+
                std::to_string((d>>8)&31)+"\" clut=\""+std::to_string(d>>13)+"\" seq=\""+std::to_string(d&255)+"\"/></normal></object>";
        }
        Bytes obj=slice(objects,p,16);put(obj,0,1,2);put(obj,2,types.at(key),2);
        put(obj,14,get(obj,14,2)&2,2); // retain ceiling placement, expose dormant monsters
        append(out["OBJS"],obj);
    }
    out["MMLS"]=script_chunk(mml+"</scenery></marathon>");
}

// Imported wall tiles must also work with the fixed-size software mapper and
// OpenGL configurations that reject non-power-of-two texture dimensions.
inline void normalize_wall_bitmaps(Bytes& c) {
    const unsigned count=get(c,26,2), table=get(c,28,4), target=128;
    for(unsigned i=0;i<count;++i) {
        const unsigned p=get(c,table+4*i,4);
        const unsigned w=get(c,p,2), h=get(c,p+2,2);
        if(w==target && h==target) continue;
        const unsigned stride=get(c,p+4,2), flags=get(c,p+6,2);
        const bool columns=(flags&0x8000)!=0;
        require(w && h && get(c,p+8,2)==8 && stride!=0xffff &&
                stride>=(columns?h:w), "unsupported ZPC wall bitmap layout");
        const size_t data=p+30+4*size_t(columns?w:h);
        bounds(c,data,size_t(columns?w:h)*stride);
        Bytes bitmap=slice(c,p,30);
        put(bitmap,0,target,2); put(bitmap,2,target,2); put(bitmap,4,target,2);
        bitmap.resize(30+4*target+target*target,0);
        for(unsigned y=0;y<target;++y) for(unsigned x=0;x<target;++x) {
            const unsigned sx=x*w/target, sy=y*h/target;
            bitmap[30+4*target+(columns?x*target+y:y*target+x)]=
                c[data+(columns?sx*stride+sy:sy*stride+sx)];
        }
        put(c,table+4*i,c.size(),4); append(c,bitmap);
    }
    put(c,34,c.size(),4);
}

inline Bytes shapes(const Bytes& original,const Bytes& support,const Bytes& raw, const StaticDisplays* displays=nullptr) {
    // Explicit opt-in format check: ZPC has 64 header slots, not Marathon's 32.
    bounds(original,0,2048);
    require(get(original,4,4)>=2048 && !collection(original,32).empty(),
            "Shapes does not match the supported ZPC layout");
    std::map<unsigned,Bytes> replacements;
    if(displays) replacements=displays->collections;
    for(unsigned i=5;i<=12;++i) {
        Bytes c=collection(original,i);
        require(!c.empty(),"missing ZPC wall collection");
        // Wall-only collections contain invalid dummy animation records.
        put(c,14,0,2); put(c,16,0,4);
        normalize_wall_bitmaps(c); replacements[i+12]=c;
    }
    // Extract chi hands and force sprites; unused ZPC weapon records contain
    // bitmap references that are invalid in the original file.
    Bytes hands=collection(original,1);
    require(get(hands,14,2)>7 && get(hands,6,2)==256,"missing ZPC chi punch animations");
    StaticBank handbank;
    for(unsigned i=0;i<get(hands,8,2);++i)
        handbank.palettes.push_back(slice(hands,get(hands,10,4)+i*2048,2048));
    require(get(hands,8,2)==1,"unsupported chi hand palettes");
    Bytes force=collection(original,2);
    require(get(force,6,2)==256 && get(force,8,2)==1,"unsupported chi force palette");
    handbank.palettes.push_back(slice(force,get(force,10,4),2048));
    for(unsigned index=0;index<4;++index) {
        const unsigned sequences[]={7,3,8,9};
        const unsigned sequence=sequences[index];
        const Bytes& art=index<2?hands:force;
        unsigned p=get(art,get(art,16,4)+sequence*4,4);
        Bytes high=slice(art,p,88);
        unsigned frames=get(high,40,2);
        if(index>=2) {
            require(get(high,46,2)==14,"unexpected ZPC chi transfer mode");
            put(high,46,2,2); // refractive invisibility, not Marathon fold-out
        }
        require(get(high,38,2)==1 && frames>0,"unsupported chi animation");
        for(unsigned field:{50,52,54}) put(high,field,0xffff,2);
        for(unsigned frame=0;frame<frames;++frame) {
            unsigned lowid=get(art,p+88+frame*2,2);
            require(lowid<get(art,20,2),"invalid chi frame");
            Bytes low=slice(art,get(art,get(art,22,4)+lowid*4,4),36);
            unsigned bitmap=get(low,6,2), table=get(art,28,4);
            require(bitmap<get(art,26,2),"invalid chi bitmap");
            unsigned begin=get(art,table+bitmap*4,4),end=art.size();
            for(unsigned j=0;j<get(art,26,2);++j) {
                unsigned next=get(art,table+j*4,4);
                if(next>begin) end=std::min(end,next);
            }
            number(high,handbank.low.size(),2);
            put(low,6,handbank.bitmap.size(),2);
            handbank.low.push_back(low);
            handbank.bitmap.push_back(slice(art,begin,end-begin));
        }
        handbank.high.push_back(high);
    }
    replacements[1]=pack_static_bank(handbank);
    put(replacements[1],2,get(hands,2,2),2);
    put(replacements[1],32,get(hands,32,2),2);
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
    return R"ZPC(Triggers = {}
local chi = ProjectileTypes["fusion bolt minor"]
function Triggers.init(restoring)
 Players.print('ZPC: primary fire = chi punch; aim down near the ground to chi-jump')
 Players.print('Console: .1 through .36, .next, .open')
 for p in Players() do
  for item in ItemTypes() do
   if item.index ~= 0 and item.kind == ItemKinds["weapon"] then p.items[item] = 0 end
  end
  p.items[0] = 1
  p.weapons[0]:select()
 end
end
function Triggers.idle()
 for p in Players() do
  p.weapons.active = true
  p.action_flags.right_trigger = false
  p.energy = 150
  p.oxygen = 10800
 end
end
function Triggers.projectile_created(projectile)
 if projectile.type == chi then projectile.damage_scale = 0 end
end
function Triggers.projectile_detonated(kind, owner, polygon, x, y, z)
 if kind ~= chi or not owner then return end
 for p in Players() do
  if p.monster == owner then
   local dx, dy, dz = p.x-x, p.y-y, p.z-z
   local distance = math.sqrt(dx*dx+dy*dy+dz*dz)
   -- Only close impacts impart force; distant shots cannot propel the player.
   if distance > 0.001 and distance < 1.5 and not p.dead then
    local force = 0.20 * (1-distance/1.5)
    local horizontal = math.sqrt(dx*dx+dy*dy)
    local direction = p.yaw + 180
    if horizontal > 0.001 then direction = math.deg((math.atan2 or math.atan)(dy,dx)) end
    p:accelerate(direction, force*horizontal/distance, force*dz/distance)
   end
  end
 end
end
)ZPC";
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
        for(unsigned f:{40,42}) {
            unsigned original=get(poly,p+f,2), coll=original>>8;
            put(poly,p+f,texture(original),2);
            // Some ZPC ceilings use a landscape image with normal transfer mode.
            // Marathon must render these as sky rather than a tiled wall bitmap.
            if(coll>=23 && coll<=30 && get(poly,p+f+24,2)==0)
                put(poly,p+f+24,9,2);
        }
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
inline Bytes story_terminal(const std::vector<unsigned>& pictures) {
    std::string text=pictures.empty()?"No illustrated story panels are referenced by this map. Use .storyall to browse the recovered story artwork.":"\r";
    Bytes out(10);put(out,4,22,2);put(out,6,pictures.empty()?3:pictures.size()+2,2);
    auto group=[&](unsigned flags,unsigned type,unsigned permutation,unsigned length) {
        number(out,flags,2);number(out,type,2);number(out,permutation,2);
        number(out,0,2);number(out,length,2);number(out,1,2);
    };
    group(0,1,0,0); // unfinished branch, also used as completion fallback
    if(pictures.empty()) group(0,4,0,text.size());
    else for(auto id:pictures) group(2,12,id,1); // centered picture, no scripted actions
    group(0,5,0,0);append(out,bytes(text));out.push_back(0);
    require(out.size()<65536,"story terminal too large");put(out,0,out.size(),2);return out;
}
inline std::vector<unsigned> story_references(const Tags& tags) {
    std::vector<unsigned> ids;
    if(!tags.count("term")) return ids;
    const auto& data=tags.at("term");size_t start=0;
    while(start<data.size()) {
        unsigned size=get(data,start,2),count=get(data,start+6,2);
        require(size>=10 && 10+count*12<=size,"invalid original terminal");bounds(data,start,size);
        for(unsigned i=0;i<count;++i) {
            unsigned p=start+10+i*12,type=get(data,p+2,2),id=get(data,p+4,2);
            if((type==0 || type==12) && id>=10000 && id<11000 && std::find(ids.begin(),ids.end(),id)==ids.end()) ids.push_back(id);
        }
        start+=size;
    }
    return ids;
}

inline Bytes maps(const Bytes& src,const StaticDisplays* displays=nullptr) {
    require(get(src,0,2)==2 && get(src,2,2)==1 && get(src,80,2)==16 && get(src,82,2)==10,
            "unsupported map WAD version");
    unsigned dir=get(src,72,4), count=get(src,76,2), app=get(src,78,2);
    require(app==74,"unsupported map directory metadata"); bounds(src,dir,size_t(count)*84);
    Bytes out(128), directory; unsigned levels=0;
    std::vector<std::pair<unsigned,Tags>> resources;
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
        if(!tags.count("POLY")) {
            if(tags.count("pict") && tags.count("clut")) resources.emplace_back(index,tags);
            continue;
        }
        require(index==levels && levels<36,"unsupported level numbering");
        Tags converted=convert(tags);
        auto refs=story_references(tags);
        std::vector<unsigned> all;
        for(unsigned r=0;r<count;++r) { unsigned id=get(src,dir+r*84+8,2); if(id>=10000 && id<11000) all.push_back(id); }
        for(auto id:refs) require(std::find(all.begin(),all.end(),id)!=all.end(),"missing story panel");
        converted["term"]=story_terminal(refs);append(converted["term"],story_terminal(all));
        if(displays) add_static_objects(tags,converted,*displays);
        size_t start=out.size();
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
    for(const auto& resource:resources) {
        size_t start=out.size();
        for(auto it=resource.second.begin();it!=resource.second.end();++it) {
            append(out,bytes(it->first));
            number(out,std::next(it)==resource.second.end()?0:uint32_t(out.size()-4-start+16+it->second.size()),4);
            number(out,it->second.size(),4);number(out,0,4);append(out,it->second);
        }
        number(directory,start,4);number(directory,out.size()-start,4);number(directory,resource.first,2);
        directory.resize(directory.size()+74);
    }
    put(out,0,2,2); put(out,2,1,2); auto name=bytes("ZPC exploration mode");
    std::copy(name.begin(),name.end(),out.begin()+4);
    put(out,72,out.size(),4); put(out,76,levels+resources.size(),2); put(out,78,74,2); put(out,80,16,2); put(out,82,10,2);
    append(out,directory); put(out,68,crc(out),4); return out;
}
inline std::string mml() {
    std::string s="<?xml version=\"1.0\"?>\n<marathon>\n"
        " <scenario name=\"ZPC Exploration\" id=\"zpc-native-v1\" version=\"0.1\"/>\n"
        " <console use_lua_console=\"true\">\n"
        "  <macro input=\"story\" output=\"Players[0]:activate_terminal(0)\"/>\n"
        "  <macro input=\"storyall\" output=\"Players[0]:activate_terminal(1)\"/>\n"
        "  <macro input=\"next\" output=\"Players[0]:teleport_to_level((Level.index + 1) % 36)\"/>\n"
        "  <macro input=\"open\" output=\"for p in Platforms() do p.contracting = true; p.active = true end\"/>\n";
    for(unsigned i=0;i<36;++i) s+="  <macro input=\""+std::to_string(i+1)+"\" output=\"Players[0]:teleport_to_level("+std::to_string(i)+")\"/>\n";
    s+=" </console>\n <interface>\n";
    // ZPC's seven painted buttons, in 640 x 480 image coordinates.
    for(unsigned index=7;index<=19;++index) {
        unsigned top=0,bottom=0,left=0,right=0;
        const unsigned buttons[]={7,8,9,13,14,11,16};
        const unsigned tops[]={50,102,154,206,258,310,362};
        for(unsigned row=0;row<7;++row) if(index==buttons[row]) {
            top=tops[row]; bottom=top+29; left=32; right=218;
        }
        s+="  <rect index=\""+std::to_string(index)+"\" top=\""+std::to_string(top)+
           "\" left=\""+std::to_string(left)+"\" bottom=\""+std::to_string(bottom)+
           "\" right=\""+std::to_string(right)+"\"/>\n";
    }
    return s+" </interface>\n <texture_loading landscapes=\"false\">\n"
        "  <texture_env index=\"0\" which=\"0\" coll=\"17\"/>\n"
        "  <texture_env index=\"0\" which=\"1\" coll=\"-1\"/>\n </texture_loading>\n</marathon>\n";
}
} // namespace zpc_import
#endif
