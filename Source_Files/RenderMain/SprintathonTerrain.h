#ifndef SPRINTATHON_TERRAIN_H
#define SPRINTATHON_TERRAIN_H
// Experimental OBJ heightfield reader and CPU mesh preparation. No GL ownership.
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace SprintathonTerrain {
struct Point { double x,y,z,u,v; };
struct Mesh {
    std::vector<Point> points;
    std::vector<std::array<size_t,3>> faces;
};
struct DrawMesh {
    std::vector<float> positions,uv,normals,tangents;
    float minZ=0,maxZ=0;
};
inline void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
inline size_t index(const std::string& s,size_t count) {
    require(!s.empty() && s.find_first_not_of("0123456789")==std::string::npos,"OBJ requires positive indices");
    size_t used=0; unsigned long n=std::stoul(s,&used);
    require(used==s.size() && n>0 && n<=count,"OBJ index outside array");return size_t(n-1);
}
inline Mesh read(std::istream& in) {
    Mesh m;std::vector<std::array<double,2>> uv;
    // Faces resolve UVs into separate vertices, supporting OBJ UV seams.
    std::vector<Point> source;std::string line;size_t bytes=0;
    while(std::getline(in,line)) {
        bytes+=line.size()+1;require(bytes<=16*1024*1024 && line.size()<65536,"OBJ exceeds input budget");
        std::istringstream row(line);row.imbue(std::locale::classic());std::string kind;row>>kind;
        if(kind.empty() || kind[0]=='#')continue;
        if(kind=="v") {
            Point p{};require(bool(row>>p.x>>p.y>>p.z),"Invalid OBJ vertex");
            require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
                std::abs(p.x)<=1024&&std::abs(p.y)<=1024&&std::abs(p.z)<=1024,"Invalid vertex range");
            source.push_back(p);require(source.size()<=100000,"Too many OBJ vertices");
        } else if(kind=="vt") {
            double u,v;require(bool(row>>u>>v)&&std::isfinite(u)&&std::isfinite(v)&&
                std::abs(u)<=1e6&&std::abs(v)<=1e6,"Invalid OBJ UV");
            uv.push_back({{u,v}});require(uv.size()<=100000,"Too many OBJ UVs");
        } else if(kind=="f") {
            std::array<size_t,3> face;
            for(int i=0;i<3;++i) {
                std::string token;require(bool(row>>token),"OBJ faces must be triangles");
                size_t slash=token.find('/');Point p=source.at(index(token.substr(0,slash),source.size()));
                p.u=p.x/2;p.v=p.y/2;
                if(slash!=std::string::npos) {
                    size_t end=token.find('/',slash+1);std::string ti=token.substr(slash+1,end==std::string::npos?end:end-slash-1);
                    if(!ti.empty()){auto t=uv.at(index(ti,uv.size()));p.u=t[0];p.v=t[1];}
                }
                face[i]=m.points.size();m.points.push_back(p);
            }
            std::string extra;require(!(row>>extra)||extra[0]=='#',"OBJ faces must be triangles");
            m.faces.push_back(face);require(m.faces.size()<=100000,"Too many OBJ faces");
        } else require(kind=="o"||kind=="g"||kind=="s"||kind=="vn"||kind=="usemtl"||kind=="mtllib","Unsupported OBJ record");
    }
    require(!in.bad() && !m.faces.empty(),"Empty or unreadable terrain");return m;
}
inline Mesh load(const char* path) {
    std::ifstream f(path,std::ios::binary);require(bool(f),"Cannot open terrain OBJ");
    f.seekg(0,std::ios::end);require(f.tellg()>=0 && f.tellg()<=16*1024*1024,"OBJ exceeds input budget");f.seekg(0);
    return read(f);
}
inline double cross(const Point&a,const Point&b,const Point&p) {return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
inline Point mix(const Point&a,const Point&b,double t) {return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};}
inline DrawMesh fit(const Mesh&m,std::vector<Point> boundary,double floor,double ceiling,double units,bool worldSpace=false) {
    require(boundary.size()>=3 && boundary.size()<=16,"Invalid host polygon");
    double area=0;for(size_t i=0;i<boundary.size();++i){auto a=boundary[i],b=boundary[(i+1)%boundary.size()];area+=a.x*b.y-b.x*a.y;}
    require(std::abs(area)>1,"Degenerate host polygon");if(area<0)std::reverse(boundary.begin(),boundary.end());
    for(size_t i=0;i<boundary.size();++i)for(auto p:boundary)
        require(cross(boundary[i],boundary[(i+1)%boundary.size()],p)>=-1e-5,"Host polygon must be convex");
    double x0=1e30,y0=1e30,x1=-1e30,y1=-1e30,sx0=1e30,sy0=1e30,sx1=-1e30,sy1=-1e30;
    for(auto p:boundary){x0=std::min(x0,p.x);x1=std::max(x1,p.x);y0=std::min(y0,p.y);y1=std::max(y1,p.y);}
    for(auto p:m.points){sx0=std::min(sx0,p.x);sx1=std::max(sx1,p.x);sy0=std::min(sy0,p.y);sy1=std::max(sy1,p.y);}
    require(sx1-sx0>1e-6 && sy1-sy0>1e-6,"Terrain needs XY area");
    double xs=(x1-x0)/(sx1-sx0),ys=(y1-y0)/(sy1-sy0),zs=std::min(xs,ys);
    double margin=std::min(x1-x0,y1-y0)*.1;
    require(margin>0 && ceiling>floor,"Host has no room");
    DrawMesh out;out.minZ=out.maxZ=float(floor);
    auto project=[&](Point p){if(worldSpace){p.x*=units;p.y*=units;p.z*=units;return p;}p.x=x0+(p.x-sx0)*xs;p.y=y0+(p.y-sy0)*ys;p.z*=zs;return p;};
    for(auto f:m.faces) {
        std::vector<Point> clip={project(m.points[f[0]]),project(m.points[f[1]]),project(m.points[f[2]])};
        // Reject triangles outside this cell before clipping a shared world mesh.
        if(worldSpace && (std::max({clip[0].x,clip[1].x,clip[2].x})<x0 ||
            std::min({clip[0].x,clip[1].x,clip[2].x})>x1 ||
            std::max({clip[0].y,clip[1].y,clip[2].y})<y0 ||
            std::min({clip[0].y,clip[1].y,clip[2].y})>y1))continue;
        // Sutherland-Hodgman clipping keeps mesh inside its visibility polygon.
        for(size_t e=0;e<boundary.size()&&!clip.empty();++e){
            auto a=boundary[e],b=boundary[(e+1)%boundary.size()];std::vector<Point> next;
            auto p=clip.back();double dp=cross(a,b,p);
            for(auto q:clip){double dq=cross(a,b,q);if((dp>=0)!=(dq>=0))next.push_back(mix(p,q,dp/(dp-dq)));
                if(dq>=0)next.push_back(q);
                p=q;dp=dq;}
            clip.swap(next);
        }
        for(auto& p:clip){double d=margin;
            for(size_t e=0;e<boundary.size();++e){auto a=boundary[e],b=boundary[(e+1)%boundary.size()];
                double len=std::hypot(b.x-a.x,b.y-a.y);require(len>0,"Repeated host vertex");d=std::min(d,cross(a,b,p)/len);}
            double w=std::max(0.,std::min(1.,d/margin));if(!worldSpace)p.z=floor+p.z*w*w*(3-2*w);
            require(p.z<ceiling && p.z>=-32*units && p.z<32*units,"Terrain exceeds host ceiling or world range");
        }
        for(size_t j=1;j+1<clip.size();++j){Point a=clip[0],b=clip[j],c=clip[j+1];
            double nx=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y);
            double ny=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z);
            double nz=cross(a,b,c);if(std::abs(nz)<1e-7)continue;
            if(nz<0){std::swap(b,c);nx=-nx;ny=-ny;nz=-nz;}
            double len=std::sqrt(nx*nx+ny*ny+nz*nz);nx/=len;ny/=len;nz/=len;
            // Floor shader convention: tangent along +Y, handedness -1.
            double ty=nz,tz=-ny,tl=std::hypot(ty,tz);ty/=tl;tz/=tl;
            for(auto p:{a,b,c}){out.positions.insert(out.positions.end(),{float(p.x),float(p.y),float(p.z)});
                out.uv.insert(out.uv.end(),{float(p.u),float(p.v)});out.normals.insert(out.normals.end(),{float(nx),float(ny),float(nz)});
                out.tangents.insert(out.tangents.end(),{0,float(ty),float(tz),-1});out.minZ=std::min(out.minZ,float(p.z));out.maxZ=std::max(out.maxZ,float(p.z));}
            require(out.positions.size()<=1800000,"Clipped terrain exceeds vertex budget");
        }
    }
    require(!out.positions.empty(),"Terrain does not cover host polygon");return out;
}

