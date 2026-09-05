/* OMEGA / THROUGH THE BLUE. Procedural geometry, materials and original audio.
 * 20-second loop: ignition, opening, emergence, short-short-long fire, collapse.
 * Space pause; R restart; M mute; A/D orbit; W/S elevation; F12 capture.
 * --headless --frame 915 --out omega.png --audio-out omega.wav
 * --audio-only --audio-out omega.wav renders the complete 20-second score.
 */
#include "vkmin.h"
#include "vkmin_math.h"
#include "sndmin.h"
#include "omega_shared.h"
#include "omega_score.h"
#include "shaders.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <threads.h>
#endif

enum { OMEGA_CAPACITY=160000, OMEGA_TICKS=1200, OMEGA_FIRST_SHOT=615, OMEGA_LAST_SHOT=1047 };
typedef struct { OmegaVertex *v; uint32_t count; float part; } omega_mesh;
typedef struct {
    sndmin_sound drone, gate, closing, cannon, cannon_long;
    sndmin_voice engine, gate_voice;
    omega_score score;
} omega_audio;
typedef struct { uint32_t age, duration; } omega_pulse;
static const float omega_pi=3.14159265359f;
static const vec4 armor={.30f,.32f,.34f,1}, dark={.12f,.14f,.16f,1};
static const vec4 steel={.43f,.44f,.42f,1}, red={.32f,.065f,.042f,1};

static void triangle(omega_mesh *m, vec3 a, vec3 b, vec3 c, vec4 color, int material) {
    VKMIN_ASSERT(m->count+3<=OMEGA_CAPACITY,"omega vertex capacity");
    const vec3 n=vkmin_vec3_normalize(vkmin_vec3_cross(vkmin_vec3_sub(b,a),vkmin_vec3_sub(c,a)));
    const vec3 points[3]={a,b,c};
    for(int i=0;i<3;++i) m->v[m->count++]=(OmegaVertex){
        .position={points[i].x,points[i].y,points[i].z,(float)material},
        .normal={n.x,n.y,n.z,m->part},.color=color};
}
static void quad(omega_mesh *m,vec3 a,vec3 b,vec3 c,vec3 d,vec4 color,int material) {
    triangle(m,a,b,c,color,material); triangle(m,a,c,d,color,material);
}
/* Chamfered octagonal extrusion. Flat faces keep the armor silhouette crisp. */
static void hull(omega_mesh *m,vec3 p,vec3 size,float bevel,vec4 color,int material) {
    const float x=size.x*.5f,y=size.y*.5f,z=size.z*.5f;
    const vec2 rim[8]={{-x+bevel,-y},{x-bevel,-y},{x,-y+bevel},{x,y-bevel},
        {x-bevel,y},{-x+bevel,y},{-x,y-bevel},{-x,-y+bevel}};
    for(int i=0;i<8;++i) {
        const int j=(i+1)%8;
        const vec3 a={p.x+rim[i].x,p.y+rim[i].y,p.z-z};
        const vec3 b={p.x+rim[j].x,p.y+rim[j].y,p.z-z};
        const vec3 c={b.x,b.y,p.z+z},d={a.x,a.y,p.z+z};
        quad(m,a,b,c,d,color,material);
        triangle(m,(vec3){p.x,p.y,p.z-z},b,a,color,material);
        triangle(m,(vec3){p.x,p.y,p.z+z},d,c,color,material);
    }
}
static void tube(omega_mesh *m,vec3 a,vec3 b,float ra,float rb,vec4 color,int material,int segments) {
    const vec3 axis=vkmin_vec3_normalize(vkmin_vec3_sub(b,a));
    const vec3 u=vkmin_vec3_normalize(vkmin_vec3_cross(axis,fabsf(axis.y)>.9f?(vec3){1,0,0}:(vec3){0,1,0}));
    const vec3 v=vkmin_vec3_cross(axis,u);
    for(int k=0;k<segments;++k) {
        const float t=2*omega_pi*(float)k/(float)segments,s=2*omega_pi*(float)(k+1)/(float)segments;
        const vec3 n=vkmin_vec3_add(vkmin_vec3_scale(u,cosf(t)),vkmin_vec3_scale(v,sinf(t)));
        const vec3 nn=vkmin_vec3_add(vkmin_vec3_scale(u,cosf(s)),vkmin_vec3_scale(v,sinf(s)));
        const vec3 p=vkmin_vec3_add(a,vkmin_vec3_scale(n,ra)),q=vkmin_vec3_add(a,vkmin_vec3_scale(nn,ra));
        const vec3 r=vkmin_vec3_add(b,vkmin_vec3_scale(nn,rb)),ss=vkmin_vec3_add(b,vkmin_vec3_scale(n,rb));
        quad(m,p,q,r,ss,color,material); triangle(m,a,q,p,color,material); triangle(m,b,ss,r,color,material);
    }
}
/* Tapered flat blade from root to tip. chord is the width direction; the
 * thickness direction follows from it, so root and tip stay parallel. */
