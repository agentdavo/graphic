#version 450
// 03..06: pull an ExVertex by device address, transform by the push mvp.
#define VKMIN_OWN_PUSH
#include "common.glsl"
layout(buffer_reference, scalar) readonly buffer ExVertexRef { ExVertex v[]; };
layout(push_constant, scalar) uniform ExBlock { ExPush ex; } ;
layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_uv;
void main() {
    ExVertex v = ExVertexRef(ex.vertices).v[gl_VertexIndex];
    gl_Position = ex.mvp * vec4(v.px, v.py, v.pz, 1.0);
    v_color = rgba8_decode(v.color).rgb;
    v_uv = vec2(v.u, v.v);
}
