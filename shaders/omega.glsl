#define VKMIN_OWN_PUSH
#include "common.glsl"
#include "../demo/omega_shared.h"
layout(push_constant, scalar) uniform OmegaBlock { OmegaPush o; };
layout(buffer_reference, scalar) readonly buffer OmegaFrame { OmegaScene s; };
#define F OmegaFrame(o.frame).s
const float O_PI=3.14159265359;
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
    float scale=a*farDepth/max(nearDepth+a*(farDepth-nearDepth),.001);
    return vec3(mix(farZ,nearZ,scale),farZ,scale);
}
vec4 omegaShadow(vec3 world) {
    vec3 light=normalize(vec3(-.6,.9,-.65));
    vec3 right=normalize(cross(-light,vec3(0,1,0)));
    vec3 up=cross(right,-light);
    return vec4(dot(right,world)/25.,-dot(up,world)/25.,(60.-dot(light,world))/120.,1);
}
float hash21(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
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
vec3 omegaFlares(vec2 uv) {
    vec3 color=vec3(0);
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
