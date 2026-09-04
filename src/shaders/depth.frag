#version 450
// depth.frag -- alpha test for masked materials. Always samples: the branch
// is on the result, not on whether to do the work.
#include "common.glsl"
#include "lib/outdoor.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in uint v_material;
layout(location = 2) flat in uint v_instance;

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Instance inst = InstanceRef(frame.instances).i[v_instance];
    if (push.view == 0u && (inst.flags & VKMIN_INST_TREE) != 0u && coverage_noise(gl_FragCoord.xy) >= 1.0-uintBitsToFloat(inst.pad1)) discard;
    Material m = MaterialRef(frame.materials).m[v_material];
    uint tex = m.albedo_tex == VKMIN_NONE ? 0u : m.albedo_tex;
    float alpha = texture(TEX(tex), v_uv).a * m.base_color.a;
    if ((m.flags & VKMIN_MAT_MASKED) != 0u && alpha < m.alpha_cutoff) discard;
}
