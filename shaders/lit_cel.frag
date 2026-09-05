#version 450
// lit_cel.frag -- the canonical cel shader: cel() for the sun and every local
// light, the same shadows and light lists as lit_pbr, then fog. The outline
// and colour LUT are post passes and need nothing from here but the normal.
#include "lib/vkmin_lib.glsl"

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Instance inst = InstanceRef(frame.instances).i[v_instance];
    Material m = MaterialRef(frame.materials).m[inst.material];
    Surface s = surface_fetch(frame, m, v_normal, v_tangent, v_uv, gl_FrontFacing);
    if ((s.flags & VKMIN_MAT_MASKED) != 0u && s.alpha < m.alpha_cutoff) discard;
    vec3 P = v_world_pos;
    vec3 V = normalize(frame.camera_pos.xyz - P);
    float view_depth = -(frame.view * vec4(P, 1.0)).z;
    uint cascade = cascade_for(frame, view_depth);
    bool unlit = (s.flags & VKMIN_MAT_UNLIT) != 0u;

    vec3 color = unlit ? s.albedo : frame.ambient.rgb * s.albedo * frame.shadow_tint.rgb + s.emissive;
    if (frame.sun_light != VKMIN_NONE && !unlit) {
        Light sun = LightRef(frame.lights).l[frame.sun_light];
        vec3 L = normalize(frame.sun.xyz);
        color += cel(frame, s.N, V, L, s.albedo, sun.color.rgb, sun_shadow(frame, sun, P, s.N, L, cascade));
    }
    LightList list = lights_for_pixel(frame, gl_FragCoord.xy, view_depth);
    if (!unlit) {
        for (uint k = 0u; k < list.count; ++k) {
            Light light = light_at(frame, list, k);
            vec3 L;
            float atten = light_attenuation(light, P, L);
            if (atten <= 0.0) continue;
            // Quantise the falloff too, or the ramp's bands smear back into a gradient.
            float band = step(0.15, atten * dot(light.color.rgb, vec3(0.33)));
            color += cel(frame, s.N, V, L, s.albedo, light.color.rgb * band * 0.35, light_shadow(frame, light, P, s.N, L));
        }
    }
    color = fog(frame, color, view_depth);
    color = debug_color(frame, color, s, list.clustered ? list.count : 0u, cascade, inst.id);
    write_outputs(color, (s.flags & VKMIN_MAT_BLEND) != 0u ? s.alpha : 1.0, s.N, inst.id);
}
