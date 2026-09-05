// lib/shadow.glsl -- one atlas, many views. shadow_sample() is the primitive:
// 3x3 PCF against one View's tile, returning the lit fraction. The two
// helpers above it pick the view for a sun (by cascade) or a local light
// (by cube face).
float shadow_sample(Frame frame, uint view_index, vec3 world_pos, vec3 N, float NdotL) {
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

// Lit fraction from the sun at P, or 1.0 when shadows are off or unassigned.
float sun_shadow(Frame frame, Light sun, vec3 P, vec3 N, vec3 L, uint cascade) {
    if (sun.shadow_view == VKMIN_NONE || (frame.flags & VKMIN_FRAME_SHADOWS) == 0u) return 1.0;
    uint c = min(cascade, sun.shadow_views - 1u);
    return shadow_sample(frame, sun.shadow_view + c, P, N, max(dot(N, L), 0.0));
}

// Lit fraction from a local light; L points from P towards the light.
float light_shadow(Frame frame, Light light, vec3 P, vec3 N, vec3 L) {
    if (light.shadow_view == VKMIN_NONE || (frame.flags & VKMIN_FRAME_SHADOWS) == 0u) return 1.0;
    uint face = light.type == VKMIN_LIGHT_POINT ? cube_face(-L) : 0u;
    return shadow_sample(frame, light.shadow_view + face, P, N, max(dot(N, L), 0.0));
}
