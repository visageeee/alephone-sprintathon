#ifndef SPRINTATHON_ZERO_G_MATH_H
#define SPRINTATHON_ZERO_G_MATH_H
#include <algorithm>
#include <cmath>
namespace SprintathonZeroGMath {
constexpr double pi=3.14159265358979323846;
struct V {double x=0,y=0,z=0;
 V operator+(V b)const{return {x+b.x,y+b.y,z+b.z};}
 V operator-(V b)const{return {x-b.x,y-b.y,z-b.z};}
 V operator*(double s)const{return {x*s,y*s,z*s};}
 double dot(V b)const{return x*b.x+y*b.y+z*b.z;}
 V cross(V b)const{return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
 double length()const{return std::sqrt(dot(*this));}
};
struct Q {double w=1,x=0,y=0,z=0;
 Q operator*(Q b)const{return {w*b.w-x*b.x-y*b.y-z*b.z,w*b.x+x*b.w+y*b.z-z*b.y,
 w*b.y-x*b.z+y*b.w+z*b.x,w*b.z+x*b.y-y*b.x+z*b.w};}
 Q normalized()const{double n=std::sqrt(w*w+x*x+y*y+z*z);return {w/n,x/n,y/n,z/n};}
 V rotate(V p)const{V v{x,y,z};V t=v.cross(p)*2;return p+t*w+v.cross(t);}
};
inline Q axis(V v,double a){double s=std::sin(a/2);return {std::cos(a/2),v.x*s,v.y*s,v.z*s};}
inline Q fromAngles(double yaw,double pitch,double roll){return axis({0,0,1},yaw)*axis({0,1,0},-pitch)*axis({1,0,0},-roll);}
inline Q turn(Q q,double yaw,double pitch,double roll){return (q*fromAngles(yaw,pitch,roll)).normalized();}
inline Q blend(Q a,Q b,double t){
 t=std::max(0.,std::min(1.,t));double d=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;
 if(d<0){b={-b.w,-b.x,-b.y,-b.z};d=-d;}
 double wa=1-t,wb=t;
 if(d<.9995){double theta=std::acos(std::min(1.,d)),s=std::sin(theta);wa=std::sin((1-t)*theta)/s;wb=std::sin(t*theta)/s;}
 return Q{wa*a.w+wb*b.w,wa*a.x+wb*b.x,wa*a.y+wb*b.y,wa*a.z+wb*b.z}.normalized();
}
inline V angles(Q q){
 V f=q.rotate({1,0,0}),up=q.rotate({0,0,1});
 // At a pole yaw is arbitrary; choosing zero and deriving roll from up
 // still reproduces the complete orientation without a singular state.
 double yaw=std::hypot(f.x,f.y)>1e-9?std::atan2(f.y,f.x):0;
 double pitch=std::atan2(f.z,std::hypot(f.x,f.y));
 V r{-std::sin(yaw),std::cos(yaw),0};
 V u{-std::sin(pitch)*std::cos(yaw),-std::sin(pitch)*std::sin(yaw),std::cos(pitch)};
 return {yaw,pitch,std::atan2(up.dot(r),up.dot(u))};
}
inline V advanceVelocity(V velocity,Q q,V thrust,double dt,bool brake){
 double n=thrust.length();if(n>1)thrust=thrust*(1/n);
 velocity=velocity+q.rotate(thrust)*(.007*dt);
 if(brake)velocity=velocity*std::exp(-.3*dt);
 double speed=velocity.length();if(speed>.35)velocity=velocity*(.35/speed);
 if(brake && velocity.length()<.0001)velocity={};
 return velocity;
}
inline V collide(V velocity,V correction){
 double n=correction.length();if(n<1e-12)return velocity;
 V normal=correction*(1/n);double inward=velocity.dot(normal);
 return inward<0?velocity-normal*inward:velocity;
}
}
#endif
