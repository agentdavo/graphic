// omega.glsl -- the preamble for the omega demo's own six shaders. Omega is a
// second, self-contained user of the transport contract, kept apart from the
// renderer on purpose: it uses none of shaders/lib, and nothing in lib/ may
// grow a dependency on it.
//
// It replaces the engine's push block rather than extending it. VKMIN_OWN_PUSH
// suppresses common.glsl's `Push push` and OmegaBlock takes its place, so
// every omega pipeline is created with push_size = sizeof(OmegaPush) and the
// SPIR-V size check in vkmin_make_pipeline is what holds the two ends
// together. omega_shared.h is the C/GLSL pair for that block and for the scene
// constants below; like render_shared.h it is compiled by both toolchains from
// one text, and its geometry constants (mouth Z, pylon radius, muzzle offsets)
// are read by omega.c to build the meshes and by these shaders to draw the
// light that has to land in the same places.
#define VKMIN_OWN_PUSH
#include "common.glsl"
#include "omega_shared.h"
layout(push_constant, scalar) uniform OmegaBlock { OmegaPush o; };
layout(buffer_reference, scalar) readonly buffer OmegaFrame { OmegaScene s; };
// The per-frame scene block, reached by device address out of the push block.
#define F OmegaFrame(o.frame).s
const float O_PI=3.14159265359;
/* The inverse of omega_pack_normal in omega/omega.c: two snorm16 on an
 * octahedron, folded for the lower hemisphere. Both halves of this pair have
 * to change together, so keep the comment on the C side pointing here. This
 * is the part of the contract no _Static_assert can cover -- the sizes agree
 * whatever the encoding is, so only the pair of comments guards it. */
