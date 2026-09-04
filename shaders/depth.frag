#version 450
// depth.frag -- alpha test for masked materials. Always samples: the branch
// is on the result, not on whether to do the work.
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_material;

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Material m = MaterialRef(frame.materials).m[v_material];
    uint tex = m.albedo_tex == VKMIN_NONE ? 0u : m.albedo_tex;
    float alpha = texture(TEX(tex), v_uv).a * m.base_color.a;
    if ((m.flags & VKMIN_MAT_MASKED) != 0u && alpha < m.alpha_cutoff) discard;
}
