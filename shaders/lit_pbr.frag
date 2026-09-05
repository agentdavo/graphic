#version 450
// lit_pbr.frag -- the canonical PBR forward shader: sun with cascaded shadows,
// local lights from the cluster list (or all of them on the reference path),
// constant ambient, emissive, fog. Everything The Corridor and the shooter use.
#include "lib/vkmin_lib.glsl"

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Instance inst = InstanceRef(frame.instances).i[v_instance];
    if ((inst.flags & VKMIN_INST_TREE) != 0u && coverage_noise(gl_FragCoord.xy) >= 1.0-uintBitsToFloat(inst.pad1)) discard;
    Material m = MaterialRef(frame.materials).m[inst.material];
    Surface s = surface_fetch(frame, m, v_normal, v_tangent, v_uv, gl_FrontFacing);
    if ((s.flags & VKMIN_MAT_MASKED) != 0u && s.alpha < m.alpha_cutoff) discard;
    vec3 P = v_world_pos;
    if (frame.outdoor != uint64_t(0) && (m.flags & VKMIN_MAT_TERRAIN) != 0u) {
        Outdoor o = OutdoorRef(frame.outdoor).o;
        vec4 weights = max(texture(TEX(o.maps.y),terrain_uv(o,P.xz)),vec4(0));
        weights /= max(dot(weights,vec4(1)),0.001);
        vec3 albedo = vec3(0), normal = vec3(0);
        for (uint k=0u; k<4u; ++k) {
            vec2 uv = P.xz*o.weather.z;
            albedo += texture(TEX(o.albedo[k]),uv).rgb*weights[k];
            normal += (texture(TEX(o.normals[k]),uv).xyz*2.0-1.0)*weights[k];
        }
        float macro = texture(TEX(o.maps.w),P.xz*.012).r;
        s.albedo = albedo*mix(.72,1.15,macro);
        vec3 N = normalize(v_normal), T = normalize(vec3(1,0,0)-N*N.x);
        s.N = normalize(T*normal.x+cross(N,T)*normal.y+N*max(normal.z,0.1));
    }
    // Root occlusion and pigment variation remain stable as cells enter/leave.
    if ((inst.flags & VKMIN_INST_GRASS) != 0u) {
        float pigment = hash_unit(inst.pad0*747796405u);
        s.albedo *= mix(vec3(0.48,0.65,0.42),vec3(1.08,1.0,0.65),pigment);
        s.albedo *= mix(0.32,1.0,smoothstep(0.0,0.8,v_uv.y));
    }
    vec3 V = normalize(frame.camera_pos.xyz - P);
    float view_depth = -(frame.view * vec4(P, 1.0)).z;
    uint cascade = cascade_for(frame, view_depth);
    bool unlit = (s.flags & VKMIN_MAT_UNLIT) != 0u;

    vec3 color = unlit ? s.albedo : frame.ambient.rgb * s.albedo + s.emissive;
    if (frame.outdoor != uint64_t(0) && !unlit) {
        vec3 zenith = sky_radiance(vec3(0,1,0),frame.sun.xyz);
        vec3 horizon = sky_radiance(normalize(vec3(1,0.02,1)),frame.sun.xyz);
        color = mix(horizon*0.56,zenith*1.2,s.N.y*0.5+0.5)*s.albedo+s.emissive;
    }
    if (frame.sun_light != VKMIN_NONE && !unlit) {
        Light sun = LightRef(frame.lights).l[frame.sun_light];
        vec3 L = normalize(frame.sun.xyz);
        float shadow = sun_shadow(frame, sun, P, s.N, L, cascade);
        if (frame.outdoor != uint64_t(0)) {
            // The last cascade covers a finite region; fade before its edge.
            shadow = mix(shadow,1.0,smoothstep(frame.cascade_splits.w*.8,frame.cascade_splits.w,view_depth));
            shadow *= cloud_shadow(OutdoorRef(frame.outdoor).o,P,L,frame.frame_index);
        }
        color += pbr(s.N, V, L, s.albedo, s.metallic, s.roughness) * sun.color.rgb * shadow;
        if ((inst.flags & (VKMIN_INST_GRASS | VKMIN_INST_LEAF)) != 0u)
            color += s.albedo*sun.color.rgb*shadow*(0.12+0.12*max(dot(-s.N,L),0.0));
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
    if (frame.outdoor != uint64_t(0)) color = aerial(OutdoorRef(frame.outdoor).o,color,P,frame.camera_pos.xyz,frame.sun.xyz);
    color = debug_color(frame, color, s, list.clustered ? list.count : 0u, cascade, inst.id);
    if (frame.debug_mode == VKMIN_DEBUG_OVERDRAW) { write_outputs(vec3(0.12, 0.10, 0.06), 0.0, s.N, inst.id); o_color = vec4(0.12, 0.10, 0.06, 0.0); return; }
    write_outputs(color, (s.flags & VKMIN_MAT_BLEND) != 0u ? s.alpha : 1.0, s.N, inst.id);
}