// Index the final fitted triangles, including clipping and perimeter blending.
struct HeightIndex {
    enum { resolution=32 };
    std::array<std::vector<size_t>,resolution*resolution> cells;
    double x0=0,y0=0,x1=1,y1=1;
    int cellX(double x)const{return std::max(0,std::min(resolution-1,int((x-x0)/(x1-x0)*resolution)));}
    int cellY(double y)const{return std::max(0,std::min(resolution-1,int((y-y0)/(y1-y0)*resolution)));}
    void build(const DrawMesh& m){
        for(auto& c:cells)c.clear();
        require(!m.positions.empty(),"Cannot index empty terrain");
        x0=y0=1e30;x1=y1=-1e30;
        for(size_t i=0;i<m.positions.size();i+=3){x0=std::min(x0,double(m.positions[i]));x1=std::max(x1,double(m.positions[i]));
            y0=std::min(y0,double(m.positions[i+1]));y1=std::max(y1,double(m.positions[i+1]));}
        require(x1>x0 && y1>y0,"Invalid terrain bounds");
        size_t references=0;
        for(size_t i=0;i<m.positions.size();i+=9){const float* p=&m.positions[i];
            int ax=cellX(std::min(p[0],std::min(p[3],p[6]))),bx=cellX(std::max(p[0],std::max(p[3],p[6])));
            int ay=cellY(std::min(p[1],std::min(p[4],p[7]))),by=cellY(std::max(p[1],std::max(p[4],p[7])));
            for(int y=ay;y<=by;++y)for(int x=ax;x<=bx;++x){
                require(++references<=2000000,"Terrain collision index exceeds budget");cells[y*resolution+x].push_back(i);}
        }
    }

