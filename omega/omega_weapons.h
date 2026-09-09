/* Pure animation and firing geometry. Limits are demo mechanics, not canon
 * engineering claims: full azimuth, 0..75 degrees above the mounting plane.
 * A shot's launch pose is evaluated at its birth tick, never moved in flight. */
#ifndef OMEGA_WEAPONS_H
#define OMEGA_WEAPONS_H
#include "omega_shared.h"
#include "vkmin_math.h"
static float ship_position(float t) {
    const float start=OMEGA_GATE_ENTRANCE_Z-20.f,speed=220.f,brake=70.f,cruise=22.f,rate=3.7f;
    const float settle=12.f;
    const float travel=fmaxf(0,t-4.5f),brake_time=(start-brake)/speed;
    if(travel<brake_time) return start-speed*travel;
    const float dt=travel-brake_time;
    return brake-cruise*settle*(1-expf(-dt/settle))-(speed-cruise)/rate*(1-expf(-rate*dt));
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
/* Does a shot from here to there pass through the drum? Pure, and shared by
 * the demo and its test, because the answer is the whole reason the batteries
 * are where they are. */
static bool omega_drum_masks(vec3 from,vec3 to) {
    const vec3 d=vkmin_vec3_sub(to,from);
    const float span=vkmin_vec3_length(d);
    if(span<1e-4f) return false;
    const vec3 step=vkmin_vec3_scale(d,1.f/span);
    for(unsigned i=1;i<=600;++i) {
        const float s=span*(float)i/600.f;
        const vec3 at=vkmin_vec3_add(from,vkmin_vec3_scale(step,s));
        if(at.z>=OMEGA_DRUM_FORE && at.z<=OMEGA_DRUM_AFT
            && sqrtf(at.x*at.x+at.y*at.y)<=OMEGA_DRUM_RADIUS) return true;
    }
    return false;
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
/* Where a fighter belongs once it is done manoeuvring. Six flights of four:
 * the first four are the screen that came through the gate, the last two are
 * the wave out of the bay, stationed further ahead. */
static vec3 omega_fury_station(unsigned slot) {
    static const vec3 lead[6]={{-16.f,5.2f,-27.f},{15.f,-4.4f,-13.f},{-7.f,-9.5f,-35.f},
        {9.f,9.0f,3.f},{-23.f,-2.2f,9.f},{20.f,6.2f,-41.f}};
    const unsigned flight=slot/4u;
    /* Wingmen sit one to two spans off each other, not five. A Starfury is
     * 0.45 units across, so these offsets put a flight inside about 150 m and
     * it reads as a formation rather than four unrelated dots. */
    const float step=(float)(slot%4u),echelon=(flight&1u)?-1.4f:1.4f;
    return (vec3){lead[flight].x+step*echelon,lead[flight].y-step*.45f,lead[flight].z+step*1.6f};
}
/* Where the centrifuge is pointing. omega.vert turns the drum with the same
 * three constants; that is the only reason a hatch and its door agree. */
static float omega_habitat_angle(unsigned ship,float t) {
    return t*OMEGA_HABITAT_RATE+OMEGA_HABITAT_PHASE
        +(float)(ship?ship-1u:0u)*OMEGA_HABITAT_STAGGER;
}
/* When this fighter's pair is called away. Fighters that never sat in a bay
 * get the first slot's time and never use it; computing (index-WING)/2 for
 * them would wrap, which is exactly the kind of unsigned trap worth closing
 * once here rather than at three call sites. */
static float omega_fury_launch_time(unsigned index) {
    const unsigned pair=(index>=OMEGA_FURY_WING&&index<OMEGA_FURY_HERO)?(index-OMEGA_FURY_WING)/2u:0u;
    return OMEGA_FURY_LAUNCH_START+(float)pair*OMEGA_FURY_LAUNCH_INTERVAL;
}
/* Negative while a fighter is still in the drum, huge for one that never sat
 * in it. omegaFuryAge in omega.glsl is the same schedule from the same
 * constants: the shader needs it to hide an unlaunched fighter, and the two
 * change together or neither does. */
static float omega_fury_age(unsigned index,float t) {
    if(index<OMEGA_FURY_WING || index>=OMEGA_FURY_HERO) return 1e9f;
    return t-omega_fury_launch_time(index);
}
/* The hatch a fighter left from, frozen at the moment it left. The drum keeps
 * turning afterwards; the curve must not swing with it. A pair leaves from
 * opposite sides of the rim, so one of them simply gets the negated radius.
 * The throw is radial, with the rim's tangential velocity added, which is what
 * bends the exit into a spiral rather than a straight line. */
static void omega_fury_hatch(unsigned index,unsigned ship,vec3 *at,vec3 *out) {
    const float spin=omega_habitat_angle(ship,omega_fury_launch_time(index));
    const float side=(index&1u)?-1.f:1.f;
    const float c=cosf(spin)*side,s=sinf(spin)*side;
    *at=(vec3){OMEGA_FURY_BAY_R*c,OMEGA_FURY_BAY_R*s,OMEGA_FURY_BAY_Z};
    *out=(vec3){c*OMEGA_FURY_BAY_RUN-s*OMEGA_FURY_BAY_SPIN,
        s*OMEGA_FURY_BAY_RUN+c*OMEGA_FURY_BAY_SPIN,-OMEGA_FURY_BAY_RUN*.35f};
}
/* Which station a fighter takes. The gate escorts and the opponents' wings
 * map straight through. A launched pair is split across the two bay flights,
 * one stationed to port and one to starboard, and then each fighter takes
 * whichever of the two lies on its own side of the drum. Without that, a
 * fighter thrown to port has to cross the hull to reach a starboard station
 * and its curve passes back through the drum it just left. The launch test
 * catches that; no rendered frame ever showed it, because at this scale a
 * fighter inside the hull looks exactly like a fighter that is not there. */
static unsigned omega_fury_slot(unsigned index,unsigned ship) {
    if(index>=OMEGA_FURY_HERO) return (index-OMEGA_FURY_HERO)%OMEGA_FURY_WING;
    if(index<OMEGA_FURY_WING) return index;
    const unsigned pair=(index-OMEGA_FURY_WING)/2u;
    const unsigned mine=OMEGA_FURY_WING+(index&1u)*4u+pair;
    const unsigned other=OMEGA_FURY_WING+((index^1u)&1u)*4u+pair;
    vec3 at,out;
    omega_fury_hatch(index,ship,&at,&out);
    const vec3 a=omega_fury_station(mine),b=omega_fury_station(other);
    return (at.x*b.x+at.y*b.y)>(at.x*a.x+at.y*a.y)?other:mine;
}
/* Cubic Hermite from the hatch to the station: out of the rim sideways on the
 * spin, then straightening onto the bow heading. Everything between the two
 * tangents is the spread. */
static vec3 omega_fury_curve(unsigned index,unsigned ship,float u) {
    vec3 a,out;
    omega_fury_hatch(index,ship,&a,&out);
    const vec3 b=omega_fury_station(omega_fury_slot(index,ship));
    const float uu=u*u,uuu=uu*u;
    const float h00=2*uuu-3*uu+1,h10=uuu-2*uu+u,h01=3*uu-2*uuu,h11=uuu-uu;
    return (vec3){h00*a.x+h10*out.x+h01*b.x,h00*a.y+h10*out.y+h01*b.y,
        h00*a.z+h10*out.z+h01*b.z-h11*OMEGA_FURY_BAY_EASE};
}
/* A fighter's complete local-to-world transform: along the curve, banked into
 * its own turn, then carried by whichever Omega it belongs to. Pure -- index
 * and seconds in, a pose out -- so the launch is a function of the tick and a
 * replay reproduces it exactly. */
/* Smoothstep, local: omega_weapons.h is included by the test and the demo and
 * borrows nothing from either. */
static float omega_fury_smooth(float u) {
    const float s=u<0.f?0.f:(u>1.f?1.f:u);
    return s*s*(3.f-2.f*s);
}
/* Where a fighter is in its attack cycle, and how far it has committed to one.
 * Only the hero's wing runs: the opponents arrived with theirs already out and
 * keep them as a screen, which is also why the hero is losing. */
static float omega_fury_run(unsigned index,float t,float *phase) {
    *phase=0.f;
    if(index>=OMEGA_FURY_HERO) return 0.f;
    const float since=t-(OMEGA_FURY_ATTACK+(float)index*OMEGA_FURY_STAGGER);
    if(since<=0.f) return 0.f;
    const float laps=since/OMEGA_FURY_CYCLE;
    *phase=laps-(float)(int)laps;
    return omega_fury_smooth(since/2.2f);
}
/* One lap: out from station, through the target's flank, and a long break back.
 * Both halves are Hermites sharing the run direction as their tangent, so the
 * fighter flies straight through the pass and the loop closes smoothly. */
static vec3 omega_fury_lap(vec3 station,vec3 pass,float phase) {
    const vec3 reach=vkmin_vec3_sub(pass,station);
    const vec3 tangent=vkmin_vec3_scale(reach,1.35f);
    const bool out=phase<.5f;
    const float u=out?phase*2.f:(phase-.5f)*2.f;
    const vec3 a=out?station:pass,b=out?pass:station;
    const float uu=u*u,uuu=uu*u;
    const float h00=2*uuu-3*uu+1,h10=uuu-2*uu+u,h01=3*uu-2*uuu,h11=uuu-uu;
    return (vec3){h00*a.x+h01*b.x+(h10+h11)*tangent.x,
        h00*a.y+h01*b.y+(h10+h11)*tangent.y,
        h00*a.z+h01*b.z+(h10+h11)*tangent.z};
}
/* The flank a fighter runs through, in world space. Alternating sides and
 * staggered heights keep twenty-four of them from sharing one line. */
static vec3 omega_fury_pass(unsigned index,float t) {
    const unsigned target=1u+(index%3u);
    const float side=(index&1u)?1.f:-1.f;
    return vkmin_vec3_add(omega_formation(target,t),
        (vec3){side*26.f,((float)(index%5u)-2.f)*8.f,-16.f+(float)(index%3u)*11.f});
}
static mat4 omega_fury_pose(unsigned index,float t) {
    const unsigned ship=index<OMEGA_FURY_HERO?0u:1u+(index-OMEGA_FURY_HERO)/OMEGA_FURY_WING;
    const float age=omega_fury_age(index,t),span=OMEGA_FURY_LAUNCH_FLIGHT;
    const float raw=age>=1e8f?1.f:fminf(1.f,fmaxf(0.f,age/span));
    const float u=raw*(2.f-raw),step=.02f;
    float phase=0.f;
    const float commit=omega_fury_run(index,t,&phase);
    /* Station and launch are hull-space and get the carrier transform at the
     * end. An attack crosses to somebody else's hull, so it is world-space
     * from the start and skips that transform. */
    const vec3 seated=vkmin_vec3_add(omega_fury_curve(index,ship,u),omega_formation(ship,t));
    const vec3 flank=omega_fury_pass(index,t);
    const float lap=.0035f;
    const vec3 here=commit>0.f?omega_fury_lap(seated,flank,phase)
        :omega_fury_curve(index,ship,u);
    const vec3 ahead=commit>0.f?omega_fury_lap(seated,flank,phase+lap)
        :omega_fury_curve(index,ship,fminf(1.f,u+step));
    const vec3 behind=commit>0.f?omega_fury_lap(seated,flank,phase-lap<0.f?0.f:phase-lap)
        :omega_fury_curve(index,ship,fmaxf(0.f,u-step));
    const vec3 chord=vkmin_vec3_sub(ahead,behind);
    const vec3 forward=vkmin_vec3_length(chord)>1e-5f?vkmin_vec3_normalize(chord):(vec3){0,0,-1};
    /* Bank into the turn: the horizontal swing of the chord across the step.
     * It is a look, not aerodynamics -- there is no air out here. */
    const vec3 lead_in=vkmin_vec3_sub(here,behind),lead_out=vkmin_vec3_sub(ahead,here);
    const float swing=lead_in.x*lead_out.z-lead_in.z*lead_out.x;
    const float roll=fmaxf(-.85f,fminf(.85f,-swing*2.2f))+.20f*sinf((float)index*2.4f);
    /* An Aurora manoeuvres on vectored thrust in all six degrees of freedom:
     * it holds its nose on a target while translating sideways, which is the
     * one thing an aeroplane cannot do and the entire reason the type looks
     * like that. So through the run-in the nose tracks the target and the
     * flight path does as it pleases; facing only returns to velocity on the
     * break, when the fighter is running rather than shooting. */
    vec3 nose=forward;
    if(commit>0.f) {
        const vec3 mark=vkmin_vec3_sub(omega_formation(1u+(index%3u),t),here);
        const float aimed=commit*(1.f-omega_fury_smooth((phase-.42f)/.22f));
        if(vkmin_vec3_length(mark)>1e-4f) {
            const vec3 to=vkmin_vec3_normalize(mark);
            nose=vkmin_vec3_add(vkmin_vec3_scale(forward,1.f-aimed),vkmin_vec3_scale(to,aimed));
            if(vkmin_vec3_length(nose)>1e-4f) nose=vkmin_vec3_normalize(nose);
            else nose=forward;
        }
    }
    const vec3 back=vkmin_vec3_scale(nose,-1.f);
    const vec3 side=vkmin_vec3_cross((vec3){0,1,0},back);
    const vec3 right=vkmin_vec3_length(side)>1e-4f?vkmin_vec3_normalize(side):(vec3){1,0,0};
    const vec3 up=vkmin_vec3_cross(back,right);
    const float cr=cosf(roll),sr=sinf(roll);
    const vec3 columns[3]={vkmin_vec3_add(vkmin_vec3_scale(right,cr),vkmin_vec3_scale(up,sr)),
        vkmin_vec3_sub(vkmin_vec3_scale(up,cr),vkmin_vec3_scale(right,sr)),back};
    mat4 result=vkmin_mat4_identity();
    for(unsigned column=0;column<3;++column) {
        const vec3 c=(ship&&commit<=0.f)?omega_rotate(columns[column],false):columns[column];
        result.m[column*4]=c.x; result.m[column*4+1]=c.y; result.m[column*4+2]=c.z;
    }
    const vec3 seat=commit>0.f?here
        :vkmin_vec3_add(ship?omega_rotate(here,false):here,omega_formation(ship,t));
    result.m[12]=seat.x; result.m[13]=seat.y; result.m[14]=seat.z;
    return result;
}
static vec3 omega_tip(unsigned mount,unsigned barrel) {
    const vec3 u=vkmin_vec3_cross(omega_mount_normal[mount],(vec3){0,0,1});
    return vkmin_vec3_add(omega_mount[mount],vkmin_vec3_add((vec3){0,0,-.996f},
        vkmin_vec3_add(vkmin_vec3_scale(u,barrel?.17f:-.17f),vkmin_vec3_scale(omega_mount_normal[mount],.39f))));
}
typedef struct { vec3 start,end; } omega_trajectory;
static omega_trajectory omega_projectile(unsigned ship,unsigned battery,unsigned shot,float birth) {
    const unsigned mount=battery?OMEGA_BATTERY_B:OMEGA_BATTERY_A;
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
    for(unsigned fury=0;fury<OMEGA_FURY_TOTAL;++fury) scene->fury[fury]=omega_fury_pose(fury,t);
    /* Where the hull opens, and when. The first three are where the opponents'
     * pulses were ray-cast onto the hero, the fourth is the second half's
     * punishment offset along the same frame, and the last two are where her
     * own beams land on whoever she is shooting. */
    /* Every round in flight, frozen at the muzzle it actually left. A bolt
     * whose origin tracks its fighter's current pose is dragged sideways for
     * its whole life and the burst bends; the capital ships already solve this
     * by baking pulse_start at the birth tick, and this is the same trick for
     * seventy-two more guns. The pose function is pure, so evaluating it at a
     * past instant costs nothing but arithmetic. */
    for(unsigned round=0;round<OMEGA_BOLT_COUNT;++round) {
        const unsigned fighter=round%OMEGA_FURY_HERO,shot=round/OMEGA_FURY_HERO;
        const float begin=OMEGA_FURY_ATTACK+(float)fighter*OMEGA_FURY_STAGGER;
        const float burst=(float)(shot/4u),within=(float)(shot%4u);
        const float stagger=burst*(OMEGA_BOLT_LIFE*.30f)+within*.014f;
        const float since=t-begin;
        const float age=fmodf(fmaxf(since,0.f)+stagger,OMEGA_BOLT_LIFE);
        const float fired=t-age;
        const mat4 pose=omega_fury_pose(fighter,fired);
        const vec3 muzzle=vkmin_mat4_mul_point(pose,
            (vec3){0,OMEGA_BOLT_MUZZLE_Y,OMEGA_BOLT_MUZZLE_Z});
        scene->bolt[round]=(vec4){muzzle.x,muzzle.y,muzzle.z,fired};
    }
    static const float opened[OMEGA_DAMAGE_SITES]={
        14.8f,17.6f,21.2f,26.4f,33.0f,41.5f,   /* hers, as the fight goes on */
        10.6f,11.9f,28.0f,38.5f};              /* theirs, under her beams */
    static const vec3 spread[OMEGA_DAMAGE_SITES]={
        {0,0,0},{0,0,0},{0,0,0},{.8f,-.6f,2.6f},{-1.4f,.9f,-4.2f},{1.9f,.4f,6.1f},
        {0,0,0},{0,0,0},{-1.1f,1.3f,3.4f},{1.6f,-.9f,-2.8f}};
    for(unsigned site=0;site<OMEGA_DAMAGE_SITES;++site) {
        const vec3 anchor=site<3?omega_pulse_contact[site]
            :(site<6?omega_pulse_contact[site-3]
            :(site<8?omega_beam_contact[site-6]:omega_beam_contact[site-8]));
        const vec3 at=vkmin_vec3_add(anchor,spread[site]);
        scene->damage[site]=(vec4){at.x,at.y,at.z,opened[site]};
    }
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
