#ifndef SPRINTATHON_FLASHLIGHT_SHADOW_H
#define SPRINTATHON_FLASHLIGHT_SHADOW_H
#include "SprintathonFlashlight.h"
#include "SprintathonTerrainRuntime.h"
#include "map.h"
#include "OGL_Textures.h"
#include "AnimatedTextures.h"
#include "OGL_Model_Def.h"
#include "monsters.h"
#include "player.h"
#include <vector>
#include <cstdio>

void instantiate_transfer_mode(view_data*,short,world_distance&,world_distance&);

// A separate light-space pass: never reuse the camera's clipped visibility tree.
// Level surfaces, alpha-tested wall textures and camera-facing enemy and scenery sprites.
namespace SprintathonFlashlightShadow {
constexpr int size=1024;
struct Target { GLuint texture=0,fbo=0; GLhandleARB program=0; GLint alpha=-1; std::vector<unsigned char> polygons; };
inline Target& target(){static Target value;return value;}
inline void multiply(const GLfloat* a,const GLfloat* b,GLfloat* out) {
    for(int col=0;col<4;++col)for(int row=0;row<4;++row){
        out[col*4+row]=0;
        for(int k=0;k<4;++k)out[col*4+row]+=a[k*4+row]*b[col*4+k];
    }
}
inline void normalize(float* v){
    const float scale=1.0f/std::sqrt(std::max(0.000001f,v[0]*v[0]+v[1]*v[1]+v[2]*v[2]));
    for(int i=0;i<3;++i)v[i]*=scale;
}
inline void matrices(const SprintathonFlashlight::Frame& f,GLfloat* projection,GLfloat* lightView) {
    float forward[3]={f.depth[2],f.depth[3],-1};normalize(forward);
    float right[3]={-forward[2],0,forward[0]};normalize(right);
    float up[3]={right[1]*forward[2]-right[2]*forward[1],right[2]*forward[0]-right[0]*forward[2],right[0]*forward[1]-right[1]*forward[0]};
    const float origin[3]={f.projection[0],f.projection[1],f.projection[3]};
    for(int i=0;i<16;++i){projection[i]=0;lightView[i]=0;}
    for(int i=0;i<3;++i){
        lightView[i*4]=right[i];lightView[i*4+1]=up[i];lightView[i*4+2]=-forward[i];
        lightView[12]-=right[i]*origin[i];lightView[13]-=up[i]*origin[i];lightView[14]+=forward[i]*origin[i];
    }
    lightView[15]=1;
    // Broader than the illumination cone, including its finite starting radius.
    const float tangent=std::max(1.0f,f.params[1]+0.25f),nearZ=8.0f,farZ=f.params[2];
    projection[0]=projection[5]=1.0f/tangent;
    projection[10]=-(farZ+nearZ)/(farZ-nearZ);
    projection[11]=-1;projection[14]=-2*farZ*nearZ/(farZ-nearZ);
}
inline GLhandleARB compile(GLenum type,const char* source) {
    GLhandleARB shader=glCreateShaderObjectARB(type);
    glShaderSourceARB(shader,1,&source,nullptr);glCompileShaderARB(shader);
    GLint ok=0;glGetObjectParameterivARB(shader,GL_OBJECT_COMPILE_STATUS_ARB,&ok);
    if(!ok){char log[2048]={};glGetInfoLogARB(shader,sizeof(log),nullptr,log);
        std::fprintf(stderr,"Flashlight shadow shader: %s\n",log);glDeleteObjectARB(shader);return 0;}
    return shader;
}
inline bool prepare() {
    auto& t=target();
    if(t.texture && !glIsTexture(t.texture))t=Target{};
    if(!t.program){
        const char* vertex="#version 120\nvoid main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;gl_TexCoord[0]=gl_TextureMatrix[0]*gl_MultiTexCoord0;}";
        const char* fragment="#version 120\nuniform sampler2D casterTexture;uniform float alphaTest;void main(){if(alphaTest>0.5 && texture2D(casterTexture,gl_TexCoord[0].xy).a<0.5)discard;gl_FragColor=vec4(1.0);}";
        const auto v=compile(GL_VERTEX_SHADER_ARB,vertex),f=compile(GL_FRAGMENT_SHADER_ARB,fragment);
        if(!v || !f){if(v)glDeleteObjectARB(v);if(f)glDeleteObjectARB(f);return false;}
        t.program=glCreateProgramObjectARB();glAttachObjectARB(t.program,v);glAttachObjectARB(t.program,f);glLinkProgramARB(t.program);
        glDeleteObjectARB(v);glDeleteObjectARB(f);
        GLint ok=0;glGetObjectParameterivARB(t.program,GL_OBJECT_LINK_STATUS_ARB,&ok);
        if(!ok){glDeleteObjectARB(t.program);t.program=0;return false;}
        t.alpha=glGetUniformLocationARB(t.program,"alphaTest");
    }
    if(!t.texture){
        glGenTextures(1,&t.texture);glBindTexture(GL_TEXTURE_2D,t.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,size,size,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
    }
    if(!t.fbo)glGenFramebuffersEXT(1,&t.fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,t.fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,t.texture,0);
    glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    return glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
}
inline bool landscape(short mode){return mode==_xfer_landscape || mode==_xfer_big_landscape;}
inline void horizontal(const polygon_data& p,float height) {
    glBegin(GL_TRIANGLE_FAN);
    for(int i=0;i<p.vertex_count;++i){const auto& v=get_endpoint_data(p.endpoint_indexes[i])->vertex;glVertex3f(v.x,v.y,height);}
    glEnd();
}
inline void wall(view_data* view,const world_point2d& a,const world_point2d& b,
                 float low,float high,float textureTop,const side_texture_definition* texture,short mode,bool masked) {
    if(high<=low || landscape(mode))return;
    float scale=WORLD_ONE,x0=0,y0=0;
    TextureManager material;
    bool textured=false;
    if(masked){
        if(!texture || texture->texture==UNONE)return;
        // Scrolling and scaled grates use the same UV convention as the world pass.
        material.ShapeDesc=AnimTxtr_Translate(texture->texture);
        get_shape_bitmap_and_shading_table(material.ShapeDesc,&material.Texture,&material.ShadingTables,_shading_normal);
        material.TextureType=OGL_Txtr_Wall;material.TransferMode=_textured_transfer;
        material.IsShadeless=0;material.TransferData=0;
        if(!material.Setup())return;
        // Fractional-opacity glass is not a binary occluder. Cutout grates are.
        if(material.IsBlended())return;
        material.RenderNormal();material.SetupTextureMatrix();textured=true;
        scale*=material.TileRatio()*(mode==_xfer_2x?2.0f:mode==_xfer_4x?4.0f:1.0f);
        world_distance x=0,y=0;instantiate_transfer_mode(view,mode,x,y);
        x0=std::fmod(float(texture->x0),scale)-x;y0=std::fmod(float(texture->y0),scale)-y;
    }
    glUseProgramObjectARB(target().program);glUniform1fARB(target().alpha,textured?1.0f:0.0f);
    const float dx=float(b.x)-a.x,dy=float(b.y)-a.y,length=std::sqrt(dx*dx+dy*dy);
    glBegin(GL_QUADS);
    glTexCoord2f((textureTop+y0-high)/scale,x0/scale);glVertex3f(a.x,a.y,high);
    glTexCoord2f((textureTop+y0-high)/scale,(x0+length)/scale);glVertex3f(b.x,b.y,high);
    glTexCoord2f((textureTop+y0-low)/scale,(x0+length)/scale);glVertex3f(b.x,b.y,low);
    glTexCoord2f((textureTop+y0-low)/scale,x0/scale);glVertex3f(a.x,a.y,low);
    glEnd();
    if(textured)material.RestoreTextureMatrix();
    glUniform1fARB(target().alpha,0);
}
inline void geometry(view_data* view,const GLfloat* modelview) {
    auto& visited=target().polygons;visited.clear();
    const int count=dynamic_world->polygon_count;
    if(view->origin_polygon_index<0 || view->origin_polygon_index>=count)return;
    const auto& f=SprintathonFlashlight::frame();
    const float eye[3]={f.projection[0]-modelview[12],f.projection[1]-modelview[13],f.projection[3]-modelview[14]};
    float origin[3]={};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)origin[i]+=modelview[i*4+j]*eye[j];
    visited.assign(count,0);std::vector<int> pending;
    pending.push_back(view->origin_polygon_index);visited[view->origin_polygon_index]=1;
    for(size_t index=0;index<pending.size();++index){
        const auto& p=*get_polygon_data(pending[index]);
        if(!p.vertex_count)continue;
        const auto* terrain=SprintathonTerrainRuntime::meshFor(get_polygon_data(pending[index]));
        float lo[3]={32768,32768,terrain?terrain->minZ:float(p.floor_height)},hi[3]={-32768,-32768,float(p.ceiling_height)};
        for(int i=0;i<p.vertex_count;++i){const auto& v=get_endpoint_data(p.endpoint_indexes[i])->vertex;
            lo[0]=std::min(lo[0],float(v.x));lo[1]=std::min(lo[1],float(v.y));hi[0]=std::max(hi[0],float(v.x));hi[1]=std::max(hi[1],float(v.y));}
        float distance2=0;
        for(int i=0;i<3;++i){const float d=std::max(lo[i]-origin[i],std::max(0.0f,origin[i]-hi[i]));distance2+=d*d;}
        if(distance2>f.params[2]*f.params[2])continue;
        if(terrain){
            // The depth shader needs only world positions; keep its existing
            // alpha/texture state and replace the flat floor, never draw both.
            glBegin(GL_TRIANGLES);
            for(size_t v=0;v<terrain->positions.size();v+=3)
                glVertex3f(terrain->positions[v],terrain->positions[v+1],terrain->positions[v+2]);
            glEnd();
        }else if(!landscape(p.floor_transfer_mode))horizontal(p,p.floor_height);
        if(!landscape(p.ceiling_transfer_mode))horizontal(p,p.ceiling_height);
        for(int i=0;i<p.vertex_count;++i){
            const auto& a=get_endpoint_data(p.endpoint_indexes[i])->vertex;
            const auto& b=get_endpoint_data(p.endpoint_indexes[(i+1)%p.vertex_count])->vertex;
            const int adjacent=p.adjacent_polygon_indexes[i];
            if(adjacent>=0 && adjacent<count){
                const auto& other=*get_polygon_data(adjacent);
                if(!visited[adjacent] && std::max(p.floor_height,other.floor_height)<std::min(p.ceiling_height,other.ceiling_height)){
                    visited[adjacent]=1;pending.push_back(adjacent);
                }
            }
            if(p.side_indexes[i]==NONE)continue;
            const auto& side=*get_side_data(p.side_indexes[i]);
            const float floor=p.floor_height,ceiling=p.ceiling_height;
            // Side types describe texture layout, not the current opening.
            // A door can retain a full-side record after its platform opens.
            // Use live polygon heights (also valid during interpolation), not
            // the cached line bounds or camera portal clipping.
            float openingBottom=ceiling,openingTop=floor;
            if(adjacent>=0 && adjacent<count){
                const auto& other=*get_polygon_data(adjacent);
                openingBottom=std::max(floor,float(other.floor_height));
                openingTop=std::min(ceiling,float(other.ceiling_height));
            }
            if(openingBottom>=openingTop){
                wall(view,a,b,floor,ceiling,ceiling,&side.primary_texture,side.primary_transfer_mode,false);
            }else{
                wall(view,a,b,floor,openingBottom,openingBottom,
                     side.type==_split_side?&side.secondary_texture:&side.primary_texture,
                     side.type==_split_side?side.secondary_transfer_mode:side.primary_transfer_mode,false);
                wall(view,a,b,openingTop,ceiling,ceiling,
                     &side.primary_texture,side.primary_transfer_mode,false);
                // A real cutout texture across an open portal still blocks
                // light at its opaque texels (grates and fences).
                wall(view,a,b,openingBottom,openingTop,openingTop,
                     &side.transparent_texture,side.transparent_transfer_mode,true);
            }
        }
    }
}
inline bool sphereInLight(const GLfloat* matrix,const world_point3d& p,float radius) {
    // Test against all six light frustum planes, not the player's view frustum.
    for(int axis=0;axis<3;++axis)for(int sign=-1;sign<=1;sign+=2){
        float plane[4];
        for(int i=0;i<4;++i)plane[i]=matrix[i*4+3]+sign*matrix[i*4+axis];
        const float length=std::sqrt(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2]);
        if(plane[0]*p.x+plane[1]*p.y+plane[2]*p.z+plane[3]<-radius*length)return false;
    }
    return true;
}
inline void sprites(view_data* view,const GLfloat* modelview) {
    GLfloat worldToLight[16];multiply(SprintathonFlashlight::frame().shadowMatrix,modelview,worldToLight);
    const auto& polygons=target().polygons;
    const double degrees=360.0/(double(FIXED_ONE)*double(FULL_CIRCLE));
    const double yaw=view->virtual_yaw*degrees;
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.0f,2.0f);
    // Use live objects rather than RenderObjects: off-screen enemies and scenery can cast
    // into the view. Select the same directional frame as the camera renderer.
    for(size_t index=0;index<ObjectList.size();++index){
        auto& object=ObjectList[index];
        if(!SLOT_IS_USED(&object) || (GET_OBJECT_OWNER(&object)!=_object_is_monster && GET_OBJECT_OWNER(&object)!=_object_is_scenery) ||
           OBJECT_IS_INVISIBLE(&object) || int(index)==current_player->object_index ||
           object.shape==UNONE || object.polygon<0 || size_t(object.polygon)>=polygons.size() ||
           !polygons[object.polygon])continue;
        if(!is_collection_present(GET_COLLECTION(GET_DESCRIPTOR_COLLECTION(object.shape))))continue;
        shape_and_transfer_mode data={};get_object_shape_and_transfer_mode(&view->origin,&object,&data);
        // Cloaking and teleport transitions need a separate opacity treatment.
        if(data.collection_code==NONE || data.transfer_mode!=_xfer_normal)continue;
        if(data.low_level_shape_index<0 || data.low_level_shape_index>=get_number_of_collection_frames(GET_COLLECTION(data.collection_code)))continue;
        short modelSequence=0;
        if(OGL_GetModelData(GET_COLLECTION(data.collection_code),GET_DESCRIPTOR_SHAPE(object.shape),modelSequence))continue;
        const auto* info=extended_get_shape_information(data.collection_code,data.low_level_shape_index);
        if(!info)continue;
        const auto scaled=[&](int value){
            if(object.flags&_object_is_enlarged)return value+(value>>2);
            if(object.flags&_object_is_tiny)return value>>1;
            return value;
        };
        const float left=scaled(info->world_left),right=scaled(info->world_right);
        const float top=scaled(info->world_top),bottom=scaled(info->world_bottom);
        const float center=(top+bottom)*0.5f;
        const float width=std::max(std::abs(left),std::abs(right));
        // Banking rotates about the vertical midpoint, so bound that pivot too.
        const float radius=std::abs(center)+std::sqrt(width*width+(top-bottom)*(top-bottom)*0.25f)+2.0f;
        if(!sphereInLight(worldToLight,object.location,radius))continue;
        TextureManager material;
        material.ShapeDesc=BUILD_DESCRIPTOR(data.collection_code,0);material.LowLevelShape=data.low_level_shape_index;
        extended_get_shape_bitmap_and_shading_table(data.collection_code,data.low_level_shape_index,
            &material.Texture,&material.ShadingTables,_shading_normal);
        if(!material.Texture)continue;
        material.TextureType=OGL_Txtr_Inhabitant;material.TransferMode=_textured_transfer;
        material.TransferData=0;material.IsShadeless=false;
        if(!material.Setup())continue;
        material.RenderNormal();material.SetupTextureMatrix();
        glUseProgramObjectARB(target().program);glUniform1fARB(target().alpha,1.0f);
        float u0=material.U_Offset,u1=material.U_Offset+material.U_Scale;
        float v0=material.V_Offset,v1=material.V_Offset+material.V_Scale;
        if(info->flags&_Y_MIRRORED_BIT)std::swap(u0,u1);
        if(info->flags&_X_MIRRORED_BIT)std::swap(v0,v1);
        glMatrixMode(GL_MODELVIEW);glPushMatrix();
        glTranslatef(object.location.x,object.location.y,object.location.z);
        glRotated(yaw,0,0,1);
        if(!view->mimic_sw_perspective && (material.ForceXYBillboard() || (view->billboard_xy && !material.ForceYBillboard())))
            glRotated(view->virtual_pitch*degrees,0,-1,0);
        if(GET_OBJECT_OWNER(&object)==_object_is_monster &&
           object.permutation>=0 && size_t(object.permutation)<MonsterList.size() &&
           SLOT_IS_USED(&MonsterList[object.permutation]) && MonsterList[object.permutation].object_index==int(index)){
            const float bank=sprintathon_flight_bank(object.permutation,float(yaw*0.017453292519943295));
            glTranslatef(0,0,center);glRotatef(bank,1,0,0);glTranslatef(0,0,-center);
        }
        glBegin(GL_QUADS);
        glTexCoord2f(u0,v0);glVertex3f(0,left,top);
        glTexCoord2f(u0,v1);glVertex3f(0,right,top);
        glTexCoord2f(u1,v1);glVertex3f(0,right,bottom);
        glTexCoord2f(u1,v0);glVertex3f(0,left,bottom);
        glEnd();glPopMatrix();material.RestoreTextureMatrix();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);glUniform1fARB(target().alpha,0);
}
inline void render(view_data* view,const GLfloat* modelview,bool allowed) {
    auto& f=SprintathonFlashlight::frame();f.shadowParams[0]=0;
    if(!allowed || f.params[0]<=0 || !graphics_preferences->flashlight_contact_shadows || graphics_preferences->flashlight_contact_strength<=0)return;
    GLint units=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);if(units<10)return;
    GLint oldFbo,oldActive,oldMode;glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&oldFbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE_ARB,&oldActive);glGetIntegerv(GL_MATRIX_MODE,&oldMode);
    const auto oldProgram=glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glActiveTextureARB(GL_TEXTURE0_ARB);
    glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_PROJECTION);glPushMatrix();glMatrixMode(GL_MODELVIEW);glPushMatrix();
    if(prepare()){
        GLfloat projection[16],lightView[16],worldView[16];matrices(f,projection,lightView);
        multiply(projection,lightView,f.shadowMatrix);multiply(lightView,modelview,worldView);
        glMatrixMode(GL_PROJECTION);glLoadMatrixf(projection);glMatrixMode(GL_MODELVIEW);glLoadMatrixf(worldView);
        glViewport(0,0,size,size);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);
        for(int i=0;i<6;++i)glDisable(GL_CLIP_PLANE0+i);
        glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);
        glDisable(GL_POLYGON_OFFSET_FILL);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glDepthRange(0,1);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgramObjectARB(target().program);glUniform1fARB(target().alpha,0);
        geometry(view,modelview);
        sprites(view,modelview);
        f.shadowTexture=target().texture;f.shadowParams[0]=std::max(0,std::min(100,int(graphics_preferences->flashlight_contact_strength)))/100.0f;
        f.shadowParams[1]=1.0f/size;
    }
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();
    glMatrixMode(GL_TEXTURE);glPopMatrix();glMatrixMode(oldMode);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,oldFbo);glUseProgramObjectARB(oldProgram);
    glPopClientAttrib();glPopAttrib();glActiveTextureARB(oldActive);
}
}
#endif
