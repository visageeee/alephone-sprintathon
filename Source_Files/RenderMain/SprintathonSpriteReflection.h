#ifndef SPRINTATHON_SPRITE_REFLECTION_H
#define SPRINTATHON_SPRITE_REFLECTION_H
#include <vector>
#include <cmath>
// Projective attributes stay homogeneous while clipping to the pool polygon.
namespace SprintathonSpriteReflection {
struct Vertex { double x,y,u,v,q; };
inline std::vector<Vertex> clip(const std::vector<Vertex>& input,double ax,double ay,double bx,double by,double sign) {
    std::vector<Vertex> out;
    if(input.empty())return out;
    auto distance=[&](const Vertex& p){return sign*((bx-ax)*(p.y-ay)-(by-ay)*(p.x-ax));};
    Vertex previous=input.back();double dp=distance(previous);
    for(const auto& current:input) {
        double dc=distance(current);
        if((dc>=0)!=(dp>=0)) {
            double t=dp/(dp-dc);
            out.push_back({previous.x+t*(current.x-previous.x),previous.y+t*(current.y-previous.y),
                previous.u+t*(current.u-previous.u),previous.v+t*(current.v-previous.v),previous.q+t*(current.q-previous.q)});
        }
        if(dc>=0)out.push_back(current);
        previous=current;dp=dc;
    }
    return out;
}
inline Vertex project(double ex,double ey,double ez,double x,double y,double z,double h,double u,double v) {
    double t=(ez-h)/(ez-(2*h-z));
    return {ex+t*(x-ex),ey+t*(y-ey),u*t,v*t,t};
}
}
#endif
