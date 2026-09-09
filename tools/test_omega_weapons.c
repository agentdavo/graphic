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
            const unsigned index=ship*6+battery*3+shot;
            /* Same mount omega_projectile picks, not a second copy of the
               rule: keeping its own made this check stale the moment the
               batteries moved clear of the drum. */
            const unsigned mount=battery?OMEGA_BATTERY_B:OMEGA_BATTERY_A;
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
        /* The wings. A fighter that never sat in a bay is on station at every
         * tick; a launched one starts on the drum's rim, is never afterwards
         * back inside the drum it left, and arrives exactly on station. The
         * clearance check is the one that matters: an earlier hatch radius put
         * the first second of every launch inside the hull. */
        for(unsigned index=0;index<OMEGA_FURY_TOTAL;++index) {
            const mat4 pose=frame.fury[index];
            const vec3 x={pose.m[0],pose.m[1],pose.m[2]},y={pose.m[4],pose.m[5],pose.m[6]};
            const vec3 z={pose.m[8],pose.m[9],pose.m[10]};
            CHECK(fabsf(vkmin_vec3_length(x)-1)<.0001f);
            CHECK(fabsf(vkmin_vec3_length(y)-1)<.0001f);
            CHECK(fabsf(vkmin_vec3_length(z)-1)<.0001f);
            CHECK(fabsf(vkmin_vec3_dot(x,y))<.0001f);
            CHECK(vkmin_vec3_dot(vkmin_vec3_cross(x,y),z)>.9999f);
            const unsigned ship=index<OMEGA_FURY_HERO?0u:1u+(index-OMEGA_FURY_HERO)/OMEGA_FURY_WING;
            const float t=(float)tick/60.f;
            const vec3 world={pose.m[12],pose.m[13],pose.m[14]};
            float phase=0.f;
            const float commit=omega_fury_run(index,t,&phase);
            if(commit>0.f) {
                /* Committed to a run, which is world space rather than its
                 * carrier's. Only the hero's wing runs, so ship is 0 here and
                 * the station needs no rotation. What is worth pinning is that
                 * the lap closes: it leaves station, reaches the target's flank
                 * at the half, and returns. An open loop would walk a fighter
                 * out of the engagement one lap at a time and nothing on screen
                 * would say so until it had already gone. */
                const vec3 seated=vkmin_vec3_add(omega_fury_curve(index,ship,1.f),omega_formation(ship,t));
                const vec3 flank=omega_fury_pass(index,t);
                if(phase<.003f) CHECK(vkmin_vec3_length(vkmin_vec3_sub(world,seated))<3.f);
                if(phase>.497f && phase<.503f) CHECK(vkmin_vec3_length(vkmin_vec3_sub(world,flank))<3.f);
                CHECK(vkmin_vec3_length(vkmin_vec3_sub(world,seated))<600.f);
                continue;
            }
            vec3 local=vkmin_vec3_sub(world,omega_formation(ship,t));
            if(ship) local=omega_rotate(local,true);
            const vec3 station=omega_fury_station(omega_fury_slot(index,ship));
            const float age=omega_fury_age(index,t);
            const float radius=sqrtf(local.x*local.x+local.y*local.y);
            if(age>=1e8f) {
                CHECK(vkmin_vec3_length(vkmin_vec3_sub(local,station))<.002f);
            } else if(age<=0.f) {
                CHECK(fabsf(radius-OMEGA_FURY_BAY_R)<.002f);
                CHECK(fabsf(local.z-OMEGA_FURY_BAY_Z)<.002f);
            } else {
                CHECK(radius>=OMEGA_FURY_BAY_R-.002f);
                if(age>=OMEGA_FURY_LAUNCH_FLIGHT)
                    CHECK(vkmin_vec3_length(vkmin_vec3_sub(local,station))<.002f);
            }
        }
    }
    /* No battery may fire through its own habitat drum. Eight of the twelve
     * mounts are masked toward anything ahead, and the two that used to fire
     * were both among them -- every pulse in the demo left through the drum.
     * The drum is a body of revolution, so turning it never opens a lane; the
     * only remedy is to fire from a mount that has one. Elevation is no escape
     * either: the angles that clear it are 35 to 66 degrees, at which the round
     * misses. These ships shoot at the hero, so the line is checked from the
     * firing ship's own hull frame. */
    for(unsigned battery=0;battery<2;++battery) {
        const unsigned mount=battery?OMEGA_BATTERY_B:OMEGA_BATTERY_A;
        const vec3 muzzle=omega_tip(mount,0);
        for(unsigned owner=1;owner<4;++owner) {
            const vec3 mark=omega_rotate(vkmin_vec3_sub(omega_formation(0,20.f),
                omega_formation(owner,20.f)),true);
            CHECK(!omega_drum_masks(muzzle,mark));
        }
    }
    /* A pair leaves from opposite sides of the rim at the same moment, one
     * second after the pair before it. Nothing else balances a spinning drum. */
    for(unsigned pair=0;pair<OMEGA_FURY_LAUNCH/2u;++pair) {
        const unsigned even=OMEGA_FURY_WING+pair*2u;
        vec3 a,a_out,b,b_out;
        omega_fury_hatch(even,0,&a,&a_out);
        omega_fury_hatch(even+1u,0,&b,&b_out);
        CHECK(fabsf(a.x+b.x)<.0001f && fabsf(a.y+b.y)<.0001f && fabsf(a.z-b.z)<.0001f);
        CHECK(fabsf(omega_fury_launch_time(even)-omega_fury_launch_time(even+1u))<.0001f);
        CHECK(fabsf(omega_fury_launch_time(even)-OMEGA_FURY_LAUNCH_START
            -(float)pair*OMEGA_FURY_LAUNCH_INTERVAL)<.0001f);
    }
    printf("omega weapons: %u ticks, rigid poses, mechanical limits, muzzle alignment,"
        " immutable flight, 72 fighter launches and their attack runs passed\n",
        (unsigned)OMEGA_SEQUENCE_TICKS);
    return EXIT_SUCCESS;
}
