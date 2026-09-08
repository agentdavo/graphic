/* Pure animation and firing geometry. Limits are demo mechanics, not canon
 * engineering claims: full azimuth, 0..75 degrees above the mounting plane.
 * A shot's launch pose is evaluated at its birth tick, never moved in flight. */
#ifndef OMEGA_WEAPONS_H
#define OMEGA_WEAPONS_H
#include "omega_shared.h"
#include "vkmin_math.h"
static float ship_position(float t) {
    const float start=OMEGA_GATE_ENTRANCE_Z-20.f,speed=220.f,brake=70.f,cruise=22.f,rate=3.7f;
    const float travel=fmaxf(0,t-4.5f),brake_time=(start-brake)/speed;
    if(travel<brake_time) return start-speed*travel;
    const float dt=travel-brake_time;
    return brake-cruise*dt-(speed-cruise)/rate*(1-expf(-rate*dt));
}
static int particle_age(uint32_t tick,unsigned ship,unsigned shot) {
    const int elapsed=(int)tick-OMEGA_PARTICLE_START-(int)ship*OMEGA_PARTICLE_STAGGER;
    if(elapsed<0) return -1;
    const int age=elapsed%OMEGA_PARTICLE_PERIOD-(int)shot*OMEGA_PARTICLE_SHOT_SPACING;
    return (int)tick-age<OMEGA_PARTICLE_END?age:-1;
}
static vec3 omega_formation(unsigned owner,float t) {
    if(!owner) return (vec3){0,0,ship_position(t)};
    return (vec3){owner==2?45.f:(owner==3?75.f:0.f),owner==2?38.f:(owner==3?-26.f:0.f),
        fminf(ship_position(t),-30.f)-OMEGA_BATTLE_SEPARATION+(owner==2?-50.f:(owner==3?45.f:0.f))};
}
static vec3 omega_rotate(vec3 p,bool inverse) {
    const float s=inverse?-OMEGA_FORMATION_SIN:OMEGA_FORMATION_SIN,c=OMEGA_FORMATION_COS;
    return (vec3){c*p.x+s*p.z,p.y,-s*p.x+c*p.z};
}
static vec3 omega_target(unsigned owner,float t) {
    const vec3 local=owner?omega_pulse_contact[owner-1]:omega_beam_contact[0];
    return vkmin_vec3_add(local,omega_formation(owner?0:1,t));
}
static mat4 omega_turret(unsigned owner,unsigned mount,bool barrel,float t) {
    const vec3 base=omega_mount[mount],n=omega_mount_normal[mount];
    const vec3 u=vkmin_vec3_cross(n,(vec3){0,0,1});
    const vec3 pivot=vkmin_vec3_add(base,vkmin_vec3_scale(n,.39f));
    const vec3 translation=omega_formation(owner,t);
    vec3 target=vkmin_vec3_sub(omega_target(owner,t+(float)OMEGA_PARTICLE_FLIGHT/60.f),translation);
    if(owner) target=omega_rotate(target,true);
    const vec3 aim=vkmin_vec3_normalize(vkmin_vec3_sub(target,pivot));
    const float height=vkmin_vec3_dot(aim,n);
    vec3 forward=vkmin_vec3_sub(aim,vkmin_vec3_scale(n,height));
    forward=vkmin_vec3_length(forward)>.0001f?vkmin_vec3_normalize(forward):(vec3){0,0,-1};
    const float acquire=fminf(1,fmaxf(0,(t-11.5f)/1.7f));
    const float blend=acquire*acquire*(3-2*acquire);
    const float azimuth=atan2f(vkmin_vec3_dot(forward,u),-forward.z)*blend;
    forward=vkmin_vec3_add(vkmin_vec3_scale(u,sinf(azimuth)),(vec3){0,0,-cosf(azimuth)});
    const vec3 right=vkmin_vec3_cross(forward,n);
    if(barrel) {
        const float elevation=fminf(1.30899694f,fmaxf(0,asinf(fminf(1,fmaxf(-1,height)))))*blend;
        forward=vkmin_vec3_add(vkmin_vec3_scale(forward,cosf(elevation)),vkmin_vec3_scale(n,sinf(elevation)));
    }
    const vec3 up=vkmin_vec3_cross(right,forward),back=vkmin_vec3_scale(forward,-1);
    /* New basis * transpose(rest basis); both are orthonormal. */
    const vec3 columns[3]={vkmin_vec3_add(vkmin_vec3_scale(right,u.x),vkmin_vec3_scale(up,n.x)),
        vkmin_vec3_add(vkmin_vec3_scale(right,u.y),vkmin_vec3_scale(up,n.y)),back};
    mat4 result=vkmin_mat4_identity();
    for(unsigned column=0;column<3;++column) {
        const vec3 c=owner?omega_rotate(columns[column],false):columns[column];
        result.m[column*4]=c.x; result.m[column*4+1]=c.y; result.m[column*4+2]=c.z;
    }
    const vec3 rotated=vkmin_mat4_mul_point(result,pivot);
    const vec3 world_pivot=vkmin_vec3_add(owner?omega_rotate(pivot,false):pivot,translation);
    const vec3 shift=vkmin_vec3_sub(world_pivot,rotated);
    result.m[12]=shift.x; result.m[13]=shift.y; result.m[14]=shift.z;
    return result;
}
static vec3 omega_tip(unsigned mount,unsigned barrel) {
    const vec3 u=vkmin_vec3_cross(omega_mount_normal[mount],(vec3){0,0,1});
    return vkmin_vec3_add(omega_mount[mount],vkmin_vec3_add((vec3){0,0,-.996f},
        vkmin_vec3_add(vkmin_vec3_scale(u,barrel?.17f:-.17f),vkmin_vec3_scale(omega_mount_normal[mount],.39f))));
}
typedef struct { vec3 start,end; } omega_trajectory;
static omega_trajectory omega_projectile(unsigned ship,unsigned battery,unsigned shot,float birth) {
    const unsigned mount=4+battery;
    const mat4 pose=omega_turret(ship+1,mount,true,birth);
    const vec3 start=vkmin_mat4_mul_point(pose,omega_tip(mount,shot&1u));
    const vec3 direction={-pose.m[8],-pose.m[9],-pose.m[10]};
    const vec3 target=omega_target(ship+1,birth+(float)OMEGA_PARTICLE_FLIGHT/60.f);
    const float distance=vkmin_vec3_dot(vkmin_vec3_sub(target,start),direction);
    return (omega_trajectory){start,vkmin_vec3_add(start,vkmin_vec3_scale(direction,distance))};
}
static void omega_weapon_frame(OmegaScene *scene,uint32_t tick) {
    const float t=(float)tick/60.f;
    for(unsigned owner=0;owner<4;++owner) for(unsigned mount=0;mount<12;++mount)
        for(unsigned part=0;part<2;++part) scene->turrets[owner*24+mount*2+part]=omega_turret(owner,mount,part!=0,t);
    for(unsigned side=0;side<2;++side) {
        const vec3 hit=vkmin_vec3_add(omega_beam_contact[side],omega_formation(1,t));
        scene->beam_hit[side]=(vec4){hit.x,hit.y,hit.z,1};
    }
    for(unsigned ship=0;ship<3;++ship) for(unsigned battery=0;battery<2;++battery)
        for(unsigned shot=0;shot<3;++shot) {
            const int age=particle_age(tick,ship,shot);
            const omega_trajectory p=omega_projectile(ship,battery,shot,((float)tick-(float)age)/60.f);
            const unsigned index=ship*6+battery*3+shot;
            scene->pulse_start[index]=(vec4){p.start.x,p.start.y,p.start.z,(float)age};
            scene->pulse_end[index]=(vec4){p.end.x,p.end.y,p.end.z,0};
        }
}
#endif
