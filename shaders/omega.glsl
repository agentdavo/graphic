#define VKMIN_OWN_PUSH
#include "common.glsl"
#include "../demo/omega_shared.h"
layout(push_constant, scalar) uniform OmegaBlock { OmegaPush o; };
const float O_PI=3.14159265359;
// Mouth, throat, radius scale. Closing contracts about the fixed far throat.
// Perspective compensation makes the visible contraction follow the aperture
// envelope rather than vanishing immediately down the long corridor.
vec3 omegaGateShape() {
    float a=o.scene.w;
    float nearZ=OMEGA_GATE_MOUTH_Z,farZ=OMEGA_GATE_ENTRANCE_Z;
    if(o.scene.x<OMEGA_GATE_CLOSE_START) return vec3(nearZ,mix(nearZ,farZ,a),a);
    vec3 forward=normalize(vec3(0,o.eye.w,1)-o.eye.xyz);
    float nearDepth=dot(vec3(0,0,nearZ)-o.eye.xyz,forward);
    float farDepth=dot(vec3(0,0,farZ)-o.eye.xyz,forward);
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
