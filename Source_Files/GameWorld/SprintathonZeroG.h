#ifndef SPRINTATHON_ZERO_G_H
#define SPRINTATHON_ZERO_G_H
#include "SprintathonZeroGMath.h"
#include "map.h"
#include "player.h"
#include "render.h"
#include "shell.h"
#include "screen.h"
#include "vbl.h"
#include "preferences.h"
#include <cstdlib>
#include <cstdio>
namespace SprintathonZeroG {
using namespace SprintathonZeroGMath;
inline void reset();
inline int mode(){
 const int value=input_preferences && input_preferences->sprintathon_enabled && input_preferences->sprintathon_zero_g?1:0;
 static int previous=-1;
 if(value!=previous){reset();previous=value;}
 return value;
}
inline bool requested(){return mode()!=0;}
inline bool kicksOnly(){return mode()==2;}
inline bool debug(){static bool on=[](){const char* p=std::getenv("SPRINTATHON_ZERO_G_DEBUG");return p && p[0]=='1';}();return on;}
inline bool traceFrame(uint32 flags){
    static uint32 previous=0;static int remaining=1;
    if(!debug())return false;
    const uint32 buttons=flags&(_swim|_microphone_button);
    if(buttons!=previous){previous=buttons;remaining=12;}
    if(remaining<=0)return false;
    --remaining;return true;
}
inline bool active(const player_data* p){
#ifdef HAVE_OPENGL
 return requested() && p && p==local_player && !PLAYER_IS_DEAD(p) && !game_is_networked &&
 !game_is_being_replayed() && get_screen_mode()->acceleration==_opengl_acceleration;
#else
 return false;
#endif
}
struct State {Q previous,q;bool ready=false,jumpHeld=false,crouchHeld=false,kickSpent=false,kickedThisTick=false;
 _fixed bodyHeight=0;double animationPhase=0;};
inline State& state(){static State s;return s;}
inline void reset(){state()=State{};}
inline void seed(player_data* p){auto& s=state();if(!s.ready){
 const double unit=2*pi/(FULL_CIRCLE*double(FIXED_ONE));
 s.q=fromAngles(p->variables.direction*unit,p->variables.elevation*unit,0);s.previous=s.q;s.bodyHeight=p->variables.actual_height;s.ready=true;}}
inline void orient(player_data* p){
 V a=angles(state().q);const double unit=FULL_CIRCLE*double(FIXED_ONE)/(2*pi);
 auto& v=p->variables;
 double yaw=a.x<0?a.x+2*pi:a.x;
 v.direction=static_cast<_fixed>(std::lround(yaw*unit))%(FULL_CIRCLE*FIXED_ONE);
 v.elevation=static_cast<_fixed>(std::lround(a.y*unit));v.head_direction=0;
 p->sprintathon_camera_roll=static_cast<int16>(std::lround(a.z*FULL_CIRCLE/(2*pi)));
}
inline V velocity(player_data* p){auto& v=p->variables.external_velocity;return {v.i/double(FIXED_ONE),v.j/double(FIXED_ONE),v.k/double(FIXED_ONE)};}
inline void velocity(player_data* p,V value){auto& v=p->variables.external_velocity;
 v.i=static_cast<_fixed>(std::lround(value.x*FIXED_ONE));v.j=static_cast<_fixed>(std::lround(value.y*FIXED_ONE));v.k=static_cast<_fixed>(std::lround(value.z*FIXED_ONE));}
using ContactQuery=V (*)(player_data*,V);
template<class Constants> inline void update(player_data* p,uint32 flags,const Constants* constants,double dt,ContactQuery query=nullptr){
 seed(p);auto& s=state();auto& v=p->variables;s.previous=s.q;
 double yaw=(flags&_absolute_yaw_mode)?GET_ABSOLUTE_YAW(flags)-MAXIMUM_ABSOLUTE_YAW/2:
 ((flags&_turning_right)?2.:0.)-((flags&_turning_left)?2.:0.);
 double pitch=(flags&_absolute_pitch_mode)?GET_ABSOLUTE_PITCH(flags)-MAXIMUM_ABSOLUTE_PITCH/2:
 ((flags&_looking_up)?2.:0.)-((flags&_looking_down)?2.:0.);
 double side=((flags&_sidestepping_right)?1.:0.)-((flags&_sidestepping_left)?1.:0.);
 // Sprint has its own binding; Run/Walk may be permanently enabled.
 bool rolling=(flags&_look_dont_turn)!=0;
 s.q=turn(s.q,yaw*2*pi/FULL_CIRCLE,pitch*2*pi/FULL_CIRCLE,rolling?side*.055*dt:0.);
 double forward=(flags&_absolute_position_mode)?
 (GET_ABSOLUTE_POSITION(flags)-MAXIMUM_ABSOLUTE_POSITION/2)/double(MAXIMUM_ABSOLUTE_POSITION/2):
 ((flags&_moving_forward)?1.:0.)-((flags&_moving_backward)?1.:0.);
 bool up=(flags&_swim)!=0,down=(flags&_microphone_button)!=0;
 V speed=velocity(p);
 s.kickedThisTick=false;
 if(kicksOnly()){
     const V bodyUp=s.q.rotate({0,0,1}),forwardAxis=s.q.rotate({1,0,0});
     if(up && !s.jumpHeld && query){
         V normal=query(p,bodyUp*-1);
         if(normal.length()>.5 && normal.dot(bodyUp)>.65)
             speed=collide(speed,normal)+bodyUp*(1.0/13.0);
     }
     s.animationPhase+=dt;
     const int animationTicks=int(s.animationPhase);s.animationPhase-=animationTicks;
     p->flying_kick_exit_ticks=static_cast<uint8>(std::max(0,int(p->flying_kick_exit_ticks)-animationTicks));
     if(down && !s.crouchHeld){
         s.kickSpent=false;p->flying_kick_active=true;p->flying_kick_ticks=0;p->flying_kick_exit_ticks=0;
     }
     if(down && !s.kickSpent && p->flying_kick_active){
         p->flying_kick_ticks=static_cast<uint8>(std::min(255,int(p->flying_kick_ticks)+animationTicks));
         // Reuse the normal legs-in animation, then permit one wall rebound
         // while held. An air kick displays the legs but adds no momentum.
         if(p->flying_kick_ticks>=3 && query){
             V normal=query(p,forwardAxis);
             if(normal.length()>.5 && normal.dot(forwardAxis)<-.65){
                 speed=collide(speed,normal)-forwardAxis*.16;
                 s.kickSpent=true;s.kickedThisTick=true;
                 p->flying_kick_active=false;p->flying_kick_ticks=0;p->flying_kick_exit_ticks=8;
             }
         }
     }
     if(!down){
         if(p->flying_kick_active)p->flying_kick_exit_ticks=8;
         p->flying_kick_active=false;p->flying_kick_ticks=0;s.kickSpent=false;
     }
     s.crouchHeld=down;
     p->flying_kick_requested=false;p->flying_kick_landing_ticks=0;
     p->flying_kick_recovery_pending=false;p->wall_kick_rearm_pending=false;
     p->wall_kick_cooldown_ticks=0;p->slide_recovery_ticks=0;
     speed=advanceVelocity(speed,s.q,{},dt,false);
 }else speed=advanceVelocity(speed,s.q,{forward,rolling?0:side,double(up)-double(down)},dt,up&&down);
 s.jumpHeld=up;
 velocity(p,speed);v.last_position=v.position;v.last_direction=v.direction;
 // Leave enough integer range for world-coordinate conversion; collisions
 // below provide the actual bounds for the destination polygon.
 auto move=[&](auto& position,double delta){double next=position+delta*dt*FIXED_ONE;
 position=static_cast<_fixed>(std::lround(std::max(-32.*FIXED_ONE,std::min(32.*FIXED_ONE-64,next))));};
 move(v.position.x,speed.x);move(v.position.y,speed.y);move(v.position.z,speed.z);
 v.velocity=v.perpendicular_velocity=0;v.angular_velocity=v.vertical_angular_velocity=0;v.external_angular_velocity=0;
 v.action=_player_airborne;v.step_phase=v.step_amplitude=0;v.old_flags=v.flags;v.flags=(v.flags&(_FEET_BELOW_MEDIA_BIT|_HEAD_BELOW_MEDIA_BIT))|_ABOVE_GROUND_BIT;v.jump_grace_ticks=0;
 // Jump and the legs-out pose never resize the body or lower the eye.
 // Preserve the collision height captured when zero-g starts.
 (void)constants;v.actual_height=s.bodyHeight;
 p->sprinting=false;p->sprintathon_camera_pitch=0;p->cartwheel_camera_roll=0;p->backflip_camera_pitch=0;
 p->slide_roll_ticks_remaining=0;p->slide_ticks_remaining=0;p->dodge_ticks_remaining=0;
 orient(p);
}
inline void recoil(player_data* p,double magnitude){if(!active(p))return;seed(p);
 velocity(p,velocity(p)-state().q.rotate({1,0,0})*std::max(.012,std::min(.12,magnitude)));}
inline void correct(player_data* p,double dx,double dy){velocity(p,collide(velocity(p),{dx,dy,0}));}
inline void applyView(view_data* view,float fraction){
 if(!active(current_player) || !state().ready)return;
 V a=angles(blend(state().previous,state().q,fraction));
 double yaw=a.x<0?a.x+2*pi:a.x,pitch=a.y<0?a.y+2*pi:a.y;
 view->virtual_yaw=static_cast<fixed_angle>(std::lround(yaw*FULL_CIRCLE*FIXED_ONE/(2*pi)));
 view->virtual_pitch=static_cast<fixed_angle>(std::lround(pitch*FULL_CIRCLE*FIXED_ONE/(2*pi)));
 view->yaw=NORMALIZE_ANGLE(FIXED_INTEGERAL_PART(view->virtual_yaw));
 view->pitch=NORMALIZE_ANGLE(FIXED_INTEGERAL_PART(view->virtual_pitch));
 view->roll=static_cast<int16>(std::lround(a.z*FULL_CIRCLE/(2*pi)));
 view->mimic_sw_perspective=false;
}
}
#endif
