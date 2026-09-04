#version 450
// scene.frag -- the one lighting model, applied to everything. Metallic-
// roughness PBR: GGX / Smith / Schlick, Lambert diffuse, one directional sun
// with cascaded shadows, local lights from the cluster list (or brute force
// when the reference path is selected), constant ambient, emissive. The
// debug modes replace the result; they never skip the work.
#include "common.glsl"
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_material;
layout(location = 0) out vec4 o_color;

const float PI = 3.14159265359;

float d_ggx(float NdotH, float a2) {
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float g_smith(float NdotV, float NdotL, float a2) {
    float gv = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
    float gl = NdotL + sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
    return 1.0 / max(gv * gl, 1e-6); // includes the 1/(4 NdotV NdotL) of the BRDF
}

vec3 f_schlick(vec3 f0, float VdotH) {
    float f = pow(1.0 - VdotH, 5.0);
    return f0 + (1.0 - f0) * f;
}

vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = f_schlick(f0, VdotH);
    vec3 spec = d_ggx(NdotH, a2) * g_smith(NdotV, NdotL, a2) * F;
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * NdotL;
}

// 3x3 PCF against one View's tile in the atlas. Returns lit fraction.
float shadow_lookup(Frame frame, uint view_index, vec3 world_pos, vec3 N, float NdotL) {
    View sv = ViewRef(frame.views).v[view_index];
    vec3 offset_pos = world_pos + N * sv.texel.z * (1.0 - NdotL);
    vec4 clip = sv.view_proj * vec4(offset_pos, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) return 1.0;
    vec2 atlas_uv = sv.atlas_rect.xy + uv * sv.atlas_rect.zw;
    float ref = ndc.z - sv.texel.y;
    float texel = sv.texel.x;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(SHADOW_TEX(frame.shadow_atlas_tex), vec3(atlas_uv + vec2(x, y) * texel, ref));
        }
    }
    return lit / 9.0;
}

uint cascade_for(Frame frame, float view_depth) {
    uint c = 0u;
    if (view_depth > frame.cascade_splits.x) c = 1u;
    if (view_depth > frame.cascade_splits.y) c = 2u;
    if (view_depth > frame.cascade_splits.z) c = 3u;
    return c;
}

uint cube_face(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0u : 1u;
    if (a.y >= a.z) return d.y > 0.0 ? 2u : 3u;
    return d.z > 0.0 ? 4u : 5u;
}

