#version 450
// unlit.frag -- albedo times base colour, emissive added, fog applied. Tile
// floors, sprites-as-meshes, anything the game lights itself.
#include "lib/vkmin_lib.glsl"

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Instance inst = InstanceRef(frame.instances).i[v_instance];
    Material m = MaterialRef(frame.materials).m[inst.material];
    Surface s = surface_fetch(frame, m, v_normal, v_tangent, v_uv, gl_FrontFacing);
    if ((s.flags & VKMIN_MAT_MASKED) != 0u && s.alpha < m.alpha_cutoff) discard;
    float view_depth = -(frame.view * vec4(v_world_pos, 1.0)).z;
    vec3 color = fog(frame, s.albedo + s.emissive, view_depth);
    color = debug_color(frame, color, s, 0u, 0u, inst.id);
    write_outputs(color, (s.flags & VKMIN_MAT_BLEND) != 0u ? s.alpha : 1.0, s.N, inst.id);
}
