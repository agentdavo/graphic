#version 450
#include "common.glsl"
#include "lib/outdoor.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
vec3 world_at(Frame f,vec2 uv,float depth) {
    vec4 h=f.inv_view_proj*vec4(uv*2.0-1.0,depth,1);
    return h.xyz/h.w;
}
// Two phases are half a cycle apart. Each phase has zero weight as its UV
// resets, so the advected normal never jumps when fract wraps.
vec2 flow_normal(uint tex,vec2 uv,vec2 flow,float t) {
    float a=fract(t), b=fract(t+0.5);
    vec2 n0=texture(TEX(tex),uv-flow*a).rg*2.0-1.0;
    vec2 n1=texture(TEX(tex),uv-flow*b).rg*2.0-1.0;
    float weight=abs(a*2.0-1.0);
    return mix(n0,n1,weight);
}
void main() {
    Frame f=FrameRef(push.frame).frame;
    Outdoor o=OutdoorRef(f.outdoor).o;
    float depth=texture(TEX(f.depth_tex),v_uv).r;
    vec3 background=texture(TEX(o.targets.x),v_uv).rgb;
    o_color=vec4(background,(1.0-depth)*1000.0);
    vec3 ray=normalize(world_at(f,v_uv,1)-f.camera_pos.xyz);
    if (abs(ray.y)<0.00001) return;
    float distanceToWater=(o.height.z-f.camera_pos.y)/ray.y;
    if (distanceToWater<=0.0) return;
    vec3 P=f.camera_pos.xyz+ray*distanceToWater;
    vec2 mapUV=terrain_uv(o,P.xz);
    if (any(lessThan(mapUV,vec2(0))) || any(greaterThan(mapUV,vec2(1)))) return;
    vec4 clip=f.view_proj*vec4(P,1);
    float waterDepth=clip.z/clip.w;
    if (waterDepth>=depth || waterDepth>=1.0) return;
    vec3 behind=world_at(f,v_uv,depth);
    float thickness=max(length(behind-P),0.0);
    float bankDepth=max(P.y-behind.y,0.0);
    vec2 flow=texture(TEX(o.maps.z),mapUV).rg*2.0-1.0;
    float t=float(f.frame_index)/60.0*o.water.z;
    vec2 uv=P.xz*o.water.w;
    vec2 ripple=flow_normal(o.water_maps.x,uv,flow,t)+flow_normal(o.water_maps.y,uv*1.37,flow,t*0.79);
    ripple+=wind(P,f.frame_index).xz*0.1*o.weather.y;
    vec3 N=normalize(vec3(ripple.x*0.12,1,ripple.y*0.12));
    vec2 refractUV=clamp(v_uv+N.xz*min(thickness,2.0)*0.012,vec2(0.001),vec2(0.999));
    // Refuse refraction samples from a rock/shore in front of the water.
    if (texture(TEX(f.depth_tex),refractUV).r<waterDepth) refractUV=v_uv;
    vec3 refraction=texture(TEX(o.targets.x),refractUV).rgb;
    vec3 transmission=exp(-vec3(0.45,0.13,0.095)*thickness*o.water.x);
    vec3 under=refraction*transmission+vec3(0.018,0.15,0.12)*(1.0-transmission);
    float fresnel=0.02+0.98*pow(1.0-max(dot(N,-ray),0.0),5.0);
    vec3 reflection=outdoor_sky(o,reflect(ray,N),f.sun.xyz,f.frame_index);
    float foam=(1.0-smoothstep(0.0,o.water.y,bankDepth))*smoothstep(0.1,0.7,texture(TEX(o.water_maps.x),uv*2.0+flow*t).b);
    vec3 color=mix(under,reflection,fresnel);
    color=mix(color,vec3(0.85,0.91,0.86),foam*0.7);
    o_color=vec4(aerial(o,color,P,f.camera_pos.xyz,f.sun.xyz),(1.0-waterDepth)*1000.0);
}
