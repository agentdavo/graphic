// lib/shadow.glsl -- one atlas, many views. shadow_sample() is the primitive:
// 3x3 PCF against one View's tile, returning the lit fraction. The two
// helpers at the bottom pick the view for a sun (by cascade) or a local light
// (by cube face).
//
// Everything outside a view's frustum returns 1.0, fully lit. That is the only
// choice that degrades gracefully -- returning 0 would black out anything the
// map does not reach -- but it means shadows simply stop at the far cascade's
// edge, which is why lit_pbr.frag fades the sun's shadow to 1.0 before it.
float shadow_sample(Frame frame, uint view_index, vec3 world_pos, vec3 N, float NdotL) {
    View sv = ViewRef(frame.views).v[view_index];
    vec3 offset_pos = world_pos + N * sv.texel.z * (1.0 - NdotL);
    vec4 clip = sv.view_proj * vec4(offset_pos, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    vec2 atlas_uv = sv.atlas_rect.xy + uv * sv.atlas_rect.zw;
    float ref = ndc.z - sv.texel.y;
    float texel = sv.texel.x;
    vec2 lo = sv.atlas_rect.xy + vec2(0.5 * texel);
    vec2 hi = sv.atlas_rect.xy + sv.atlas_rect.zw - vec2(0.5 * texel);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 tap = clamp(atlas_uv + vec2(x, y) * texel, lo, hi);
            lit += texture(SHADOW_TEX(frame.shadow_atlas_tex), vec3(tap, ref));
        }
    }
    return lit / 9.0;
}

// KNOWN DISCONTINUITY: this is a hard step. Two adjacent pixels either side of
// a split get different views, different texel sizes and different depth bias,
// so the lit fraction jumps and the split shows as a visible seam across the
// image. Blending the two cascades over a band is planned separately as
// r_cascade_blend; do not "fix" it here in passing, because the same index is
// also what VKMIN_DEBUG_CASCADES colours and what sun_shadow() clamps against
// sun.shadow_views, and all three have to move together.
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
//
// Takes view_depth rather than a precomputed cascade so the selection and the
// blend cannot disagree: carrying both would be two spellings of one fact.
// frame.sun.w is the blend fraction (cvar r_cascade_blend). At 0 the second
// sample is a uniform branch nothing takes, so the hard-split path is not an
// approximation of the old behaviour -- it is the same instructions.
//
// The band for cascade c is [split_c*(1-k), split_c]. Just below split_c the
// mix is entirely cascade c+1; just above, cascade_for returns c+1 with t = 0,
// which is also entirely c+1. So the function is continuous at every split for
// any k, and smoothstep flattens the derivative at both ends of the band.
float sun_shadow(Frame frame, Light sun, vec3 P, vec3 N, vec3 L, float view_depth) {
    if (sun.shadow_view == VKMIN_NONE || (frame.flags & VKMIN_FRAME_SHADOWS) == 0u) return 1.0;
    float ndl = max(dot(N, L), 0.0);
    uint c = min(cascade_for(frame, view_depth), sun.shadow_views - 1u);
    float lit = shadow_sample(frame, sun.shadow_view + c, P, N, ndl);

    float k = frame.sun.w;
    // c + 1u < shadow_views, not c < shadow_views - 1u: shadow_views is
    // unsigned and the subtraction would wrap if it were ever zero.
    if (k > 0.0 && c + 1u < sun.shadow_views) {
        float split = frame.cascade_splits[c];
        float band = split * k;
        float t = band > 0.0 ? clamp((view_depth - (split - band)) / band, 0.0, 1.0) : 0.0;
        if (t > 0.0) {
            float next = shadow_sample(frame, sun.shadow_view + c + 1u, P, N, ndl);
            lit = mix(lit, next, smoothstep(0.0, 1.0, t));
        }
    }
    return lit;
}

// Lit fraction from a local light; L points from P towards the light.
float light_shadow(Frame frame, Light light, vec3 P, vec3 N, vec3 L) {
    if (light.shadow_view == VKMIN_NONE || (frame.flags & VKMIN_FRAME_SHADOWS) == 0u) return 1.0;
    uint face = light.type == VKMIN_LIGHT_POINT ? cube_face(-L) : 0u;
    return shadow_sample(frame, light.shadow_view + face, P, N, max(dot(N, L), 0.0));
}