static void blade(omega_mesh *m,vec3 root,vec3 tip,vec3 chord,float root_len,float tip_len,
                  float root_thick,float tip_thick,vec4 color,int material) {
    const vec3 axis=vkmin_vec3_normalize(vkmin_vec3_sub(tip,root));
    const vec3 c=vkmin_vec3_normalize(chord);
    const vec3 t=vkmin_vec3_normalize(vkmin_vec3_cross(axis,c));
    vec3 corner[2][4];
    for(int end=0;end<2;++end) {
        const vec3 centre=end?tip:root;
        const float half_len=(end?tip_len:root_len)*.5f,half_thick=(end?tip_thick:root_thick)*.5f;
        for(int k=0;k<4;++k) {
            const float sc=(k==0||k==3)?-half_len:half_len,st=k<2?-half_thick:half_thick;
            corner[end][k]=vkmin_vec3_add(centre,vkmin_vec3_add(vkmin_vec3_scale(c,sc),vkmin_vec3_scale(t,st)));
        }
    }
    for(int k=0;k<4;++k) {
        const int j=(k+1)%4;
        quad(m,corner[0][k],corner[0][j],corner[1][j],corner[1][k],color,material);
    }
    quad(m,corner[0][3],corner[0][2],corner[0][1],corner[0][0],color,material);
    quad(m,corner[1][0],corner[1][1],corner[1][2],corner[1][3],color,material);
}
static omega_mesh make_ship(void) {
    omega_mesh m={.v=calloc(OMEGA_CAPACITY,sizeof(OmegaVertex))};
    VKMIN_ASSERT(m.v,"omega mesh allocation");
    // Armored command section: broad vertical prow and recessed red bridge.
    hull(&m,(vec3){0,0,-6.7f},(vec3){3.8f,6.1f,4.1f},.65f,armor,0);
    hull(&m,(vec3){0,-.4f,-8.8f},(vec3){3.2f,4.4f,.4f},.7f,steel,0);
    // Layer the frame and window in front of the bow, never coplanar with it.
    hull(&m,(vec3){0,1.8f,-9.02f},(vec3){2.8f,.9f,.22f},.20f,dark,0);
    hull(&m,(vec3){0,1.8f,-9.16f},(vec3){2.4f,.58f,.10f},.15f,red,0);
    hull(&m,(vec3){0,3.12f,-6.7f},(vec3){2.3f,.55f,2.4f},.12f,dark,0);
    hull(&m,(vec3){0,3.45f,-6.5f},(vec3){1.3f,.2f,1.3f},.07f,steel,0);
    for(int side=-1;side<=1;side+=2) {
        const float x=(float)side;
        // Forward heavy laser barrels and stepped armored mantlets.
        tube(&m,(vec3){x*1.25f,.55f,-8.9f},(vec3){x*1.25f,.55f,-9.45f},.47f,.32f,dark,0,16);
        tube(&m,(vec3){x*1.25f,.55f,-9.45f},(vec3){x*1.25f,.55f,-10.0f},.23f,.19f,steel,0,16);
        tube(&m,(vec3){x*1.25f,.55f,-10.01f},(vec3){x*1.25f,.55f,-10.04f},.13f,.13f,red,4,16);
        tube(&m,(vec3){x*1.25f,.55f,-10.05f},(vec3){x*1.25f,.55f,-90.f},.042f,.034f,red,5,12);
        for(int k=0;k<3;++k) {
            tube(&m,(vec3){x*.95f,-1.7f,-8.98f+(float)k*.08f},(vec3){x*.95f,-.65f,-8.98f+(float)k*.08f},.27f,.27f,dark,0,12);
        }
        hull(&m,(vec3){x*1.94f,.3f,-6.6f},(vec3){.22f,2.4f,2.7f},.07f,dark,2);
        hull(&m,(vec3){x*2.1f,.5f,-6.6f},(vec3){.18f,.4f,1.3f},.05f,red,0);
        tube(&m,(vec3){x*.95f,3.5f,-6.6f},(vec3){x*.95f,5.7f,-6.6f},.045f,.012f,steel,0,6);
        tube(&m,(vec3){x*1.1f,-2.6f,-8.5f},(vec3){x*1.1f,-4.7f,-8.5f},.035f,.012f,steel,0,6);
        for(int k=0;k<8;++k) {
            const float z=-7.9f+(float)k*.37f;
            hull(&m,(vec3){x*1.92f,2.1f,z},(vec3){.06f,.07f,.12f},.01f,(vec4){1,.9f,.7f,1},4);
        }
    }
    // Long central spine, pressure cylinders and exposed structural longerons.
    tube(&m,(vec3){0,0,-4.6f},(vec3){0,0,7.8f},.68f,.68f,dark,2,16);
    for(int k=0;k<17;++k) {
        const float z=-4.4f+(float)k*.72f;
        tube(&m,(vec3){0,0,z},(vec3){0,0,z+.12f},.84f,.84f,steel,0,16);
    }
    for(int side=-1;side<=1;side+=2) {
        const float x=(float)side;
        for(int k=0;k<8;++k) {
            const float z=-4.4f+(float)k*1.45f;
            tube(&m,(vec3){x*.84f,-.56f,z},(vec3){x*.84f,.56f,z+1.35f},.055f,.055f,steel,0,6);
            tube(&m,(vec3){x*.84f,.56f,z},(vec3){x*.84f,-.56f,z+1.35f},.055f,.055f,steel,0,6);
        }
    }
    // Counterbalanced rotating habitat: two large rectangular drums with X ribs.
    m.part=1;
    tube(&m,(vec3){0,0,-2.1f},(vec3){0,0,3.6f},1.05f,1.05f,steel,0,24);
    for(int side=-1;side<=1;side+=2) {
        const float y=(float)side;
        hull(&m,(vec3){0,y*1.8f,.6f},(vec3){1.1f,2.4f,3.4f},.15f,dark,0);
        hull(&m,(vec3){0,y*3.65f,.6f},(vec3){4.25f,2.45f,5.4f},.36f,dark,2);
        hull(&m,(vec3){0,y*4.9f,.6f},(vec3){4.4f,.28f,5.55f},.14f,armor,0);
        for(int end=-1;end<=1;end+=2) {
            const float z=.6f+(float)end*2.77f;
            hull(&m,(vec3){0,y*3.65f,z},(vec3){4.22f,2.4f,.13f},.2f,armor,0);
            for(int panel=0;panel<2;++panel) {
                const float cx=-1.04f+(float)panel*2.08f;
                const vec3 a={cx-.93f,y*3.65f-1.02f,z+(float)end*.1f};
                const vec3 b={cx+.93f,y*3.65f+1.02f,z+(float)end*.1f};
                tube(&m,a,b,.09f,.09f,steel,0,6);
                tube(&m,(vec3){a.x,b.y,a.z},(vec3){b.x,a.y,b.z},.09f,.09f,steel,0,6);
            }
        }
        for(int k=0;k<14;++k) {
            const float z=-1.8f+(float)k*.37f;
            hull(&m,(vec3){-2.16f,y*3.65f,z},(vec3){.15f,1.9f,.075f},.02f,steel,0);
            hull(&m,(vec3){2.16f,y*3.65f,z},(vec3){.15f,1.9f,.075f},.02f,steel,0);
            for(int s=-1;s<=1;s+=2) hull(&m,(vec3){(float)s*2.25f,y*4.3f,z},
                (vec3){.04f,.055f,.13f},.01f,(vec4){.7f,.8f,1,1},3);
        }
    }
    m.part=0;
    // Reactor block and four independent armored engine nacelles.
    hull(&m,(vec3){0,0,6.7f},(vec3){3.1f,3.4f,3.5f},.5f,armor,0);
    for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2) {
        const float x=(float)sx*1.8f,y=(float)sy*1.65f;
        hull(&m,(vec3){x,y,7.2f},(vec3){1.6f,1.7f,3.7f},.34f,dark,2);
        for(int k=0;k<5;++k) hull(&m,(vec3){x,y,5.7f+(float)k*.66f},
            (vec3){1.75f,1.86f,.12f},.28f,steel,0);
        tube(&m,(vec3){x,y,8.8f},(vec3){x,y,9.4f},.70f,.59f,steel,0,16);
        tube(&m,(vec3){x,y,9.4f},(vec3){x,y,9.45f},.47f,.47f,(vec4){1,1,1,1},3,16);
        tube(&m,(vec3){x,y,9.46f},(vec3){x,y,11.8f},.35f,.02f,(vec4){.25f,.5f,1,1},3,16);
    }
    // Secondary turrets and hull equipment provide intermediate scale detail.
    for(int k=0;k<5;++k) for(int side=-1;side<=1;side+=2) {
        const float x=(float)side,z=5.4f+(float)k*.47f;
        hull(&m,(vec3){x*1.58f,.1f,z},(vec3){.20f,1.4f,.29f},.055f,dark,0);
    }
    // Brass fleet insignia on the bow, slightly proud of its recessed plate.
    hull(&m,(vec3){.68f,-1.75f,-9.04f},(vec3){.48f,.72f,.04f},.05f,dark,0);
    hull(&m,(vec3){.68f,-1.51f,-9.08f},(vec3){.34f,.09f,.03f},.02f,(vec4){.65f,.39f,.10f,1},0);
    hull(&m,(vec3){.68f,-1.79f,-9.08f},(vec3){.085f,.55f,.03f},.01f,(vec4){.65f,.39f,.10f,1},0);
    // Small four-wing escorts establish the capital ship's scale.
    m.part=2;
    const vec3 escorts[3]={{-5.2f,-3.5f,-5.5f},{5.5f,3.8f,-1.0f},{-3.9f,4.9f,4.8f}};
    for(int i=0;i<3;++i) {
        const vec3 e=escorts[i];
        hull(&m,e,(vec3){.30f,.34f,.8f},.1f,dark,0);
        hull(&m,(vec3){e.x,e.y+.12f,e.z-.3f},(vec3){.18f,.15f,.28f},.045f,(vec4){.1f,.5f,.8f,1},3);
        for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2) {
            const vec3 tip={e.x+(float)sx*.9f,e.y+(float)sy*.65f,e.z+.15f};
            tube(&m,e,tip,.085f,.04f,steel,0,5);
            hull(&m,tip,(vec3){.17f,.21f,.70f},.05f,armor,0);
            tube(&m,(vec3){tip.x,tip.y,tip.z+.36f},(vec3){tip.x,tip.y,tip.z+.55f},.067f,.018f,(vec4){.5f,.7f,1,1},3,8);
        }
    }
    // Four containment pylons run from the mouth back toward the camera, so
    // the vortex forms at their far tips and the emerging hull passes between
    // them. Each is an open box truss of four rails with a cross frame, a
    // yellow window bay and a pair of long tangential blade fins per station.
    m.part=3;
    for(int station=0;station<4;++station) {
        const uint32_t first=m.count;
        const vec4 rail={.36f,.21f,.12f,1}, frame={.30f,.29f,.27f,1}, fin={.50f,.56f,.66f,1};
        const vec4 window={.30f,.70f,1.6f,1};
        const float front=OMEGA_GATE_PYLON_Z,back=OMEGA_GATE_PYLON_BACK_Z,radial=OMEGA_GATE_PYLON_RADIAL;
        const float hw=1.0f,hh=.8f;
        for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2)
            tube(&m,(vec3){(float)sx*hw,radial+(float)sy*hh,back},(vec3){(float)sx*hw,radial+(float)sy*hh,front-1},.17f,.17f,rail,0,6);
        hull(&m,(vec3){0,radial,back-.4f},(vec3){2*hw+.4f,2*hh+.4f,.8f},.2f,dark,0);
        hull(&m,(vec3){0,radial,front-.5f},(vec3){2*hw-.2f,2*hh-.2f,1.0f},.25f,frame,0);
        for(int k=0;k<OMEGA_PYLON_STATIONS;++k) {
            const float z=back+2+(float)k*OMEGA_PYLON_STATION_SPACING;
            hull(&m,(vec3){0,radial,z},(vec3){2*hw+.5f,2*hh+.5f,.6f},.18f,frame,0);
            for(int sx=-1;sx<=1;sx+=2) {
                const float x=(float)sx;
                hull(&m,(vec3){x*(hw-.12f),radial,z+3.f},(vec3){.08f,.38f,2.2f},.02f,window,4);
                blade(&m,(vec3){x*hw,radial,z},(vec3){x*(hw+6.4f),radial,z+1.3f},
                    (vec3){0,0,1},1.5f,.45f,.34f,.10f,fin,0);
            }
        }
        // A broad inward emitter at the far cap injects energy into the mouth.
        tube(&m,(vec3){0,radial-.7f,front-.5f},(vec3){0,radial-1.5f,front-.2f},.42f,.27f,frame,0,16);
        tube(&m,(vec3){0,radial-1.5f,front-.2f},(vec3){0,radial-1.65f,front-.05f},.23f,.23f,(vec4){.5f,.8f,1,1},6,16);
        // Splay the pylon outward about its far cap, then rotate the complete
        // local assembly around the mouth, including normals.
        const float splay_c=cosf(OMEGA_PYLON_SPLAY),splay_s=sinf(OMEGA_PYLON_SPLAY);
        const float angle=(float)station*omega_pi*.5f;
        const float cs=cosf(angle),sn=sinf(angle);
        for(uint32_t i=first;i<m.count;++i) {
            const vec4 p=m.v[i].position,n=m.v[i].normal;
            const float dy=p.y-radial,dz=p.z-front;
            const vec3 sp={p.x,radial+dy*splay_c-dz*splay_s,front+dy*splay_s+dz*splay_c};
            const vec3 sn3={n.x,n.y*splay_c-n.z*splay_s,n.y*splay_s+n.z*splay_c};
            m.v[i].position.x=cs*sp.x-sn*sp.y; m.v[i].position.y=sn*sp.x+cs*sp.y; m.v[i].position.z=sp.z;
            m.v[i].normal.x=cs*sn3.x-sn*sn3.y; m.v[i].normal.y=sn*sn3.x+cs*sn3.y; m.v[i].normal.z=sn3.z;
        }
    }
    return m;
}

