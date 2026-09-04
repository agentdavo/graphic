#version 450
#include "common.glsl"
#include "lib/outdoor.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(location=1) out uint o_id;
layout(location=2) out vec2 o_normal;
void main() {
    Frame f = FrameRef(push.frame).frame;
    vec4 farPoint = f.inv_view_proj*vec4(v_uv*2.0-1.0,1,1);
    vec3 ray = normalize(farPoint.xyz/farPoint.w-f.camera_pos.xyz);
    o_color = vec4(outdoor_sky(OutdoorRef(f.outdoor).o,ray,f.sun.xyz,f.frame_index),1);
    o_id = 0u; o_normal = vec2(0.5);
}
