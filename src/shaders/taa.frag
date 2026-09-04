#version 450
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
void main() {
    Frame f=FrameRef(push.frame).frame;
    Outdoor o=OutdoorRef(f.outdoor).o;
    vec4 current=texture(TEX(push.param),v_uv);
    o_color=current;
    if (o.targets.z==0u) return;
    vec4 world=f.inv_view_proj*vec4(v_uv*2.0-1.0,1.0-current.a*0.001,1);
    world/=world.w;
    vec4 previous=o.previous_vp*world;
    if (previous.w<=0) return;
    vec3 ndc=previous.xyz/previous.w;
    vec2 uv=ndc.xy*0.5+0.5;
    if (any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) return;
    vec4 history=texture(TEX(o.targets.y),uv);
    // Depth rejection and a 3x3 neighbourhood clamp limit disocclusion trails.
    if (abs(history.a-(1.0-ndc.z)*1000.0)>max(0.003,(1.0-ndc.z)*15.0)) return;
    vec3 lo=current.rgb, hi=lo;
    for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
        vec3 value=texture(TEX(push.param),v_uv+vec2(x,y)*f.screen.zw).rgb;
        lo=min(lo,value); hi=max(hi,value);
    }
    o_color.rgb=mix(current.rgb,clamp(history.rgb,lo,hi),0.88);
}
