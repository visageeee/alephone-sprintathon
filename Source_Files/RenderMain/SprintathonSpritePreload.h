#ifndef SPRINTATHON_SPRITE_PRELOAD_H
#define SPRINTATHON_SPRITE_PRELOAD_H
#include "shape_descriptors.h"
namespace SprintathonSpritePreload {
struct State{
 unsigned requested[MAXIMUM_COLLECTIONS]={};
 unsigned warmed[MAXIMUM_COLLECTIONS]={};
 bool broad[MAXIMUM_COLLECTIONS]={};
 bool exact=false;
};
inline State& state(){static State s;return s;}
struct ExactPalette{
 bool previous;
 explicit ExactPalette(bool exact=true):previous(state().exact){state().exact=exact;}
 ~ExactPalette(){state().exact=previous;}
};
inline void mark(short code,bool first){
 auto& s=state();const int c=GET_COLLECTION(code);
 if(first){s.requested[c]=0;s.broad[c]=false;}
 if(s.exact)s.requested[c]|=1u<<GET_COLLECTION_CLUT(code);
 else {s.requested[c]=(1u<<MAXIMUM_CLUTS_PER_COLLECTION)-1;s.broad[c]=true;}
}
inline bool wanted(int c,int palette,bool retained){
 const unsigned bit=1u<<palette;auto& s=state();
 // No marking information (e.g. an unusual loading path): preserve all palettes.
 const bool selected=!s.requested[c] || (s.requested[c]&bit);
 return selected && (!retained || !(s.warmed[c]&bit));
}
inline void begin(int c,bool retained){if(!retained)state().warmed[c]=0;}
inline void done(int c,int palette){state().warmed[c]|=1u<<palette;}
}
#endif