static float clamp01(float x) { return fmaxf(0,fminf(1,x)); }
static float smooth(float a,float b,float t) { const float s=clamp01((t-a)/(b-a)); return s*s*(3-2*s); }
/* Full-sized ship cruises through a very deep tunnel, then brakes only in the
 * final approach to the pylon tips. Position and speed are continuous. */
static float ship_position(float t) {
    const float start=OMEGA_GATE_ENTRANCE_Z-20.f,speed=220.f,brake=70.f,stop=-5.f;
    const float travel=fmaxf(0,t-4.5f),brake_time=(start-brake)/speed;
    if(travel<brake_time) return start-speed*travel;
    return stop+(brake-stop)*expf(-speed/(brake-stop)*(travel-brake_time));
}
/* Shared audiovisual rhythm: two 14-tick taps, then a 78-tick sustained beam.
 * At tick 615 the hull center reaches the mouth: half the ship is out. */
static omega_pulse cannon_pulse(uint32_t tick) {
    if(tick<OMEGA_FIRST_SHOT || tick>=OMEGA_LAST_SHOT) return (omega_pulse){0};
    const uint32_t phase=(tick-OMEGA_FIRST_SHOT)%216;
    const uint32_t start=phase>=66?66:(phase>=33?33:0);
    const uint32_t duration=start==66?78:14;
    return phase-start<duration?(omega_pulse){phase-start,duration}:(omega_pulse){0};
}
static float cannon_flash(uint32_t tick) {
    const omega_pulse pulse=cannon_pulse(tick);
    if(!pulse.duration) return 0;
    const float age=(float)pulse.age/60.f,duration=(float)pulse.duration/60.f;
    return (1-smooth(duration-.07f,duration,age))*(.90f+.10f*cosf(age*80));
}
static double seconds_now(void) {
#ifdef _WIN32
    LARGE_INTEGER counter,frequency;
    QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart/(double)frequency.QuadPart;
#else
    struct timespec ts; timespec_get(&ts,TIME_UTC);
    return (double)ts.tv_sec+(double)ts.tv_nsec*1e-9;
#endif
}
static void idle_millisecond(void) {
#ifdef _WIN32
    Sleep(1);
#else
    const struct timespec delay={0,1000000}; thrd_sleep(&delay,NULL);
#endif
}
static float thunder_roll(float t,float onset,float decay) {
    const float age=fmaxf(0,t-onset);
    return smooth(onset,onset+.045f,t)*expf(-age*decay);
}
/* Original PCM: filtered reactor noise, lightning/thunder, falling FM cannon.
 * Generated once; all mixing, spatialization, playback and WAV output use sndmin. */