vec3 omegaOctDecode(uint packed) {
    vec2 e=unpackSnorm2x16(packed);
    vec3 n=vec3(e.x,e.y,1.-abs(e.x)-abs(e.y));
    float fold=max(-n.z,0.);
    n.x+=n.x>=0.?-fold:fold;
    n.y+=n.y>=0.?-fold:fold;
    return normalize(n);
}
vec3 omegaOpponent() { return vec3(0,0,min(F.scene.z,-30.)-OMEGA_BATTLE_SEPARATION); }
vec3 omegaFormation(int ship) {
    return omegaOpponent()+(ship==1?vec3(45,38,-50):(ship==2?vec3(75,-26,45):vec3(0)));
}
vec3 omegaOpponentRotate(vec3 p) { return vec3(OMEGA_FORMATION_COS*p.x+OMEGA_FORMATION_SIN*p.z,p.y,-OMEGA_FORMATION_SIN*p.x+OMEGA_FORMATION_COS*p.z); }
vec3 omegaImpact(float side) { return F.beam_hit[side<0.?0:1].xyz; }
int omegaLatestShot(int ship) {
    int shot=0;
    for(int j=1;j<3;j++) if(F.pulse_start[ship*6+j].w>=0.) shot=j;
    return shot;
}
vec3 omegaParticleMuzzle(int ship,int battery) {
    return F.pulse_start[ship*6+battery*3+omegaLatestShot(ship)].xyz;
}
vec3 omegaParticleImpact(int ship,int battery) {
    return F.pulse_end[ship*6+battery*3+omegaLatestShot(ship)].xyz;
}
float omegaParticleAge(int ship,int shot) { return F.pulse_start[ship*6+shot].w; }
// The launch schedule, from the constants omega_weapons.h computes poses
// with. The shader needs only enough of it to hide a fighter still in the
// bay; omega_fury_age is the same two lines, and they change together.
float omegaFuryAge(int index) {
    if(index<OMEGA_FURY_WING || index>=OMEGA_FURY_HERO) return 1e9;
    return F.scene.x-(OMEGA_FURY_LAUNCH_START+float((index-OMEGA_FURY_WING)/2)*OMEGA_FURY_LAUNCH_INTERVAL);
}
float omegaParticleLight(int ship,bool impact) {
    float level=0.;
    for(int shot=0;shot<3;shot++) {
        float age=omegaParticleAge(ship,shot)-(impact?float(OMEGA_PARTICLE_FLIGHT):0.);
        if(age>=0.) level+=exp(-age*(impact?.32:.65));
    }
    return min(level,1.);
}
// Mouth, throat, radius scale. Closing contracts about the fixed far throat.
// Perspective compensation makes the visible contraction follow the aperture
// envelope rather than vanishing immediately down the long corridor.
vec3 omegaGateShape() {
    float a=F.scene.w;
    float nearZ=OMEGA_GATE_MOUTH_Z,farZ=OMEGA_GATE_THROAT_Z;
    if(F.scene.x<OMEGA_GATE_CLOSE_START) return vec3(nearZ,mix(nearZ,farZ,a),a);
    vec3 forward=normalize(vec3(F.vp[0][3],F.vp[1][3],F.vp[2][3])); // clip w row
    float nearDepth=dot(vec3(0,0,nearZ)-F.eye.xyz,forward);
    float farDepth=dot(vec3(0,0,farZ)-F.eye.xyz,forward);
    // Preserve the denominator sign when a reverse shot puts the gate behind
    // the camera; a positive-only clamp would expand it to a huge negative cone.
    float denominator=nearDepth+a*(farDepth-nearDepth);
    float scale=clamp(a*farDepth/(abs(denominator)>.001?denominator:(denominator<0.?-.001:.001)),0.,1.);
    return vec3(mix(farZ,nearZ,scale),farZ,scale);
}
vec4 omegaShadow(vec3 world) {
    // Follow the emerging hull so its recessed machinery keeps casting shadows
    // after it has travelled outside the gate's fixed shadow-map footprint.
    world.z-=min(F.scene.z,0.);
    vec3 light=normalize(vec3(-.35,.82,-.45)); // the hull shader's key
    vec3 right=normalize(cross(-light,vec3(0,1,0)));
    vec3 up=cross(right,-light);
    return vec4(dot(right,world)/25.,-dot(up,world)/25.,(60.-dot(light,world))/120.,1);
}
float hash21(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
vec3 omegaPlanetCentre() { return vec3(OMEGA_PLANET_X,OMEGA_PLANET_Y,OMEGA_PLANET_Z); }
// One ember of the impact spray, as a pure function of its index and the
// clock: birth staggered by a hash, so the fountain is continuous while the
// guns are firing and nothing has to be stored between frames.
// One pulse bolt, as a pure function of its index and the clock. The index
// splits into the fighter that fired it and which shot of the burst it is;
// firing follows the same three lines of attack schedule omega_fury_run uses
// on the CPU, from the same constants, because the shader cannot see a pose
// that was evaluated at a moment it no longer has.
struct OmegaBolt { vec3 centre; float live, fade; };
OmegaBolt omegaBolt(float index) {
    int fighter=int(mod(index,float(OMEGA_FURY_HERO)));
    int shot=int(index)/OMEGA_FURY_HERO;
    float since=F.scene.x-(OMEGA_FURY_ATTACK+float(fighter)*OMEGA_FURY_STAGGER);
    float laps=since/OMEGA_FURY_CYCLE;
    float phase=laps-floor(laps);
    // The muzzle is whatever omega_weapon_frame froze at the instant this
    // round left, not wherever its fighter is now. The bursts -- four rounds
    // together then a gap -- are set by the same arithmetic on the CPU side.
    vec4 fired=F.bolt[int(index)];
    vec3 muzzle=fired.xyz;
    float age=max(F.scene.x-fired.w,0.);
    // A round flies from the muzzle it left toward what it was aimed at, and
    // stops there rather than continuing to infinity.
    vec3 mark=omegaFormation(int(mod(float(fighter),3.)));
    vec3 toward=mark-muzzle;
    float range=max(length(toward)-14.,4.);
    float flown=OMEGA_BOLT_SPEED*age;
    OmegaBolt b;
    b.centre=muzzle+normalize(toward)*min(flown,range);
    b.fade=1.-age/OMEGA_BOLT_LIFE;
    // Only on the way in, and only until the round arrives: a fighter breaking
    // off is running, not shooting.
    b.live=(since>0. && phase<.45 && flown<range && age<OMEGA_BOLT_LIFE)?1.:0.;
    return b;
}
// Where a breach is, in world space. Sites are stored in hull space so they
// ride the ship; the first four belong to the hero and the last two to the
// opponent her beams are on.
vec3 omegaBreach(int site) {
    vec3 local=F.damage[site].xyz;
    return site<4?local+vec3(0,0,F.scene.z)
        :omegaOpponentRotate(local)+omegaFormation(0);
}
// One ember of a breach's cloud, as a pure function of its index and the clock.
// Births are staggered by a hash so the fountain is continuous with nothing
// stored between frames, and the cone opens outward from the wound rather than
// in one direction: a breach vents, it does not aim.
struct OmegaEmber { vec3 centre, velocity; float fade, live; };
OmegaEmber omegaEmber(float index) {
    int site=int(mod(index,float(OMEGA_DAMAGE_SITES)));
    float spread=hash21(vec2(index,7.)),around=hash21(vec2(index,19.));
    float reach=hash21(vec2(index,31.)),birth=hash21(vec2(index,53.));
    float age=fract(F.scene.x/OMEGA_EMBER_LIFE+birth)*OMEGA_EMBER_LIFE;
    float a=around*6.28318531,z=spread*2.-1.,r=sqrt(max(1.-z*z,0.));
    vec3 direction=vec3(cos(a)*r,sin(a)*r,z);
    OmegaEmber e;
    e.velocity=direction*(.85+3.4*reach);
    e.centre=omegaBreach(site)+e.velocity*age;
    e.fade=1.-age/OMEGA_EMBER_LIFE;
    e.live=step(F.damage[site].w,F.scene.x);
    return e;
}
float noise2(vec2 p) {
    vec2 i=floor(p),f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),
               mix(hash21(i+vec2(0,1)),hash21(i+1.),f.x),f.y);
}
float fbm(vec2 p) {
    float v=0.,a=.5;
    for(int k=0;k<5;k++) { v+=a*noise2(p); p=mat2(.8,-.6,.6,.8)*p*2.03+7.3; a*=.5; }
    return v;
}
// Lens flares composited over the finished scene, so they sit in front of
// the pylon trusses rather than behind them in the gate layer.
vec3 omegaContactGlow(vec2 uv,vec3 point,float level,vec3 hue) {
    if(level<.005) return vec3(0);
    vec4 clip=F.vp*vec4(point,1);
    if(clip.w<=0.) return vec3(0);
    vec2 dp=uv-(clip.xy/clip.w*.5+.5); dp.x*=F.scene.y;
    float radius=clamp(.65/clip.w,.001,.025),q=dot(dp,dp)/(radius*radius);
    return level*(vec3(4.,7.,9.)*exp(-q*2.)+hue*exp(-q*.14));
}
vec3 omegaFlares(vec2 uv) {
    vec3 color=vec3(0);
    for(int ship=0;ship<3;ship++) for(int battery=0;battery<2;battery++) for(int shot=0;shot<3;shot++) {
        int index=ship*6+battery*3+shot;
        float age=F.pulse_start[index].w;
        vec3 muzzle=F.pulse_start[index].xyz,hit=F.pulse_end[index].xyz;
        float facing=max(dot(normalize(F.eye.xyz-muzzle),normalize(hit-muzzle)),0.);
        if(age>=0.) color+=omegaContactGlow(uv,muzzle,exp(-age*.65)*facing,vec3(.08,.7,1.3));
        if(age>=float(OMEGA_PARTICLE_FLIGHT))
            color+=omegaContactGlow(uv,hit,exp(-(age-float(OMEGA_PARTICLE_FLIGHT))*.32),vec3(.12,.8,1.6));
    }
    // Compact white-hot contact and amber halo where each red beam meets armor.
    for(int side=-1;side<=1;side+=2) {
        vec4 hit=F.vp*vec4(omegaImpact(float(side)),1);
        if(hit.w>0. && F.flash>.01) {
            vec2 dp=uv-(hit.xy/hit.w*.5+.5); dp.x*=F.scene.y;
            float r=1.1/hit.w,q=dot(dp,dp)/(r*r);
            color+=F.flash*(vec3(9.,5.,1.8)*exp(-q*2.)
                +vec3(1.8,.18,.015)*exp(-q*.12));
        }
    }
    // Blue-white engine discs and soft optical halos, visible from the stern.
    // Restrict to the cleared gate and a rear view to avoid shining through hull.
    if(F.scene.x>10.5 && F.scene.z+16.<OMEGA_GATE_MOUTH_Z) {
        for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2) {
            vec3 e=vec3(float(sx)*OMEGA_ENGINE_X,float(sy)*OMEGA_ENGINE_Y,15.72+F.scene.z);
            float facing=smoothstep(.18,.55,normalize(F.eye.xyz-e).z);
            vec4 clip=F.vp*vec4(e,1);
            if(clip.w<=0. || facing<=0.) continue;
            vec2 dp=uv-(clip.xy/clip.w*.5+.5); dp.x*=F.scene.y;
            float radius=clamp(1.2/clip.w,.001,.055);
            float q=dot(dp,dp)/(radius*radius);
            color+=facing*(vec3(.18,.34,1.25)*exp(-q*.55)
                +vec3(.035,.065,.23)*exp(-q*.075));
        }
    }
    // Activation, as in the footage: a red-orange flare appears part way
    // along each spine, travels toward the far cap brightening at every fin
    // station, swells into a pink-white sphere near the far end, and the four
    // spheres merge into one central white flash before the ring opens.
    float t=F.scene.x;
    float travel=smoothstep(.25,1.75,t);
    float along=mix(.30,.92,travel);
    float swell=smoothstep(1.55,2.2,t);
    float charge=smoothstep(.15,.45,t)*(1.-smoothstep(2.45,2.8,t));
    float stationPulse=.62+.38*cos(6.2831853*along*float(OMEGA_PYLON_STATIONS));
    float radius=mix(.028,.085,swell);
    vec3 core=mix(vec3(2.4,.75,.45),vec3(6.,5.2,4.6),swell);
    for(int k=0;k<4;k++) {
        float a=float(k)*O_PI*.5;
        vec3 local=vec3(0,OMEGA_GATE_PYLON_RADIAL-1.2,mix(OMEGA_GATE_PYLON_BACK_Z,OMEGA_GATE_PYLON_Z,along));
        // Follow the pylon splay about its far cap.
        float dz=local.z-OMEGA_GATE_PYLON_Z,cs=cos(OMEGA_PYLON_SPLAY),sn=sin(OMEGA_PYLON_SPLAY);
        local=vec3(0,OMEGA_GATE_PYLON_RADIAL+(local.y-OMEGA_GATE_PYLON_RADIAL)*cs-dz*sn,
                   OMEGA_GATE_PYLON_Z+(local.y-OMEGA_GATE_PYLON_RADIAL)*sn+dz*cs);
        vec3 anchorPos=vec3(cos(a)*local.x-sin(a)*local.y,sin(a)*local.x+cos(a)*local.y,local.z);
        vec4 c=F.vp*vec4(anchorPos,1);
        if(c.w<=0.) continue;
        vec2 dp=uv-(c.xy/c.w*.5+.5); dp.x*=F.scene.y;
        float d=length(dp);
        float level=charge*mix(stationPulse,1.,swell);
        color+=level*(core*exp(-d*d/(radius*radius))
            +vec3(.7,.13,.06)*exp(-d*d/pow(radius*2.2,2.))
            +vec3(.28,.05,.03)*exp(-pow((d-radius*1.8)/(radius*.25),2.)));
    }
    // The merge: one white sphere at the mouth centre with a pink halo, a
    // horizontal streak and a faint ring, then the ring of the opening gate.
    float ignition=smoothstep(1.9,2.3,t)*(1.-smoothstep(2.4,2.9,t));
    vec4 center=F.vp*vec4(0,0,OMEGA_GATE_MOUTH_Z,1);
    vec2 delta=uv-(center.xy/center.w*.5+.5); delta.x*=F.scene.y;
    float d2=dot(delta,delta);
    color+=ignition*(vec3(14,12,10)*exp(-d2*120.)+vec3(1.2,.24,.18)*exp(-d2*16.)
        +vec3(.35,.05,.03)*exp(-pow((sqrt(d2)-.22)*22.,2.)));
    color+=ignition*(vec3(1.4,1.0,.85)*exp(-abs(delta.y)*320.-abs(delta.x)*5.)
        +vec3(.22,.10,.08)*exp(-pow((sqrt(d2)-.36)*45.,2.)));
    return color;
}
