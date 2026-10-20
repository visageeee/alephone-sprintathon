#ifndef SPRINTATHON_SKY_BAKE_H
#define SPRINTATHON_SKY_BAKE_H
#include <map>
#include "SprintathonStartupTiming.h"
#include "SprintathonLuaSun.h"
#include <tuple>
#include <vector>
#include <array>
#include <deque>

// Experimental static lightmaps. Geometry is copied once, never edited.
namespace SprintathonSkyBake {
static GLuint batchBoundTexture = 0;
struct V { double x,y,z; V(double X=0,double Y=0,double Z=0):x(X),y(Y),z(Z){} V operator+(V b)const{return V(x+b.x,y+b.y,z+b.z);} V operator*(double s)const{return V(x*s,y*s,z*s);} };
inline double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V unit(V a){return a*(1.0/std::max(1e-12,std::sqrt(dot(a,a))));}
inline bool sky(short mode){return mode==_xfer_landscape || mode==_xfer_big_landscape;}
struct Edge {V a,b,in;int next=-1;bool highSky=false,lowSky=false,panel=false,panelSky=false;};
struct Room {double floor=0,ceiling=0;bool floorSky=false,ceilingSky=false;V center;std::vector<Edge> edges;};
struct Map {bool sunPath=false;int layers=1;double low=0,high=0;GLuint texture=0,textureLight=0;V u,v;double u0=0,v0=0,us=1,vs=1;};
static std::vector<Room> rooms;
static std::map<std::tuple<int,int,int,int>,Map> maps;
struct TextureSource {int room,face;shape_descriptor shape;short light;V position,color;double intensity;
 short object=NONE,type=NONE;int scale=0,transfer=NONE;V anchor;double radius=0;};
static std::vector<TextureSource> textureSources;
static std::vector<bool> bakedSceneryObjects;
static bool textureBakeReady=false,textureBakeValid=false,textureReceiver=false;
static size_t textureBakeBytes=0;
static int textureCheckTick=-1;
static double textureBakeRadius=0;
static int textureBakeGain=0,textureBakeReach=0,sceneryBakeGain=0,sceneryBakeReach=0;
static bool bakedTextures=false,bakedScenery=false;
// The legacy environment override controls both source classes for diagnostics.
inline bool bakeTextureSourcesRequested(){const char* p=std::getenv("SPRINTATHON_TEXTURE_BAKE");return p ? p[0]=='1' : graphics_preferences->baked_texture_lights;}
inline bool bakeScenerySourcesRequested(){const char* p=std::getenv("SPRINTATHON_TEXTURE_BAKE");return p ? p[0]=='1' : graphics_preferences->baked_scenery_lights;}
inline bool textureBakeRequested(){return bakeTextureSourcesRequested() || bakeScenerySourcesRequested();}
inline bool bakeTexturesEnabled(){return bakeTextureSourcesRequested() && graphics_preferences->bright_texture_lights;}
inline bool bakeSceneryEnabled(){return bakeScenerySourcesRequested() && graphics_preferences->bright_scenery_lights;}
inline bool textureBakeActive(){return textureBakeRequested() && textureBakeReady && textureBakeValid &&
 graphics_preferences->projectile_lights_per_pixel &&
 bakeTexturesEnabled()==bakedTextures && bakeSceneryEnabled()==bakedScenery &&
 graphics_preferences->scenery_light_intensity==sceneryBakeGain && graphics_preferences->scenery_light_reach==sceneryBakeReach &&
 graphics_preferences->texture_light_intensity==textureBakeGain && graphics_preferences->texture_light_reach==textureBakeReach;}
inline bool scenerySourceBaked(short index){
 if(!textureBakeActive())return false;
 return index>=0 && size_t(index)<bakedSceneryObjects.size() && bakedSceneryObjects[index];}
inline bool textureSourceBaked(const polygon_data* polygon,int face,shape_descriptor shape,short light){
 if(!textureBakeActive() || !polygon)return false;
 int room=int(polygon-get_polygon_data(0));
 for(const auto& s:textureSources)if(s.room==room && s.face==face && s.shape==shape && s.light==light)return true;
 return false;
}
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
static std::deque<std::tuple<int,int,int,int>> actorSampleOrder;
static const size_t actorSampleLimit=65536;
static double actorSpacing=256.0;
// Match the shaft projection: configured north is world -Y, yaw zero is +X.
static double sunDirectionSetting=0, sunElevationSetting=0;
static bool sunReady=false;
static V sun;
static void (*pathBakeProgress)()=nullptr;
inline void updateSun(){
 if(sunReady && SprintathonLuaSun::state().declared && SprintathonLuaSun::state().locked)return;
 const auto& config=Get_OGL_ConfigureData();
 const double direction=config.LandscapeLightShaftDirection;
 const double elevation=config.LandscapeLightShaftElevation;
 if(sunReady && direction==sunDirectionSetting && elevation==sunElevationSetting)return;
 // Only lighting changes; retain the level's original geometry snapshot.
 for(auto& p:maps){if(p.second.texture)glDeleteTextures(1,&p.second.texture);if(p.second.textureLight)glDeleteTextures(1,&p.second.textureLight);}
 maps.clear();actorSamples.clear();actorSampleOrder.clear();
 sunDirectionSetting=direction;sunElevationSetting=elevation;sunReady=true;
 const double a=(direction-90.0)*0.017453292519943295;
 const double h=elevation*0.017453292519943295;
 sun=V(std::cos(a)*std::cos(h),std::sin(a)*std::cos(h),std::sin(h));
}
inline void reset(){
 if(context==SDL_GL_GetCurrentContext())for(auto& p:maps){if(p.second.texture)glDeleteTextures(1,&p.second.texture);if(p.second.textureLight)glDeleteTextures(1,&p.second.textureLight);}
 textureSources.clear();bakedSceneryObjects.clear();textureBakeReady=textureBakeValid=textureReceiver=false;textureBakeBytes=0;textureCheckTick=-1;
 maps.clear();actorSamples.clear();actorSampleOrder.clear();rooms.clear();actorSpacing=256.0;paletteReady=false;ambientColor=sunColor=V(1,1,1);sunReady=false;checked=available=false;level=lastTick=-1;context=SDL_GL_GetCurrentContext();
}
inline void traceTextureBakeInvalidation(const TextureSource& source,const char* reason) {
 if(!SprintathonStartupTiming::get().active())return;
 fprintf(stderr,"Light bake invalidated frame %llu tick %ld: %s; object=%d room=%d face=%d light=%d; combined bake disabled, sources=%llu\n",
  static_cast<unsigned long long>(SprintathonStartupTiming::get().frame_id),long(dynamic_world->tick_count),
  reason,int(source.object),source.room,source.face,int(source.light),
  static_cast<unsigned long long>(textureSources.size()));
}
inline void prepare(){
 if(!enabled()){if(!rooms.empty())reset();return;}
 if(context!=SDL_GL_GetCurrentContext() || level!=dynamic_world->current_level_number ||
    dynamic_world->tick_count<lastTick || (!rooms.empty() && rooms.size()!=size_t(dynamic_world->polygon_count)))reset();
 lastTick=dynamic_world->tick_count;level=dynamic_world->current_level_number;
 if(textureBakeValid && textureCheckTick!=lastTick){
  textureCheckTick=lastTick;
  for(const auto& s:textureSources){
   if(s.object!=NONE){
    if(s.object<0 || size_t(s.object)>=ObjectList.size()){traceTextureBakeInvalidation(s,"scenery object index invalid");textureBakeValid=false;break;}
    const auto* o=&ObjectList[s.object];short damaged=NONE;
    if(!SLOT_IS_USED(o) || GET_OBJECT_OWNER(o)!=_object_is_scenery ||
       o->polygon!=s.room || o->permutation!=s.type || o->shape!=s.shape ||
       GET_OBJECT_SCALE_FLAGS(o)!=s.scale || OBJECT_IS_INVISIBLE(o) ||
       o->transfer_mode!=s.transfer || get_damaged_scenery_collection(o->permutation,damaged) ||
       o->location.x!=s.anchor.x || o->location.y!=s.anchor.y || o->location.z!=s.anchor.z){traceTextureBakeInvalidation(s,"scenery changed, moved or became invalid");textureBakeValid=false;break;}
    continue;
   }
   const auto* p=get_polygon_data(s.room);bool shapeOK=false;
   if(s.face==0)shapeOK=p->floor_texture==s.shape && p->floor_lightsource_index==s.light;
   else if(s.face==1)shapeOK=p->ceiling_texture==s.shape && p->ceiling_lightsource_index==s.light;
   else if(p->side_indexes[s.face-2]!=NONE){const auto* side=get_side_data(p->side_indexes[s.face-2]);
    shapeOK=(side->primary_texture.texture==s.shape && side->primary_lightsource_index==s.light) ||
      (side->secondary_texture.texture==s.shape && side->secondary_lightsource_index==s.light);}
   if(!shapeOK || s.light<0 || size_t(s.light)>=LightList.size() || get_light_intensity(s.light)!=s.intensity){traceTextureBakeInvalidation(s,"texture or map-light intensity changed");textureBakeValid=false;break;}
  }
 }
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
inline float sectorStrength(){return std::max(0.0f,std::min(1.0f,graphics_preferences->baked_landscape_sector/100.0f));}
inline double ambientFloor(){return std::max(0.0,std::min(1.0,graphics_preferences->baked_landscape_ambient/100.0));}
inline V actorSample(int room,int x,int y,int z){
 auto key=std::make_tuple(room,x,y,z);auto found=actorSamples.find(key);
 if(found!=actorSamples.end())return found->second;
 // Unusual positions or changed geometry evict one entry, never the whole grid.
 if(actorSamples.size()>=actorSampleLimit && !actorSampleOrder.empty()){
  actorSamples.erase(actorSampleOrder.front());actorSampleOrder.pop_front();
 }
 V p=inside(room,V(x*actorSpacing,y*actorSpacing,z*actorSpacing));
 p.z=std::max(rooms[room].floor+0.5,std::min(rooms[room].ceiling-0.5,p.z));
 V value=light(room,p,V(0,0,1));
 actorSamples.emplace(key,value);actorSampleOrder.push_back(key);return value;
}
struct ActorGrid {int lo[3],hi[3];};
inline ActorGrid actorGrid(const Room& room){
 double x0=1e30,y0=1e30,x1=-1e30,y1=-1e30;
 for(const auto& e:room.edges){x0=std::min(x0,e.a.x);x1=std::max(x1,e.a.x);y0=std::min(y0,e.a.y);y1=std::max(y1,e.a.y);}
 ActorGrid grid;
 const double low[3]={x0,y0,room.floor},high[3]={x1,y1,room.ceiling};
 for(int i=0;i<3;++i){grid.lo[i]=int(std::floor(low[i]/actorSpacing));grid.hi[i]=int(std::floor(high[i]/actorSpacing))+1;}
 return grid;
}
inline void preloadActors(void (*progress)()){
 // Coarsen large maps until their complete interpolation grid fits the budget.
 actorSpacing=256.0;
 for(;;){
  double count=0;
  for(const auto& room:rooms)if(!room.edges.empty()){
   ActorGrid g=actorGrid(room);
   count+=double(g.hi[0]-g.lo[0]+1)*(g.hi[1]-g.lo[1]+1)*(g.hi[2]-g.lo[2]+1);
  }
  if(count<=actorSampleLimit || actorSpacing>=65536.0)break;
  actorSpacing*=2.0;
 }
 for(int room=0;room<int(rooms.size());++room){
  if(rooms[room].edges.empty())continue;
  ActorGrid g=actorGrid(rooms[room]);
  for(int z=g.lo[2];z<=g.hi[2];++z)for(int y=g.lo[1];y<=g.hi[1];++y){
   for(int x=g.lo[0];x<=g.hi[0];++x){
    if(actorSamples.size()>=actorSampleLimit)return;
    actorSample(room,x,y,z);
   }
   if(progress)progress();
  }
 }
}
inline V actorLight(int room,V point){
 const double spacing=actorSpacing;
 const int ix=int(std::floor(point.x/spacing)),iy=int(std::floor(point.y/spacing)),iz=int(std::floor(point.z/spacing));
 const double fx=point.x/spacing-ix,fy=point.y/spacing-iy,fz=point.z/spacing-iz;
 V total;
 for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
  V sample=actorSample(room,ix+x,iy+y,iz+z);
  double weight=(x?fx:1-fx)*(y?fy:1-fy)*(z?fz:1-fz);
  total=total+sample*weight;
 }
 const double base=ambientFloor();
 return V(base,base,base)+sunColor*total.x+ambientColor*total.y;
}
inline void bindActor(Shader* shader,int room,V point){
 if(!shader || !enabled() || !available || room<0 || room>=int(rooms.size()))return;
 shader->setFloat(Shader::U_SkyBakeSectorStrength,sectorStrength());
 V c=actorLight(room,point);
 shader->setVector4(Shader::U_SkyBakeActor,float(c.x),float(c.y),float(c.z),1);
}
// Trace a finite segment through the copied polygon portals. Platforms are
// treated as open space: their initial position must not bake permanent doors.
inline bool textureVisible(int room,V from,V to,int targetRoom){
 V delta=to+from*(-1);double t=0;
 for(int hop=0;hop<256;++hop){
  if(room<0 || room>=int(rooms.size()))return false;
  const auto& r=rooms[room];double hit=1.0;int edge=-1;
  const bool platform=get_polygon_data(room)->type==_polygon_is_platform;
  if(!platform){
   if(delta.z>1e-9)hit=std::min(hit,(r.ceiling-from.z)/delta.z);
   else if(delta.z< -1e-9)hit=std::min(hit,(r.floor-from.z)/delta.z);
  }
  if(hit<t-1e-6)return false;
  for(int j=0;j<int(r.edges.size());++j){const auto& e=r.edges[j];double den=dot(delta,e.in);
   if(den>=-1e-9)continue;
   double at=-dot(from+e.a*(-1),e.in)/den;
   if(at>t+1e-7 && at<hit){hit=at;edge=j;}}
  if(hit>=1.0-1e-5)return room==targetRoom;
  if(edge<0)return false;
  const auto& e=r.edges[edge];if(e.next<0 || e.next>=int(rooms.size()) || e.panel)return false;
  const auto& next=rooms[e.next];double z=from.z+delta.z*hit;
  if(get_polygon_data(e.next)->type!=_polygon_is_platform && (z<=next.floor || z>=next.ceiling))return false;
  room=e.next;t=hit;
 }
 return false;
}
inline V textureLightAt(int room,V p,const std::vector<size_t>& candidates){
 V total;
 for(size_t index:candidates){const auto& s=textureSources[index];V d=s.position+p*(-1);double distance2=dot(d,d)/(s.radius*s.radius);
  if(distance2>=1 || !textureVisible(room,p,s.position,s.room))continue;
  double f=1-distance2;total=total+s.color*(f*f/(1+16*distance2));}
 return total;
}
inline void preloadTextureSources(bool (*emission)(shape_descriptor,float*),
 bool (*sceneryEmission)(short,V&,V&),void (*progress)()){
 textureSources.clear();bakedSceneryObjects.clear();textureBakeReady=textureBakeValid=textureReceiver=false;textureBakeBytes=0;textureCheckTick=-1;
 if(!textureBakeRequested() || !enabled() || !available || (!bakeTexturesEnabled() && !bakeSceneryEnabled()) ||
    !graphics_preferences->projectile_lights_per_pixel)return;
 GLint units=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);if(units<7)return;
 textureBakeGain=graphics_preferences->texture_light_intensity;textureBakeReach=graphics_preferences->texture_light_reach;
 textureBakeRadius=(2.0+0.14*textureBakeReach)*WORLD_ONE;
 sceneryBakeGain=graphics_preferences->scenery_light_intensity;sceneryBakeReach=graphics_preferences->scenery_light_reach;
 bakedTextures=bakeTexturesEnabled();bakedScenery=bakeSceneryEnabled();
 std::vector<bool> controlled(LightList.size(),false);
 // Conservatively exclude all panel-referenced IDs, even when the panel could
 // address a platform rather than a light. False exclusions stay dynamic.
 for(int r=0;r<int(rooms.size());++r){const auto* p=get_polygon_data(r);
  for(int j=0;j<p->vertex_count;++j)if(p->side_indexes[j]!=NONE){const auto* side=get_side_data(p->side_indexes[j]);
   if(SIDE_IS_CONTROL_PANEL(side) && side->control_panel_permutation>=0 && size_t(side->control_panel_permutation)<controlled.size())controlled[side->control_panel_permutation]=true;
  }
 }
 auto add=[&](int room,int face,shape_descriptor shape,int mode,short index,double ambient,V position){
  if(!bakedTextures || shape==UNONE || mode!=_xfer_normal || AnimTxtr_IsAnimated(shape) || index<0 || size_t(index)>=LightList.size())return;
  const auto& l=LightList[index].static_data;if(l.tag!=0 || controlled[index])return;
  // Exclude any light with varying active or inactive phases. Runtime changes
  // to a constant light invalidate the experiment, restoring the dynamic path.
  if(LIGHT_IS_STATELESS(&l))return;
  if(LightList[index].state!=_light_primary_active && LightList[index].state!=_light_secondary_active &&
     LightList[index].state!=_light_primary_inactive && LightList[index].state!=_light_secondary_inactive)return;
  const lighting_function_specification* f[]={&l.primary_active,&l.secondary_active,
   &l.primary_inactive,&l.secondary_inactive};
  for(auto p:f)if(p->function!=_constant_lighting_function || p->delta_intensity!=0)return;
  if(l.primary_active.intensity!=l.secondary_active.intensity || l.primary_inactive.intensity!=l.secondary_inactive.intensity)return;
  float rgb[3];if(!emission(shape,rgb))return;
  double intensity=get_light_intensity(index),shade=std::max(0.0,std::min(1.0,(intensity+ambient)/double(FIXED_ONE-1)));
  if(shade<=0)return;
  TextureSource source;source.room=room;source.face=face;source.shape=shape;source.light=index;
  source.position=inside(room,position);source.intensity=intensity;source.radius=textureBakeRadius;
  source.color=V(rgb[0],rgb[1],rgb[2])*(0.8*shade*textureBakeGain/100.0);
  textureSources.push_back(source);
 };
 for(int room=0;room<int(rooms.size());++room){const auto* p=get_polygon_data(room);const auto& r=rooms[room];
  if(p->type==_polygon_is_platform)continue;
  add(room,0,p->floor_texture,p->floor_transfer_mode,p->floor_lightsource_index,0,V(r.center.x,r.center.y,r.floor+2));
  add(room,1,p->ceiling_texture,p->ceiling_transfer_mode,p->ceiling_lightsource_index,0,V(r.center.x,r.center.y,r.ceiling-2));
  for(int j=0;j<int(r.edges.size());++j){if(p->side_indexes[j]==NONE)continue;
   const auto* side=get_side_data(p->side_indexes[j]);if(SIDE_IS_CONTROL_PANEL(side))continue;
   const auto& e=r.edges[j];
   if(e.next>=0 && get_polygon_data(e.next)->type==_polygon_is_platform)continue;
   auto wall=[&](shape_descriptor shape,int mode,short light,double bottom,double top){
    if(top-bottom<1)return;
    V center=(e.a+e.b)*0.5+e.in*2;center.z=(bottom+top)*0.5;
    add(room,2+j,shape,mode,light,side->ambient_delta,center);};
   if(side->type==_full_side)wall(side->primary_texture.texture,side->primary_transfer_mode,side->primary_lightsource_index,r.floor,r.ceiling);
   else if(e.next>=0){const auto& n=rooms[e.next];
    if(side->type==_high_side || side->type==_split_side)wall(side->primary_texture.texture,side->primary_transfer_mode,side->primary_lightsource_index,std::max(r.floor,n.ceiling),r.ceiling);
    if(side->type==_low_side)wall(side->primary_texture.texture,side->primary_transfer_mode,side->primary_lightsource_index,r.floor,std::min(r.ceiling,n.floor));
    if(side->type==_split_side)wall(side->secondary_texture.texture,side->secondary_transfer_mode,side->secondary_lightsource_index,r.floor,std::min(r.ceiling,n.floor));
   }
  }
  if(progress)progress();
 }
 // Scenery is discovered during loading, independently of camera visibility.
 if(bakedScenery && sceneryEmission)for(size_t i=0;i<ObjectList.size() && i<32768;++i){
  const auto* o=&ObjectList[i];
  if(!SLOT_IS_USED(o) || GET_OBJECT_OWNER(o)!=_object_is_scenery || o->polygon<0 ||
     size_t(o->polygon)>=rooms.size() || get_polygon_data(o->polygon)->type==_polygon_is_platform)continue;
  V position,color;if(!sceneryEmission(short(i),position,color))continue;
  TextureSource source;source.room=o->polygon;source.face=-1;source.shape=o->shape;source.light=NONE;
  source.object=short(i);source.type=o->permutation;source.scale=GET_OBJECT_SCALE_FLAGS(o);source.transfer=o->transfer_mode;
  source.anchor=V(o->location.x,o->location.y,o->location.z);source.intensity=0;
  source.position=inside(source.room,position);source.color=color*(0.8*sceneryBakeGain/100.0);
  source.radius=(2.0+0.14*sceneryBakeReach)*WORLD_ONE;
  textureSources.push_back(source);if(progress)progress();
 }
 // Never truncate sources: incomplete bakes must not suppress dynamic light.
 if(textureSources.size()>2048){fprintf(stderr,"Texture bake: source budget exceeded; using dynamic lights.\n");textureSources.clear();}
 bakedSceneryObjects.assign(ObjectList.size(),false);
 for(const auto& source:textureSources)if(source.object>=0)bakedSceneryObjects[source.object]=true;
 textureBakeReady=textureBakeValid=!textureSources.empty();
 fprintf(stderr,"Texture bake: %lu constant sources; static receivers only.\n",static_cast<unsigned long>(textureSources.size()));
}
inline void bind(Shader* shader,const polygon_data* polygon,int face,V origin,V u,V v,V normal,double width,double height){
 if(shader){textureReceiver=false;shader->setFloat(Shader::U_TextureBakeEnabled,0);shader->setFloat(Shader::U_SkyBakeEnabled,0);shader->setVector4(Shader::U_SkyBakeLayers,0,0,0,1);}
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
 // Media stores a fixed stack of height slices in one atlas.
 const bool mediaFace=face==16 || face==17;

 auto key=std::make_tuple(room,face,mediaFace?0:int(std::lround(origin.z)),int(std::lround(height)));
 auto it=maps.find(key);
 if(it==maps.end()){
  // Missing media atlases must never trigger a gameplay bake.
  if(shader && (mediaFace || SprintathonLuaSun::state().declared || textureBakeReady))return;
  if(maps.size()>=16384)return; // Bounded experimental cache.
  Map m;m.u=u;m.v=v;
  if(mediaFace){m.layers=8;m.low=rooms[room].floor+0.5;m.high=std::max(m.low,rooms[room].ceiling-0.5);}
  else if(SprintathonLuaSun::state().declared && SprintathonLuaSun::state().locked){m.layers=8;m.sunPath=true;}
  int w=std::max(2,std::min(128,int(std::ceil(width/128.0))+1));int h=std::max(2,std::min(128,int(std::ceil(height/128.0))+1));
  m.us=(w-1.0)/(width*w);m.vs=(h-1.0)/(height*h);m.u0=0.5/w-dot(origin,u)*m.us;m.v0=0.5/h-dot(origin,v)*m.vs;
  std::vector<unsigned char> atlas(size_t(w)*h*3*m.layers);
  const V configuredSun=sun;
  for(int layer=0;layer<m.layers;++layer){
   if(m.sunPath){
    const auto& point=SprintathonLuaSun::state().path[layer];
    const double a=(point.azimuth-90.0)*0.017453292519943295;
    const double e=point.elevation*0.017453292519943295;
    sun=V(std::cos(a)*std::cos(e),std::sin(a)*std::cos(e),std::sin(e));
   }
   if(mediaFace)origin.z=m.low+(m.high-m.low)*layer/(m.layers-1);
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
   std::copy(pixels.begin(),pixels.end(),atlas.begin()+size_t(layer)*w*h*3);
   if(pathBakeProgress)pathBakeProgress();
  }
  sun=configuredSun;
  std::vector<unsigned char> texturePixels;
  std::vector<size_t> candidates;
  if(textureBakeActive() && !mediaFace && textureBakeBytes+size_t(w)*h*3<=64*1024*1024){
   for(size_t i=0;i<textureSources.size();++i){
    V delta=textureSources[i].position+origin*(-1);
    V nearest=origin+u*std::max(0.0,std::min(width,dot(delta,u)))+v*std::max(0.0,std::min(height,dot(delta,v)));
    V d=textureSources[i].position+nearest*(-1);
    if(dot(d,d)<textureSources[i].radius*textureSources[i].radius)candidates.push_back(i);
   }
  }
  if(!candidates.empty()){
   textureBakeBytes+=size_t(w)*h*3;
   texturePixels.resize(size_t(w)*h*3);
   for(int y=0;y<h;++y){for(int x=0;x<w;++x){
    V p=inside(room,origin+u*(width*x/(w-1))+v*(height*y/(h-1))+normal*2);
    p.z=std::max(rooms[room].floor+1.0,std::min(rooms[room].ceiling-1.0,p.z));
    V c=textureLightAt(room,p,candidates);
    texturePixels[3*(y*w+x)]=static_cast<unsigned char>(std::lround(std::min(1.0,c.x)*255));
    texturePixels[3*(y*w+x)+1]=static_cast<unsigned char>(std::lround(std::min(1.0,c.y)*255));
    texturePixels[3*(y*w+x)+2]=static_cast<unsigned char>(std::lround(std::min(1.0,c.z)*255));
   }if(pathBakeProgress)pathBakeProgress();}
  }
  GLint active=0,alignment=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glActiveTextureARB(GL_TEXTURE0_ARB+4);glGenTextures(1,&m.texture);glBindTexture(GL_TEXTURE_2D,m.texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,w,h*m.layers,0,GL_RGB,GL_UNSIGNED_BYTE,atlas.data());
  if(!texturePixels.empty()){
   glActiveTextureARB(GL_TEXTURE0_ARB+6);glGenTextures(1,&m.textureLight);glBindTexture(GL_TEXTURE_2D,m.textureLight);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,texturePixels.data());
  }glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glActiveTextureARB(active);
  it=maps.emplace(key,m).first;
 }
 if(!shader)return; // Loading creates textures without binding a draw shader.
 shader->setFloat(Shader::U_SkyBakeSectorStrength,sectorStrength());
 shader->setVector4(Shader::U_SkyBakeSun,float(sunColor.x),float(sunColor.y),float(sunColor.z),0);
 shader->setVector4(Shader::U_SkyBakeAmbient,float(ambientColor.x),float(ambientColor.y),float(ambientColor.z),float(ambientFloor()));
 const auto& mediaMap=it->second;
 if(mediaMap.sunPath){
  const auto blend=SprintathonLuaSun::pathBlend();
  shader->setVector4(Shader::U_SkyBakeLayers,float(blend.lo),float(blend.hi),float(blend.fraction),1.0f/8.0f);
 }else if(mediaMap.layers>1){
  double z=std::max(0.0,std::min(double(mediaMap.layers-1),
      (origin.z-mediaMap.low)/std::max(0.001,mediaMap.high-mediaMap.low)*(mediaMap.layers-1)));
  int lo=int(std::floor(z)),hi=std::min(lo+1,mediaMap.layers-1);
  shader->setVector4(Shader::U_SkyBakeLayers,float(lo),float(hi),float(z-lo),1.0f/mediaMap.layers);
 }
 const auto& m=it->second;batchBoundTexture=m.texture;GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&active);glActiveTextureARB(GL_TEXTURE0_ARB+4);glBindTexture(GL_TEXTURE_2D,m.texture);
 if(textureBakeActive() && m.textureLight){textureReceiver=true;glActiveTextureARB(GL_TEXTURE0_ARB+6);glBindTexture(GL_TEXTURE_2D,m.textureLight);shader->setFloat(Shader::U_TextureBakeEnabled,1);}
 glActiveTextureARB(active);
 shader->setVector4(Shader::U_SkyBakeU,float(m.u.x*m.us),float(m.u.y*m.us),float(m.u.z*m.us),float(m.u0));
 shader->setVector4(Shader::U_SkyBakeV,float(m.v.x*m.vs),float(m.v.y*m.vs),float(m.v.z*m.vs),float(m.v0));shader->setFloat(Shader::U_SkyBakeEnabled,1);
}
inline void preload(void (*progress)()){
 prepare();
 if(!enabled() || !available)return;
 pathBakeProgress=progress;
 for(int room=0;room<int(rooms.size());++room){
  const auto* polygon=get_polygon_data(room);const auto& bounds=rooms[room];
  if(polygon->type!=_polygon_is_platform && !bounds.edges.empty()){
   double x0=1e30,y0=1e30,x1=-1e30,y1=-1e30;
   for(const auto& edge:bounds.edges){x0=std::min(x0,edge.a.x);x1=std::max(x1,edge.a.x);y0=std::min(y0,edge.a.y);y1=std::max(y1,edge.a.y);}
   if(!bounds.floorSky)bind(nullptr,polygon,0,V(x0,y0,bounds.floor),V(1,0,0),V(0,1,0),V(0,0,1),x1-x0,y1-y0);
   if(progress)progress();
   if(!bounds.ceilingSky)bind(nullptr,polygon,1,V(x0,y0,bounds.ceiling),V(1,0,0),V(0,1,0),V(0,0,-1),x1-x0,y1-y0);
   if(polygon->media_index!=NONE){
    const auto* media=get_media_data(polygon->media_index);
    if(media && media->type!=_media_lava && bounds.ceiling>bounds.floor){
     bind(nullptr,polygon,16,V(x0,y0,media->height),V(1,0,0),V(0,1,0),V(0,0,1),x1-x0,y1-y0);
     if(progress)progress();
     bind(nullptr,polygon,17,V(x0,y0,media->height),V(1,0,0),V(0,1,0),V(0,0,-1),x1-x0,y1-y0);
     if(progress)progress();
    }
   }
   for(int edge=0;edge<int(bounds.edges.size());++edge){
    // Even currently hidden sides are prepared for doors opening later.
    if(polygon->side_indexes[edge]!=NONE)bind(nullptr,polygon,2+edge,V(),V(),V(),V(),1,1);
    if(progress)progress();
   }
  }
  if(progress)progress();
 }
 pathBakeProgress=nullptr;
 preloadActors(progress);
 fprintf(stderr,"Sky bake: preloaded %lu actor samples (spacing %.0f).\n",static_cast<unsigned long>(actorSamples.size()),actorSpacing);
 fprintf(stderr,"Sky bake: preloaded %lu static surface lightmaps.\n",static_cast<unsigned long>(maps.size()));
}

}
#endif