static sndmin_sound sound_make(sndmin_ctx *audio,int kind) {
    const uint32_t count=SNDMIN_RATE*4u;
    float *pcm=calloc(count,sizeof(float)); if(!pcm) return (sndmin_sound){0};
    uint32_t seed=71431; float low=0,phase=0,thunder=0,body=0,air=0;
    for(uint32_t i=0;i<count;++i) {
        const float t=(float)i/(float)SNDMIN_RATE;
        seed=seed*1664525u+1013904223u;
        const float noise=(float)(seed>>8)*(2.f/16777216.f)-1;
        low+=(noise-low)*(kind>=2?.12f:.025f);
        float sample;
        if(kind==0) sample=.12f*sinf(2*omega_pi*37*t)+.065f*sinf(2*omega_pi*55*t)+low*.27f;
        else if(kind==1 || kind==4) {
            // One lightning snap, then delayed thunder fronts merge into a
            // soft rolling tail. Two low-pass stages keep the bass turbulent
            // without the persistent high-frequency crackle of the old cue.
            const bool closing=kind==4;
            const float strike=closing?.08f:.7f;
            const float age=fmaxf(0,t-strike);
            const float tail=closing?1.6f:.85f;
            air+=(noise-air)*.16f;
            body+=(noise-body)*.045f;
            thunder+=(body-thunder)*.018f;
            const float snap=smooth(strike,strike+.0015f,t)*expf(-age*85);
            const float fronts=thunder_roll(t,strike+.035f,tail)
                +.65f*thunder_roll(t,strike+.26f,tail*1.2f)
                +.48f*thunder_roll(t,strike+.63f,tail*1.3f)
                +.30f*thunder_roll(t,strike+1.05f,tail*1.5f);
            const float rolling=.75f+.16f*sinf(t*9.3f)+.09f*sinf(t*17.1f+.8f);
            const float boom=thunder_roll(t,strike+.055f,tail*1.4f);
            const float sub=sinf(2*omega_pi*(29*age+7*(1-expf(-age*3))));
            const float charge=closing?0.f:smooth(0,.6f,t)*(1-smooth(.7f,1.1f,t));
            const float envelope=smooth(0,.015f,t)*(1-smooth(closing?1.6f:3.1f,closing?2.45f:4.f,t));
            sample=envelope*(air*snap*.9f+(thunder*3.4f+body*.45f)*fronts*rolling
                +sub*boom*.48f+thunder*charge*.8f)*(closing?.75f:1.f);
        } else {
            phase+=2*omega_pi*(48+720*expf(-t*8))/(float)SNDMIN_RATE;
            const float attack=smooth(0,.005f,t),tail=expf(-t*3.5f);
            sample=attack*(sinf(phase+3.2f*sinf(phase*1.43f))*tail*.44f+low*expf(-t*5)*.6f
                +sinf(2*omega_pi*42*t)*expf(-t*2.8f)*.22f);
            if(kind==3) {
                const float sustain=attack*(1-smooth(1.23f,1.48f,t));
                sample+=sustain*(sinf(phase+2.4f*sinf(phase*1.43f))*.23f+low*.32f);
            } else sample*=1-smooth(.23f,.43f,t);
        }
        // Seamless engine loop, exact periodic tones and a short noise crossfade.
        if(kind==0) sample*=.92f+.08f*cosf(2*omega_pi*t/4);
        pcm[i]=sample;
    }
    if(kind==0) for(uint32_t i=0;i<800;++i) {
        const float mix=(float)i/800.f;
        pcm[count-800+i]=pcm[count-800+i]*(1-mix)+pcm[i]*mix;
    }
    const sndmin_sound s=sndmin_make_sound(audio,(sndmin_bytes){pcm,(size_t)count*sizeof(float)},1,SNDMIN_RATE);
    free(pcm); return s;
}
static bool audio_tick(sndmin_ctx *audio,omega_audio *a,uint32_t absolute,uint32_t tick,bool paused,bool muted,bool restart) {
    sndmin_frame(audio,&(sndmin_frame_desc){.index=absolute,.listener={-10,5,-15},.forward={.4f,-.1f,1},.up={0,1,0},
        .delay_seconds=.30f,.delay_feedback=.28f});
    if(!sndmin_bus_set(audio,SNDMIN_MASTER,paused||muted?0.f:.40f)) return false;
    if(!sndmin_bus_set(audio,SNDMIN_MUSIC,1.5f*(1-smooth(18.3f,19.9f,(float)tick/60)))) return false;
    if(absolute==0) {
        a->engine=sndmin_play(audio,&(sndmin_play_desc){.sound=a->drone,.loop=true,.voice={.gain=.35f}});
        if(!a->engine.id) return false;
    }
    if(restart || (!paused && tick==0)) {
        omega_score_stop(audio,&a->score);
        if(a->gate_voice.id) sndmin_stop(audio,a->gate_voice,.025f);
    }
    if(!paused && (tick==60 || tick==1050)) {
        a->gate_voice=sndmin_play(audio,&(sndmin_play_desc){.sound=tick==60?a->gate:a->closing,.voice={.gain=.8f}});
        if(!a->gate_voice.id) return false;
    }
    if(!paused && !omega_score_tick(audio,&a->score,tick)) return false;
    const omega_pulse pulse=cannon_pulse(tick);
    if(!paused && pulse.duration && pulse.age==0) {
        for(int side=-1;side<=1;side+=2) {
            const sndmin_voice shot=sndmin_play(audio,&(sndmin_play_desc){.sound=pulse.duration>14?a->cannon_long:a->cannon,.spatial=true,
                .voice={.gain=.85f,.position={(float)side*1.25f,.55f,-10+ship_position((float)tick/60)},.min_radius=18,.max_radius=120}});
            if(!shot.id) return false;
        }
    }
    return sndmin_ok(audio);
}

