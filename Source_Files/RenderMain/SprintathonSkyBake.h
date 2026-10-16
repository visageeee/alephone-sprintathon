#ifndef SPRINTATHON_SKY_BAKE_H
#define SPRINTATHON_SKY_BAKE_H
#include <map>
#include <tuple>
#include <vector>
#include <array>

// Experimental static lightmaps. Geometry is copied once, never edited.
namespace SprintathonSkyBake {
struct V { double x,y,z; V(double X=0,double Y=0,double Z=0):x(X),y(Y),z(Z){} V operator+(V b)const{return V(x+b.x,y+b.y,z+b.z);} V operator*(double s)const{return V(x*s,y*s,z*s);} };
inline double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V unit(V a){return a*(1.0/std::max(1e-12,std::sqrt(dot(a,a))));}
inline bool sky(short mode){return mode==_xfer_landscape || mode==_xfer_big_landscape;}
struct Edge {V a,b,in;int next=-1;bool highSky=false,lowSky=false,panel=false,panelSky=false;};
struct Room {double floor=0,ceiling=0;bool floorSky=false,ceilingSky=false;V center;std::vector<Edge> edges;};
struct Map {GLuint texture=0;V u,v;double u0=0,v0=0,us=1,vs=1;};
static std::vector<Room> rooms;
static std::map<std::tuple<int,int,int,int>,Map> maps;
static SDL_GLContext context=nullptr;
static int level=-1,lastTick=-1;
static bool checked=false,available=false;
static bool paletteReady=false;
static V ambientColor(1,1,1), sunColor(1,1,1);
inline void landscapeColor(const float ambient[3],const float direct[3]){
 // First landscape establishes a stable level palette, independent of camera turns.
 if(paletteReady)return;
 ambientColor=V(ambient[0],ambient[1],ambient[2]);sunColor=V(direct[0],direct[1],direct[2]);paletteReady=true;
}
inline bool enabled(){const char* p=std::getenv("SPRINTATHON_SKY_BAKE");return p ? p[0]=='1' : graphics_preferences->baked_landscape_lighting;}
static std::map<std::tuple<int,int,int,int>,V> actorSamples;
// Match the shaft projection: configured north is world -Y, yaw zero is +X.
static double sunDirectionSetting=0, sunElevationSetting=0;
static bool sunReady=false;
static V sun;
inline void updateSun(){
 const auto& config=Get_OGL_ConfigureData();
 const double direction=config.LandscapeLightShaftDirection;
 const double elevation=config.LandscapeLightShaftElevation;
 if(sunReady && direction==sunDirectionSetting && elevation==sunElevationSetting)return;
 // Only lighting changes; retain the level's original geometry snapshot.
 for(auto& p:maps)if(p.second.texture)glDeleteTextures(1,&p.second.texture);
 maps.clear();actorSamples.clear();
 sunDirectionSetting=direction;sunElevationSetting=elevation;sunReady=true;
 const double a=(direction-90.0)*0.017453292519943295;
 const double h=elevation*0.017453292519943295;
 sun=V(std::cos(a)*std::cos(h),std::sin(a)*std::cos(h),std::sin(h));
}
inline void reset(){
 if(context==SDL_GL_GetCurrentContext())for(auto& p:maps)if(p.second.texture)glDeleteTextures(1,&p.second.texture);
 maps.clear();actorSamples.clear();rooms.clear();paletteReady=false;ambientColor=sunColor=V(1,1,1);sunReady=false;checked=available=false;level=lastTick=-1;context=SDL_GL_GetCurrentContext();
}
inline void prepare(){
 if(!enabled()){if(!rooms.empty())reset();return;}
 if(context!=SDL_GL_GetCurrentContext() || level!=dynamic_world->current_level_number ||
    dynamic_world->tick_count<lastTick || (!rooms.empty() && rooms.size()!=size_t(dynamic_world->polygon_count)))reset();
 lastTick=dynamic_world->tick_count;level=dynamic_world->current_level_number;
 if(!checked){GLint n=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&n);available=n>=5;checked=true;
  if(!available)fprintf(stderr,"Sky bake: requires five fragment texture units; disabled\n");}
 if(!available)return;
 updateSun();
 if(!rooms.empty())return;
 rooms.resize(dynamic_world->polygon_count);
 for(int i=0;i<dynamic_world->polygon_count;++i){const auto* p=get_polygon_data(i);auto& r=rooms[i];
  r.floor=p->floor_height;r.ceiling=p->ceiling_height;r.floorSky=sky(p->floor_transfer_mode);r.ceilingSky=sky(p->ceiling_transfer_mode);
  for(int j=0;j<p->vertex_count;++j){const auto& a=get_endpoint_data(p->endpoint_indexes[j])->vertex;r.center=r.center+V(a.x,a.y,0);}
  r.center=r.center*(1.0/std::max(1,int(p->vertex_count)));
  for(int j=0;j<p->vertex_count;++j){Edge e;const auto& a=get_endpoint_data(p->endpoint_indexes[j])->vertex;const auto& b=get_endpoint_data(p->endpoint_indexes[(j+1)%p->vertex_count])->vertex;
   e.a=V(a.x,a.y,0);e.b=V(b.x,b.y,0);e.in=unit(V(-(b.y-a.y),b.x-a.x,0));if(dot(r.center+e.a*(-1),e.in)<0)e.in=e.in*(-1);
   e.next=p->adjacent_polygon_indexes[j];
   if(p->side_indexes[j]!=NONE){const auto* side=get_side_data(p->side_indexes[j]);e.highSky=sky(side->primary_transfer_mode);e.lowSky=sky(side->type==_split_side?side->secondary_transfer_mode:side->primary_transfer_mode);
    e.panel=side->transparent_texture.texture!=UNONE;e.panelSky=sky(side->transparent_transfer_mode);}
   r.edges.push_back(e);
  }
 }
 fprintf(stderr,"Sky bake: static geometry captured; preparing lightmaps. Restart level to rebake.\n");
}
inline bool visible(int room,V origin,V direction){
 double t=0;
 for(int hop=0;hop<256;++hop){if(room<0 || room>=int(rooms.size()))return false;const auto& r=rooms[room];
  double hit=1e30;int edge=-1;bool ceiling=false;
  if(direction.z>1e-9){hit=(r.ceiling-origin.z)/direction.z;ceiling=true;}
  else if(direction.z< -1e-9)hit=(r.floor-origin.z)/direction.z;
  if(hit<t-0.1)return false;
  for(int j=0;j<int(r.edges.size());++j){const auto& e=r.edges[j];double den=dot(direction,e.in);if(den>=-1e-9)continue;
   double at=-dot(origin+e.a*(-1),e.in)/den;
   if(at>t+1e-5 && at<hit){hit=at;edge=j;}
  }
  if(hit>1e29)return false;
  if(edge<0)return ceiling?r.ceilingSky:r.floorSky;
  const auto& e=r.edges[edge];double z=origin.z+direction.z*hit;
  if(e.next<0 || e.next>=int(rooms.size()))return e.highSky;
  const auto& next=rooms[e.next];
  if(z>=next.ceiling)return e.highSky;
  if(z<=next.floor)return e.lowSky;
  if(e.panel)return e.panelSky; // First prototype treats masked panels as solid.
  room=e.next;t=hit;
 }
 return false;
}
inline V inside(int room,V p){const auto& r=rooms[room];
 for(int repeat=0;repeat<4;++repeat)for(const auto& e:r.edges){double d=dot(p+e.a*(-1),e.in);if(d<0.5)p=p+e.in*(0.5-d);}
 return p;
}
inline V light(int room,V p,V normal){

 double direct=std::max(0.0,dot(normal,sun));if(direct>0 && !visible(room,p,sun))direct=0;
 double ambient=0;
 // Fixed hemisphere samples make the bake deterministic and world-anchored.
 for(int i=0;i<12;++i){double z=(i+0.5)/12.0,a=i*2.39996322972865,r=std::sqrt(1-z*z);V d(r*std::cos(a),r*std::sin(a),z);double w=std::max(0.0,dot(normal,d));if(w>0 && visible(room,p,d))ambient+=w/6.0;}
 return V(std::min(1.0,0.80*direct),std::min(1.0,0.30*ambient),0);
}
// Cache a world-space irradiance grid per polygon. Polygon IDs preserve
// overlapping map topology; trilinear interpolation softens moving shadows.
inline double ambientFloor(){return std::max(0.0,std::min(1.0,graphics_preferences->baked_landscape_ambient/100.0));}
inline V actorLight(int room,V point){
 const double spacing=256.0;
 const int ix=int(std::floor(point.x/spacing)),iy=int(std::floor(point.y/spacing)),iz=int(std::floor(point.z/spacing));
 const double fx=point.x/spacing-ix,fy=point.y/spacing-iy,fz=point.z/spacing-iz;
 V total;
 for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
  auto key=std::make_tuple(room,ix+x,iy+y,iz+z);auto found=actorSamples.find(key);
  if(found==actorSamples.end()){
   if(actorSamples.size()>=8192)actorSamples.clear();
   V p=inside(room,V((ix+x)*spacing,(iy+y)*spacing,(iz+z)*spacing));
   p.z=std::max(rooms[room].floor+0.5,std::min(rooms[room].ceiling-0.5,p.z));
   found=actorSamples.emplace(key,light(room,p,V(0,0,1))).first;
  }
  double weight=(x?fx:1-fx)*(y?fy:1-fy)*(z?fz:1-fz);
  total=total+found->second*weight;
 }
 const double base=ambientFloor();
 return V(base,base,base)+sunColor*total.x+ambientColor*total.y;
}
inline void bindActor(Shader* shader,int room,V point){
 if(!shader || !enabled() || !available || room<0 || room>=int(rooms.size()))return;
 V c=actorLight(room,point);
 shader->setVector4(Shader::U_SkyBakeActor,float(c.x),float(c.y),float(c.z),1);
}
inline void bind(Shader* shader,const polygon_data* polygon,int face,V origin,V u,V v,V normal,double width,double height){
 if(shader)shader->setFloat(Shader::U_SkyBakeEnabled,0);
 if(!enabled() || !available || !polygon || polygon->type==_polygon_is_platform || width<1 || height<1)return;
 int room=int(polygon-get_polygon_data(0));if(room<0 || room>=int(rooms.size()))return;
 // Use one full-height lightmap per wall edge. Rendering may split or clip
 // that wall differently, but must address the same preloaded lightmap.
 if(face>=2 && face<16){
  int edge=face-2;if(edge>=int(rooms[room].edges.size()))return;
  const auto& e=rooms[room].edges[edge];
  origin=V(e.a.x,e.a.y,rooms[room].floor);
  V delta=e.b+e.a*(-1);width=std::sqrt(dot(delta,delta));
  height=rooms[room].ceiling-rooms[room].floor;
  u=unit(delta);v=V(0,0,1);normal=e.in;
  if(width<1 || height<1)return;
 }
 // Height remains part of the key for moving media.

 auto key=std::make_tuple(room,face,int(std::lround(origin.z)),int(std::lround(height)));
 auto it=maps.find(key);
 if(it==maps.end()){
  // Media uses separate faces (16/17). Replace its old height's bake when
  // liquid rises or falls, instead of accumulating textures every tick.
  if(face==16 || face==17)for(auto old=maps.begin();old!=maps.end();){
   if(std::get<0>(old->first)==room && std::get<1>(old->first)==face){
    if(old->second.texture)glDeleteTextures(1,&old->second.texture);
    old=maps.erase(old);
   }else ++old;
  }
  if(maps.size()>=16384)return; // Bounded experimental cache.
  Map m;m.u=u;m.v=v;
  int w=std::max(2,std::min(128,int(std::ceil(width/128.0))+1));int h=std::max(2,std::min(128,int(std::ceil(height/128.0))+1));
  m.us=(w-1.0)/(width*w);m.vs=(h-1.0)/(height*h);m.u0=0.5/w-dot(origin,u)*m.us;m.v0=0.5/h-dot(origin,v)*m.vs;
  std::vector<unsigned char> pixels(size_t(w)*h*3);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){V p=origin+u*(width*x/(w-1))+v*(height*y/(h-1));p=inside(room,p+normal*1.0);p.z=std::max(rooms[room].floor+0.5,std::min(rooms[room].ceiling-0.5,p.z));V c=light(room,p,normal);pixels[3*(y*w+x)]=static_cast<unsigned char>(std::lround(c.x*255));pixels[3*(y*w+x)+1]=static_cast<unsigned char>(std::lround(c.y*255));pixels[3*(y*w+x)+2]=0;}
  // Refine only direct-sun discontinuities. Coverage samples stay on the
  // receiver plane, preserving the configured sun direction and shadow shape.
  // Read the original grid so refinement does not propagate across the surface.
  const auto original=pixels;
  const double sunlight=0.80*std::max(0.0,dot(normal,sun));
  if(sunlight>0)for(int y=0;y<h;++y)for(int x=0;x<w;++x){
   const int center=original[3*(y*w+x)];bool edge=false;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
    int nx=x+dx,ny=y+dy;
    if(nx>=0 && nx<w && ny>=0 && ny<h &&
       std::abs(int(original[3*(ny*w+nx)])-center)>1)edge=true;
   }
   if(!edge)continue;
   double coverage=0;
   for(int sy=-1;sy<=1;++sy)for(int sx=-1;sx<=1;++sx){
    double px=std::max(0.0,std::min(width,width*(x+sx/3.0)/(w-1)));
    double py=std::max(0.0,std::min(height,height*(y+sy/3.0)/(h-1)));
    V sample=inside(room,origin+u*px+v*py+normal*1.0);
    sample.z=std::max(rooms[room].floor+0.5,std::min(rooms[room].ceiling-0.5,sample.z));
    if(visible(room,sample,sun))coverage+=1.0/9.0;
   }
   pixels[3*(y*w+x)]=static_cast<unsigned char>(std::lround(sunlight*coverage*255));
  }
  GLint active=0,alignment=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glActiveTextureARB(GL_TEXTURE0_ARB+4);glGenTextures(1,&m.texture);glBindTexture(GL_TEXTURE_2D,m.texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glActiveTextureARB(active);
  it=maps.emplace(key,m).first;
 }
 if(!shader)return; // Loading creates textures without binding a draw shader.
 shader->setVector4(Shader::U_SkyBakeSun,float(sunColor.x),float(sunColor.y),float(sunColor.z),0);
 shader->setVector4(Shader::U_SkyBakeAmbient,float(ambientColor.x),float(ambientColor.y),float(ambientColor.z),float(ambientFloor()));
 const auto& m=it->second;GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);glActiveTextureARB(GL_TEXTURE0_ARB+4);glBindTexture(GL_TEXTURE_2D,m.texture);glActiveTextureARB(active);
 shader->setVector4(Shader::U_SkyBakeU,float(m.u.x*m.us),float(m.u.y*m.us),float(m.u.z*m.us),float(m.u0));
 shader->setVector4(Shader::U_SkyBakeV,float(m.v.x*m.vs),float(m.v.y*m.vs),float(m.v.z*m.vs),float(m.v0));shader->setFloat(Shader::U_SkyBakeEnabled,1);
}
inline void preload(void (*progress)()){
 reset();prepare();
 if(!enabled() || !available)return;
 for(int room=0;room<int(rooms.size());++room){
  const auto* polygon=get_polygon_data(room);const auto& bounds=rooms[room];
  if(polygon->type!=_polygon_is_platform && !bounds.edges.empty()){
   double x0=1e30,y0=1e30,x1=-1e30,y1=-1e30;
   for(const auto& edge:bounds.edges){x0=std::min(x0,edge.a.x);x1=std::max(x1,edge.a.x);y0=std::min(y0,edge.a.y);y1=std::max(y1,edge.a.y);}
   if(!bounds.floorSky)bind(nullptr,polygon,0,V(x0,y0,bounds.floor),V(1,0,0),V(0,1,0),V(0,0,1),x1-x0,y1-y0);
   if(progress)progress();
   if(!bounds.ceilingSky)bind(nullptr,polygon,1,V(x0,y0,bounds.ceiling),V(1,0,0),V(0,1,0),V(0,0,-1),x1-x0,y1-y0);
   for(int edge=0;edge<int(bounds.edges.size());++edge){
    // Even currently hidden sides are prepared for doors opening later.
    if(polygon->side_indexes[edge]!=NONE)bind(nullptr,polygon,2+edge,V(),V(),V(),V(),1,1);
    if(progress)progress();
   }
  }
  if(progress)progress();
 }
 fprintf(stderr,"Sky bake: preloaded %lu static surface lightmaps.\n",static_cast<unsigned long>(maps.size()));
}

}
#endif
