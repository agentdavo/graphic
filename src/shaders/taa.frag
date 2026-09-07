#version 450
// taa.frag -- reprojected temporal accumulation. There is no motion vector
// target: the world position is reconstructed from the depth the input carries
// in its alpha and re-projected through Outdoor.previous_vp, which costs one
// matrix instead of an attachment, at the price of being wrong for anything
// that moved under its own power. Every early return leaves o_color at the
// current frame, so failing to find history degrades to no TAA rather than to
// a smear.
//
// That alpha channel is (1 - ndc.z) * 1000, a depth key written by the passes
// upstream (see water.frag), and it is also what the history rejection below
// compares against. Colour and depth share one attachment because the outdoor
// path has no spare target for the key.
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