int main(int argc,char **argv) {
    const char *wav=NULL; bool offline=false,audio_only=false;
    for(int k=1;k<argc;++k) {
        if(!strcmp(argv[k],"--audio-out") && k+1<argc) { wav=argv[++k]; offline=true; }
        else if(!strcmp(argv[k],"--headless") || !strcmp(argv[k],"--frame") || !strcmp(argv[k],"--frames")) offline=true;
        else if(!strcmp(argv[k],"--audio-only")) { audio_only=true; offline=true; }
    }
#ifdef VKMIN_NO_PLATFORM
    const bool headless_build=true;
#else
    const bool headless_build=false;
#endif
    offline=offline || headless_build;
    if(headless_build && argc==1) {
        fprintf(stderr,"omega: this is the headless renderer. Open OMEGA.cmd for the live window and sound.\n");
        return 0;
    }
    sndmin_ctx *audio=sndmin_init(&(sndmin_desc){.offline=offline});
    if(!audio) return 1;
    omega_audio a={.drone=sound_make(audio,0),.gate=sound_make(audio,1),.closing=sound_make(audio,4),
        .cannon=sound_make(audio,2),.cannon_long=sound_make(audio,3),.score=omega_score_init(audio)};
    if(!a.drone.id || !a.gate.id || !a.closing.id || !a.cannon.id || !a.cannon_long.id || !omega_score_ready(a.score)) {
        sndmin_shutdown(audio); return 1;
    }
    if(audio_only) {
        bool ok=true;
        for(uint32_t tick=0;tick<OMEGA_TICKS && ok;++tick) ok=audio_tick(audio,&a,tick,tick,false,false,false);
        if(ok) ok=sndmin_render(audio,OMEGA_TICKS,wav?wav:"omega.wav",NULL);
        sndmin_shutdown(audio); return ok?0:1;
    }
    vkmin_ctx *gpu=vkmin_init(&(vkmin_desc){.argc=argc,.argv=argv,.title="OMEGA - Through the Blue",
        .width=1280,.height=720,.vsync=true,.headless=headless_build});
    omega_mesh mesh=make_ship();
    fprintf(stderr,"omega: %u triangles; 20-second sequence; Blue Circuit transit mix 150 BPM, gate and cannons\n",mesh.count/3);
    const vkmin_buffer geometry=vkmin_make_buffer(gpu,&(vkmin_buffer_desc){
        .data={mesh.v,(size_t)mesh.count*sizeof(OmegaVertex)},.label="omega procedural destroyer"});
    free(mesh.v);
    int width,height; vkmin_size(gpu,&width,&height);
    const vkmin_image hdr=vkmin_make_image(gpu,&(vkmin_image_desc){.width=width,.height=height,
        .format=VKMIN_FMT_RGBA16_FLOAT,.usage=VKMIN_IMAGE_COLOR|VKMIN_IMAGE_SAMPLED,.sampler=VKMIN_SAMPLER_LINEAR_CLAMP,.label="omega HDR"});
    const vkmin_image gate_layer=vkmin_make_image(gpu,&(vkmin_image_desc){.width=width,.height=height,
        .format=VKMIN_FMT_RGBA16_FLOAT,.usage=VKMIN_IMAGE_COLOR|VKMIN_IMAGE_SAMPLED,.sampler=VKMIN_SAMPLER_LINEAR_CLAMP,.label="omega gate veil"});
    const vkmin_image glow=vkmin_make_image(gpu,&(vkmin_image_desc){.width=width/4>0?width/4:1,.height=height/4>0?height/4:1,
        .format=VKMIN_FMT_RGBA16_FLOAT,.usage=VKMIN_IMAGE_COLOR|VKMIN_IMAGE_SAMPLED,.sampler=VKMIN_SAMPLER_LINEAR_CLAMP,.label="omega bloom"});
    const vkmin_image depth=vkmin_make_image(gpu,&(vkmin_image_desc){.width=width,.height=height,
        .format=VKMIN_FMT_D32_FLOAT,.usage=VKMIN_IMAGE_DEPTH,.label="omega depth"});
    const vkmin_image shadow=vkmin_make_image(gpu,&(vkmin_image_desc){.width=2048,.height=2048,
        .format=VKMIN_FMT_D32_FLOAT,.usage=VKMIN_IMAGE_DEPTH|VKMIN_IMAGE_SAMPLED,.sampler=VKMIN_SAMPLER_LINEAR_CLAMP,.label="omega key shadow"});
    const uint32_t shadow_index=vkmin_index(gpu,shadow),hdr_index=vkmin_index(gpu,hdr);
    const vkmin_pipeline shadow_pipe=vkmin_make_pipeline(gpu,&(vkmin_pipeline_desc){.vs=VKMIN_BYTES(omega_vert_spv),
        .fs=VKMIN_BYTES(omega_shadow_frag_spv),.push_size=sizeof(OmegaPush),.color_format=VKMIN_FMT_NONE,
        .depth=true,.depth_write=true,.cull=VKMIN_CULL_NONE,.label="omega key shadow"});
    const vkmin_pipeline ship=vkmin_make_pipeline(gpu,&(vkmin_pipeline_desc){.vs=VKMIN_BYTES(omega_vert_spv),
        .fs=VKMIN_BYTES(omega_hull_frag_spv),.push_size=sizeof(OmegaPush),.color_format=VKMIN_FMT_RGBA16_FLOAT,
        .depth=true,.depth_write=true,.cull=VKMIN_CULL_NONE,.label="omega armor and plasma"});
    const vkmin_pipeline gate=vkmin_make_pipeline(gpu,&(vkmin_pipeline_desc){.vs=VKMIN_BYTES(omega_screen_vert_spv),
        .fs=VKMIN_BYTES(omega_gate_frag_spv),.push_size=sizeof(OmegaPush),.color_format=VKMIN_FMT_RGBA16_FLOAT,
        .cull=VKMIN_CULL_NONE,.label="omega procedural jump gate"});
    const vkmin_pipeline background=vkmin_make_pipeline(gpu,&(vkmin_pipeline_desc){.vs=VKMIN_BYTES(omega_screen_vert_spv),
        .fs=VKMIN_BYTES(omega_post_frag_spv),.push_size=sizeof(OmegaPush),.color_format=VKMIN_FMT_RGBA16_FLOAT,
        .depth=true,.depth_write=false,.cull=VKMIN_CULL_NONE,.label="omega gate composite"});
    const vkmin_pipeline bloom=vkmin_make_pipeline(gpu,&(vkmin_pipeline_desc){.vs=VKMIN_BYTES(omega_screen_vert_spv),
        .fs=VKMIN_BYTES(omega_post_frag_spv),.push_size=sizeof(OmegaPush),.color_format=VKMIN_FMT_RGBA16_FLOAT,
        .cull=VKMIN_CULL_NONE,.label="omega bloom extraction"});
    const vkmin_pipeline post=vkmin_make_pipeline(gpu,&(vkmin_pipeline_desc){.vs=VKMIN_BYTES(omega_screen_vert_spv),
        .fs=VKMIN_BYTES(omega_post_frag_spv),.push_size=sizeof(OmegaPush),.cull=VKMIN_CULL_NONE,.label="omega film grade"});
    OmegaPush p={.vertices=vkmin_address(gpu,geometry),.texture_id=vkmin_index(gpu,hdr),.bloom_id=vkmin_index(gpu,glow),
        .gate_id=vkmin_index(gpu,gate_layer)};
    bool paused=false,muted=false,ok=true; uint32_t absolute=0,phase=0; float orbit=0,elevation=0;
    const double start=seconds_now();
    while(ok && vkmin_running(gpu)) {
        const vkmin_frame f=vkmin_frame_begin(gpu,NULL);
        if(!offline) while(seconds_now()<start+(double)absolute/60.) idle_millisecond();
        const uint32_t target=offline?f.index:(uint32_t)((seconds_now()-start)*60.);
        if(vkmin_key_pressed(&f.input,32)) paused=!paused;
        if(vkmin_key_pressed(&f.input,'M')) muted=!muted;
        const bool restart=vkmin_key_pressed(&f.input,'R')!=0;
        if(restart) phase=0;
        // Preserve every audio tick even when rendering isolated frames or a slow GPU.
        while(absolute<=target && ok) {
            ok=audio_tick(audio,&a,absolute,phase,paused,muted,restart && absolute==target);
            if(!paused) phase=(phase+1)%OMEGA_TICKS;
            ++absolute;
        }
        const uint32_t visual=paused?phase:(phase+OMEGA_TICKS-1)%OMEGA_TICKS;
        const float t=(float)visual/60.f;
        orbit+=.012f*((float)vkmin_key_down(&f.input,'D')-(float)vkmin_key_down(&f.input,'A'));
        elevation=fmaxf(-6,fminf(10,elevation+.18f*((float)vkmin_key_down(&f.input,'W')-(float)vkmin_key_down(&f.input,'S'))));
        // Begin the pan during approach, before the hull reaches the mouth.
        const float reveal=smooth(6.f,11.f,t);
        // Stay inside the mouth's viewing angle so the far throat remains
        // visible throughout the pan, even with the much deeper corridor.
        // Off-axis and above, far enough back that the pylons reach toward
        // the camera and the vortex sits at their tips; the reveal dollies in
        // and swings wide so the emerging hull fills the frame.
        const float camera_time=fminf(t,OMEGA_GATE_CLOSE_START);
        const float angle=-.195f-.55f*reveal+orbit+.012f*sinf(camera_time*.19f)*reveal;
        const float radius=80.f-14.f*reveal;
        const vec3 eye={sinf(angle)*radius,6.f+6.f*reveal+elevation,cosf(angle)*-radius};
        p.eye=(vec4){eye.x,eye.y,eye.z,1.4f*reveal};
        p.vp=vkmin_mat4_mul(vkmin_mat4_perspective(omega_pi/4,f.aspect,.1f,1600),
            vkmin_mat4_look_at(eye,(vec3){0,p.eye.w,1},(vec3){0,1,0}));
        p.scene=(vec4){t,f.aspect,ship_position(t),smooth(2.f,4.5f,t)*(1-smooth(OMEGA_GATE_CLOSE_START,OMEGA_GATE_CLOSE_END,t))};
        p.flash=cannon_flash(visual); p.padding=-1; p.texture_id=shadow_index;
        vkmin_barrier(gpu,&(vkmin_barrier_desc){.images=(vkmin_transition[]){{shadow,VKMIN_USE_DEPTH_TARGET}},.image_count=1});
        vkmin_pass_begin(gpu,&(vkmin_pass_desc){.depth=shadow,.clear_depth=true,.label="omega shadow map"});
        vkmin_draw(gpu,shadow_pipe,&p,mesh.count,1); vkmin_pass_end(gpu);
        vkmin_barrier(gpu,&(vkmin_barrier_desc){.images=(vkmin_transition[]){{shadow,VKMIN_USE_SAMPLED}},.image_count=1});
        p.padding=0;
        vkmin_barrier(gpu,&(vkmin_barrier_desc){.images=(vkmin_transition[]){{gate_layer,VKMIN_USE_COLOR_TARGET}},.image_count=1});
        vkmin_pass_begin(gpu,&(vkmin_pass_desc){.color=gate_layer,.clear_color=true,.label="omega gate energy"});
        vkmin_draw(gpu,gate,&p,3,1); vkmin_pass_end(gpu);
        vkmin_barrier(gpu,&(vkmin_barrier_desc){.images=(vkmin_transition[]){{gate_layer,VKMIN_USE_SAMPLED},{hdr,VKMIN_USE_COLOR_TARGET},{depth,VKMIN_USE_DEPTH_TARGET}},.image_count=3});
        vkmin_pass_begin(gpu,&(vkmin_pass_desc){.color=hdr,.depth=depth,.clear_color=true,.clear_depth=true,.clear={0,0,0,1},.label="omega HDR scene"});
        p.padding=-2; p.texture_id=p.gate_id; vkmin_draw(gpu,background,&p,3,1);
        p.padding=0; p.texture_id=shadow_index; vkmin_draw(gpu,ship,&p,mesh.count,1); vkmin_pass_end(gpu);
        vkmin_barrier(gpu,&(vkmin_barrier_desc){.images=(vkmin_transition[]){{hdr,VKMIN_USE_SAMPLED},{glow,VKMIN_USE_COLOR_TARGET}},.image_count=2});
        vkmin_pass_begin(gpu,&(vkmin_pass_desc){.color=glow,.clear_color=true,.label="omega glow"});
        p.padding=1; p.texture_id=hdr_index; vkmin_draw(gpu,bloom,&p,3,1); vkmin_pass_end(gpu);
        vkmin_barrier(gpu,&(vkmin_barrier_desc){.images=(vkmin_transition[]){{glow,VKMIN_USE_SAMPLED},{vkmin_backbuffer(gpu),VKMIN_USE_COLOR_TARGET}},.image_count=2});
        vkmin_pass_begin(gpu,&(vkmin_pass_desc){.color=vkmin_backbuffer(gpu),.depth=vkmin_default_depth(gpu),.clear_color=true,.clear_depth=true,.label="omega presentation"});
        p.padding=0; vkmin_draw(gpu,post,&p,3,1); vkmin_pass_end(gpu);
        vkmin_frame_end(gpu);
        if(vkmin_key_pressed(&f.input,301)) ok=vkmin_save_png(gpu,"omega-capture.png") && ok;
    }
    vkmin_shutdown(gpu);
    if(ok && wav) ok=sndmin_render(audio,absolute+120,wav,NULL);
    sndmin_shutdown(audio);
    return ok?0:1;
}