    // Continuous, one-sided segment/triangle intersections. Candidate cells
    // cover the segment's XY bounds; shared triangle references are deduplicated.
    bool segment(const DrawMesh& m,const Point& a,const Point& b,double& fraction)const{
        if(std::max(a.x,b.x)<x0 || std::min(a.x,b.x)>x1 ||
           std::max(a.y,b.y)<y0 || std::min(a.y,b.y)>y1)return false;
        double startHeight;
        if(height(m,a.x,a.y,startHeight) && a.z<startHeight-1e-4){fraction=0;return true;}
        std::vector<size_t> candidates;
        for(int y=cellY(std::min(a.y,b.y));y<=cellY(std::max(a.y,b.y));++y)
            for(int x=cellX(std::min(a.x,b.x));x<=cellX(std::max(a.x,b.x));++x){
                const auto& c=cells[y*resolution+x];candidates.insert(candidates.end(),c.begin(),c.end());}
        std::sort(candidates.begin(),candidates.end());candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
        double best=2;
        for(size_t i:candidates){const float* p=&m.positions[i];
            double nx=m.normals[i],ny=m.normals[i+1],nz=m.normals[i+2];
            double da=(a.x-p[0])*nx+(a.y-p[1])*ny+(a.z-p[2])*nz;
            double db=(b.x-p[0])*nx+(b.y-p[1])*ny+(b.z-p[2])*nz;
            if(da< -1e-4 || db>1e-4 || da-db<1e-9)continue;
            double t=da/(da-db);if(t< -1e-7 || t>1+1e-7 || t>=best)continue;
            t=std::max(0.,std::min(1.,t));double x=a.x+(b.x-a.x)*t,y=a.y+(b.y-a.y)*t;
            double den=(p[3]-p[0])*(p[7]-p[1])-(p[4]-p[1])*(p[6]-p[0]);
            if(std::abs(den)<1e-7)continue;
            double u=((x-p[0])*(p[7]-p[1])-(y-p[1])*(p[6]-p[0]))/den;
            double v=((p[3]-p[0])*(y-p[1])-(p[4]-p[1])*(x-p[0]))/den;
            if(u>=-1e-6 && v>=-1e-6 && u+v<=1+1e-6)best=t;
        }
        if(best>1)return false;
        fraction=best;return true;
    }
    bool height(const DrawMesh& m,double x,double y,double& z,std::array<double,3>* normal=nullptr)const{
        if(x<x0 || x>x1 || y<y0 || y>y1)return false;
        bool found=false;double result=-1e30;
        for(size_t i:cells[cellY(y)*resolution+cellX(x)]){
            const float* p=&m.positions[i];
            double ax=p[0],ay=p[1],bx=p[3],by=p[4],cx=p[6],cy=p[7];
            double den=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);if(std::abs(den)<1e-7)continue;
            double b=((x-ax)*(cy-ay)-(y-ay)*(cx-ax))/den;
            double c=((bx-ax)*(y-ay)-(by-ay)*(x-ax))/den,a=1-b-c;
            if(a>=-1e-6 && b>=-1e-6 && c>=-1e-6){double h=a*p[2]+b*p[5]+c*p[8];
                if(h>result){result=h;if(normal)*normal={{m.normals[i],m.normals[i+1],m.normals[i+2]}};}found=true;}
        }
        if(found)z=result;
        return found;
    }
};
// Position is in world units here; callers retain the engine's fixed-point state.
inline bool groundContact(double previousZ,double previousFloor,double nextZ,
    double nextFloor,double verticalVelocity,bool firstTime,double tolerance,double& correctedZ){
    const bool walking=verticalVelocity<=0 && std::abs(previousZ-previousFloor)<=tolerance;
    if(nextZ<nextFloor || walking || (firstTime && std::abs(previousZ-previousFloor)<=tolerance)){
        correctedZ=nextFloor;return true;
    }
    correctedZ=nextZ;return false;
}


