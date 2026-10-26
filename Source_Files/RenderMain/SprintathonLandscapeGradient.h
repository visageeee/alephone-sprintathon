#ifndef SPRINTATHON_LANDSCAPE_GRADIENT_H
#define SPRINTATHON_LANDSCAPE_GRADIENT_H
#include <algorithm>
#include <cmath>
#include <vector>
namespace SprintathonLandscapeGradient {
constexpr double pi=3.14159265358979323846;
struct RGB {double r=0,g=0,b=0;
 RGB operator*(double x)const{return {r*x,g*x,b*x};}
 RGB operator+(RGB c)const{return {r+c.r,g+c.g,b+c.b};}
};
inline RGB mix(RGB a,RGB b,double t){return a*(1-t)+b*t;}
struct Panorama {
 int width=0,height=0;std::vector<unsigned char> pixels;
 double repeats=1,azimuth=0,centre=.5,slope=-1;
 RGB lowMean,highMean;
 RGB texel(int x,int y)const{
  x=(x%width+width)%width;y=std::max(0,std::min(height-1,y));
  const size_t i=(size_t(y)*width+x)*4;
  return {pixels[i]/255.,pixels[i+1]/255.,pixels[i+2]/255.};
 }
 RGB sample(double u,double v)const{
  const double x=(u-std::floor(u))*width-.5,y=std::max(0.,std::min(1.,v))*height-.5;
  const int ix=int(std::floor(x)),iy=int(std::floor(y));
  return mix(mix(texel(ix,iy),texel(ix+1,iy),x-ix),
             mix(texel(ix,iy+1),texel(ix+1,iy+1),x-ix),y-iy);
 }
 void edgeMeans(){
  lowMean={};highMean={};
  for(int x=0;x<width;++x){lowMean=lowMean+texel(x,0);highMean=highMean+texel(x,height-1);}
  lowMean=lowMean*(1./width);highMean=highMean*(1./width);
 }
 RGB direction(double x,double y,double z)const{
  const double length=std::sqrt(x*x+y*y+z*z);
  const double elevation=std::asin(std::max(-1.,std::min(1.,z/length)));
  const double u=repeats*(std::atan2(y,x)/(2*pi)+azimuth);
  const double v=centre+slope*elevation;
  // Remove directional detail before it can pinch around either pole,
  // including panoramas whose original mapping reaches the zenith/nadir.
  const auto smooth=[](double t){t=std::max(0.,std::min(1.,t));return t*t*(3-2*t);};
  const double latitude=std::abs(elevation);
  double blend=smooth((latitude-35*pi/180)/(30*pi/180));
  if((v<0 || v>1) && std::abs(slope)>1e-9){
   const double edge=v<0?0:1;
   const double edgeElevation=(edge-centre)/slope;
   // Do not stretch individual edge columns across the whole polar cap.
   blend=std::max(blend,smooth(std::abs(elevation-edgeElevation)/(15*pi/180)));
  }
  const bool lowEdge=(elevation>=0)==(slope<0);
  const RGB mean=lowEdge?lowMean:highMean;
  const double poleShade=elevation>=0?.86:.76;
  const double shade=1+(poleShade-1)*smooth(latitude/(pi/2));
  return mix(sample(u,std::max(0.,std::min(1.,v))),mean*shade,blend);
 }
};
// Cube axes are right/up/front = world Y/Z/X; rows match the BMP prototype.
inline RGB cubePixel(const Panorama& p,int face,double s,double t){
 double x=0,y=0,z=0;
 switch(face){
 case 0:x=1;y=-t;z=-s;break;case 1:x=-1;y=-t;z=s;break;
 case 2:x=s;y=1;z=t;break;case 3:x=s;y=-1;z=-t;break;
 case 4:x=s;y=-t;z=1;break;default:x=-s;y=-t;z=-1;break;
 }
 return p.direction(z,x,y);
}
}
#endif
