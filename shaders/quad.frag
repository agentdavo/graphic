#version 450
// quad.frag -- textured, tinted, premultiplied. With VKMIN_QUAD_SDF the red
// channel is a distance field: 128 is the edge, so text stays crisp at any
// size; the softness is in texels of the atlas, converted through the
// screen-space derivative so it is one pixel wide however the quad is scaled.
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in uint v_texture;
layout(location = 3) flat in uint v_flags;
layout(location = 4) in float v_softness;
layout(location = 0) out vec4 o_color;
// The world-quad pipeline masks these off, but an undeclared output leaves
// whatever the slot held (lavapipe stippled the normal target with garbage
// until they were declared); declared and written, the mask does its job.
layout(location = 1) out uint o_id;
layout(location = 2) out vec2 o_normal;

void main() {
    vec4 t = texture(TEX(v_texture), v_uv);
    float a;
    vec3 rgb;
    if ((v_flags & VKMIN_QUAD_SDF) != 0u) {
        float d = t.r - 0.5;
        float w = fwidth(t.r) * v_softness;
        a = smoothstep(-w, w, d) * v_color.a;
        rgb = v_color.rgb;
    } else {
        a = t.a * v_color.a;
        rgb = t.rgb * v_color.rgb;
    }
    o_color = vec4(rgb * a, a);
    o_id = 0u;
    o_normal = vec2(0.5);
}