// Lower a shared-XY vertex consistently in every triangle copy. XY, UVs and
// triangle topology stay fixed, so the existing collision cell index is valid.
inline bool crater(DrawMesh& mesh,const std::vector<float>& original,
    const std::vector<Point>& boundary,double x,double y,double radius,double depth,double cap,double units){
    require(original.size()==mesh.positions.size() && boundary.size()>=3,"Invalid crater baseline");
    require(radius>0 && depth>0 && cap>0 && std::isfinite(x)&&std::isfinite(y),"Invalid crater parameters");
    bool changed=false;
    for(size_t i=0;i<mesh.positions.size();i+=3){
        double px=mesh.positions[i],py=mesh.positions[i+1];
        double r2=((px-x)*(px-x)+(py-y)*(py-y))/(radius*radius);
        if(r2>=1)continue;
        double distance=radius;
        for(size_t e=0;e<boundary.size();++e){auto a=boundary[e],b=boundary[(e+1)%boundary.size()];
            double len=std::hypot(b.x-a.x,b.y-a.y);
            if(len>0)distance=std::min(distance,std::abs(cross(a,b,Point{px,py,0,0,0}))/len);}
        // Pin all boundary vertices exactly, including tiny clipping roundoff.
        if(distance<.01)continue;
        double edge=std::min(1.,distance/(radius*.4));edge=edge*edge*(3-2*edge);
        double bowl=(1-r2)*(1-r2);
        float z=float(std::max(-32*units,std::max(double(original[i+2])-cap*edge,
            double(mesh.positions[i+2])-depth*bowl*edge)));
        if(z<mesh.positions[i+2]){mesh.positions[i+2]=z;changed=true;}
    }
    if(!changed)return false;
    mesh.minZ=mesh.maxZ=mesh.positions[2];
    for(size_t i=0;i<mesh.positions.size();i+=9){const float* p=&mesh.positions[i];
        double ax=double(p[3])-p[0],ay=double(p[4])-p[1],az=double(p[5])-p[2];
        double bx=double(p[6])-p[0],by=double(p[7])-p[1],bz=double(p[8])-p[2];
        double nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
        double length=std::sqrt(nx*nx+ny*ny+nz*nz);nx/=length;ny/=length;nz/=length;
        double tl=std::hypot(nz,ny);
        for(int k=0;k<3;++k){size_t v=i+3*k,t=(i/3+k)*4;
            mesh.normals[v]=float(nx);mesh.normals[v+1]=float(ny);mesh.normals[v+2]=float(nz);
            mesh.tangents[t]=0;mesh.tangents[t+1]=float(nz/tl);mesh.tangents[t+2]=float(-ny/tl);mesh.tangents[t+3]=-1;
            mesh.minZ=std::min(mesh.minZ,mesh.positions[v+2]);mesh.maxZ=std::max(mesh.maxZ,mesh.positions[v+2]);}
    }
    return true;
}

} // namespace SprintathonTerrain
#endif