vec3 shade_light(Frame frame, Light light, vec3 P, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 to_light = light.pos_radius.xyz - P;
    float dist2 = dot(to_light, to_light);
    float dist = sqrt(dist2);
    vec3 L = to_light / max(dist, 1e-4);
    // windowed inverse square: reaches exactly zero at the radius
    float r = light.pos_radius.w;
    float w = clamp(1.0 - (dist2 * dist2) / (r * r * r * r), 0.0, 1.0);
    float atten = w * w / max(dist2, 1e-4);
    if (light.type == VKMIN_LIGHT_SPOT) {
        float cd = dot(-L, light.dir_cone.xyz);
        atten *= smoothstep(light.dir_cone.w, light.cos_inner, cd);
    }
    if (atten <= 0.0) return vec3(0.0);
    float shadow = 1.0;
    if (light.shadow_view != VKMIN_NONE && (frame.flags & VKMIN_FRAME_SHADOWS) != 0u) {
        uint face = light.type == VKMIN_LIGHT_POINT ? cube_face(-L) : 0u;
        shadow = shadow_lookup(frame, light.shadow_view + face, P, N, max(dot(N, L), 0.0));
    }
    return brdf(N, V, L, albedo, metallic, roughness) * light.color.rgb * atten * shadow;
}

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Material m = MaterialRef(frame.materials).m[v_material];

    uint albedo_tex = m.albedo_tex == VKMIN_NONE ? 0u : m.albedo_tex;
    vec4 albedo_s = texture(TEX(albedo_tex), v_uv) * m.base_color;
    float alpha = albedo_s.a;
    if ((m.flags & VKMIN_MAT_MASKED) != 0u && alpha < m.alpha_cutoff) discard;
    vec3 albedo = albedo_s.rgb;

    uint mr_tex = m.mr_tex == VKMIN_NONE ? 0u : m.mr_tex;
    vec4 mr = texture(TEX(mr_tex), v_uv);
    float roughness = clamp(mr.g * m.roughness, 0.03, 1.0);
    float metallic = clamp(mr.b * m.metallic, 0.0, 1.0);

    vec3 N = normalize(v_normal);
    vec3 V = normalize(frame.camera_pos.xyz - v_world_pos);
    if ((m.flags & VKMIN_MAT_DOUBLE_SIDED) != 0u && !gl_FrontFacing) N = -N;
    // Always sample the normal map (slot 1 is a flat normal when there is
    // none); the flags decide whether the result is used.
    uint normal_tex = m.normal_tex == VKMIN_NONE ? 1u : m.normal_tex;
    vec2 nxy = texture(TEX(normal_tex), v_uv).rg * 2.0 - 1.0;
    nxy *= m.normal_scale;
    vec3 nts = vec3(nxy, sqrt(max(1.0 - dot(nxy, nxy), 0.0)));
    vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
    vec3 B = cross(N, T) * v_tangent.w;
    vec3 N_mapped = normalize(T * nts.x + B * nts.y + N * nts.z);
    bool use_normal_map = (frame.flags & VKMIN_FRAME_NORMAL_MAPS) != 0u && m.normal_tex != VKMIN_NONE;
    N = use_normal_map ? N_mapped : N;

    vec3 color = frame.ambient.rgb * albedo + m.emissive.rgb;
    if ((m.flags & VKMIN_MAT_UNLIT) != 0u) color = albedo;

    float view_depth = -(frame.view * vec4(v_world_pos, 1.0)).z;
    uint cascade = cascade_for(frame, view_depth);
    if (frame.sun_light != VKMIN_NONE && (m.flags & VKMIN_MAT_UNLIT) == 0u) {
        Light sun = LightRef(frame.lights).l[frame.sun_light];
        vec3 L = normalize(frame.sun.xyz);
        float NdotL = max(dot(N, L), 0.0);
        float shadow = 1.0;
        if (sun.shadow_view != VKMIN_NONE && (frame.flags & VKMIN_FRAME_SHADOWS) != 0u) {
            uint c = min(cascade, sun.shadow_views - 1u);
            shadow = shadow_lookup(frame, sun.shadow_view + c, v_world_pos, N, NdotL);
        }
        color += brdf(N, V, L, albedo, metallic, roughness) * sun.color.rgb * shadow;
    }

    uint cluster = 0u;
    uint cluster_count = 0u;
    if ((frame.flags & VKMIN_FRAME_CLUSTERED) != 0u) {
        uvec2 tile = uvec2(gl_FragCoord.xy * frame.screen.zw * vec2(VKMIN_CLUSTER_X, VKMIN_CLUSTER_Y));
        tile = min(tile, uvec2(VKMIN_CLUSTER_X - 1u, VKMIN_CLUSTER_Y - 1u));
        uint slice = uint(max(log(max(view_depth, 1e-4)) * frame.cluster_params.x + frame.cluster_params.y, 0.0));
        slice = min(slice, VKMIN_CLUSTER_Z - 1u);
        cluster = (slice * VKMIN_CLUSTER_Y + tile.y) * VKMIN_CLUSTER_X + tile.x;
        ClusterRef lists = ClusterRef(frame.cluster_lights);
        uint base = cluster * VKMIN_CLUSTER_STRIDE;
        cluster_count = lists.lights[base];
        if ((m.flags & VKMIN_MAT_UNLIT) == 0u) {
            for (uint k = 0u; k < cluster_count; ++k) {
                Light light = LightRef(frame.lights).l[lists.lights[base + 1u + k]];
                color += shade_light(frame, light, v_world_pos, N, V, albedo, metallic, roughness);
            }
        }
    } else if ((m.flags & VKMIN_MAT_UNLIT) == 0u) {
        // The reference path: every light, every pixel. Slow and obviously right.
        for (uint l = 0u; l < frame.light_count; ++l) {
            Light light = LightRef(frame.lights).l[l];
            if (light.type == VKMIN_LIGHT_DIRECTIONAL) continue;
            color += shade_light(frame, light, v_world_pos, N, V, albedo, metallic, roughness);
        }
    }

    if (frame.debug_mode == VKMIN_DEBUG_NORMALS) color = N * 0.5 + 0.5;
    if (frame.debug_mode == VKMIN_DEBUG_ALBEDO) color = albedo;
    if (frame.debug_mode == VKMIN_DEBUG_CASCADES) {
        const vec3 palette[4] = vec3[4](vec3(1, 0.2, 0.2), vec3(0.2, 1, 0.2), vec3(0.2, 0.4, 1), vec3(1, 1, 0.2));
        color = palette[cascade] * (0.3 + 0.7 * max(dot(N, normalize(frame.sun.xyz)), 0.0));
    }
    if (frame.debug_mode == VKMIN_DEBUG_CLUSTERS) {
        float t = float(cluster_count) / 16.0;
        color = mix(vec3(0.0, 0.1, 0.5), vec3(1.0, 0.1, 0.0), clamp(t, 0.0, 1.0));
        if (cluster_count == 0u) color = vec3(0.02);
    }
    if (frame.debug_mode == VKMIN_DEBUG_OVERDRAW) {
        o_color = vec4(0.12, 0.10, 0.06, 0.0); // additive: the pipeline blends
        return;
    }

    bool blend = (m.flags & VKMIN_MAT_BLEND) != 0u;
    float out_alpha = blend ? alpha : 1.0;
    o_color = vec4(color * out_alpha, out_alpha);
}
