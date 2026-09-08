#include "omega_weapons.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return EXIT_FAILURE; } } while(0)
int main(void) {
    const mat4 rest=omega_turret(1,4,true,11.f),aimed=omega_turret(1,4,true,13.5f);
    CHECK(fabsf(rest.m[8]-aimed.m[8])>.2f); /* acquisition must visibly traverse */
    for(uint32_t tick=0;tick<OMEGA_SEQUENCE_TICKS;++tick) {
        OmegaScene frame={0}; omega_weapon_frame(&frame,tick);
        for(unsigned owner=0;owner<4;++owner) for(unsigned mount=0;mount<12;++mount) {
            const mat4 pose=frame.turrets[owner*24+mount*2+1];
            const vec3 x={pose.m[0],pose.m[1],pose.m[2]},y={pose.m[4],pose.m[5],pose.m[6]},z={pose.m[8],pose.m[9],pose.m[10]};
            CHECK(fabsf(vkmin_vec3_length(x)-1)<.00001f);
            CHECK(fabsf(vkmin_vec3_length(y)-1)<.00001f);
            CHECK(fabsf(vkmin_vec3_length(z)-1)<.00001f);
            CHECK(fabsf(vkmin_vec3_dot(x,y))<.00001f);
            CHECK(vkmin_vec3_dot(vkmin_vec3_cross(x,y),z)>.99999f);
            const vec3 normal=owner?omega_rotate(omega_mount_normal[mount],false):omega_mount_normal[mount];
            CHECK(-vkmin_vec3_dot(z,normal)>=-.00001f); /* no depression through hull */
            CHECK(-vkmin_vec3_dot(z,normal)<=.965927f); /* 75 degree stop */
        }
        for(unsigned ship=0;ship<3;++ship) for(unsigned battery=0;battery<2;++battery) for(unsigned shot=0;shot<3;++shot) {
            const int age=particle_age(tick,ship,shot);
            if(age<0 || age>OMEGA_PARTICLE_FLIGHT) continue;
            const unsigned index=ship*6+battery*3+shot,mount=4+battery;
            const float birth=((float)tick-(float)age)/60.f;
            const mat4 pose=omega_turret(ship+1,mount,true,birth);
            const omega_trajectory path=omega_projectile(ship,battery,shot,birth);
            const vec3 tip=vkmin_mat4_mul_point(pose,omega_tip(mount,shot&1u));
            CHECK(vkmin_vec3_length(vkmin_vec3_sub(tip,path.start))<.00001f);
            const vec3 direction=vkmin_vec3_normalize(vkmin_vec3_sub(path.end,path.start));
            CHECK(vkmin_vec3_dot(direction,(vec3){-pose.m[8],-pose.m[9],-pose.m[10]})>.99999f);
            CHECK(fabsf(frame.pulse_start[index].z-path.start.z)<.00001f);
            if(age<OMEGA_PARTICLE_FLIGHT) {
                OmegaScene next={0}; omega_weapon_frame(&next,tick+1);
                CHECK(fabsf(next.pulse_start[index].z-frame.pulse_start[index].z)<.0001f);
                CHECK(fabsf(next.pulse_end[index].z-frame.pulse_end[index].z)<.0001f);
            }
        }
        for(unsigned side=0;side<2;++side) {
            CHECK(fabsf(frame.beam_hit[side].x-(side?OMEGA_MUZZLE_X:-OMEGA_MUZZLE_X))<.00001f);
            CHECK(fabsf(frame.beam_hit[side].y-OMEGA_MUZZLE_Y)<.00001f);
        }
    }
    puts("omega weapons: 1800 ticks, rigid poses, mechanical limits, muzzle alignment and immutable flight passed");
    return EXIT_SUCCESS;
}
