#version 450
// lit_pbr.frag -- the canonical PBR forward shader: sun with cascaded shadows,
// local lights from the cluster list (or all of them on the reference path),
// constant ambient, emissive, fog. Everything The Corridor and the shooter use.
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

    vec3 color = unlit ? s.albedo : frame.ambient.rgb * s.albedo + s.emissive;
    if (frame.sun_light != VKMIN_NONE && !unlit) {
        Light sun = LightRef(frame.lights).l[frame.sun_light];
        vec3 L = normalize(frame.sun.xyz);
        float shadow = sun_shadow(frame, sun, P, s.N, L, cascade);
        color += pbr(s.N, V, L, s.albedo, s.metallic, s.roughness) * sun.color.rgb * shadow;
    }
    LightList list = lights_for_pixel(frame, gl_FragCoord.xy, view_depth);
    if (!unlit) {
        for (uint k = 0u; k < list.count; ++k) {
            Light light = light_at(frame, list, k);
            vec3 L;
            float atten = light_attenuation(light, P, L);
            if (atten <= 0.0) continue;
            float shadow = light_shadow(frame, light, P, s.N, L);
            color += pbr(s.N, V, L, s.albedo, s.metallic, s.roughness) * light.color.rgb * atten * shadow;
        }
    }
    color = fog(frame, color, view_depth);
    color = debug_color(frame, color, s, list.clustered ? list.count : 0u, cascade, inst.id);
    if (frame.debug_mode == VKMIN_DEBUG_OVERDRAW) { write_outputs(vec3(0.12, 0.10, 0.06), 0.0, s.N, inst.id); o_color = vec4(0.12, 0.10, 0.06, 0.0); return; }
    write_outputs(color, (s.flags & VKMIN_MAT_BLEND) != 0u ? s.alpha : 1.0, s.N, inst.id);
}
