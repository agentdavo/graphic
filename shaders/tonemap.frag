#version 450
// tonemap.frag -- the post stack in one full-screen pass, in fixed order:
// outline (depth and normal discontinuities darken the colour), exposure and
// tonemap curve, colour-grading LUT, sRGB encode. Each stage always runs and
// is inhibited by its strength; the cvars set the strengths, so switching a
// stage off changes no code path. push.param is the HDR texture slot,
// push.param2 the tonemap curve, push.param3 the shadow atlas slot for the
// atlas debug view.
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 aces(vec3 x) {
    // Krzysztof Narkowicz's fit of the ACES filmic curve
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 to_srgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// View-space depth from the depth target, for a threshold in world units.
float linear_depth(Frame frame, vec2 uv) {
    float z = texture(TEX(frame.depth_tex), uv).r;
    // Orthographic depth is affine. Invert the actual projection, including
    // its near plane, rather than applying the perspective reciprocal.
    if (frame.proj[3][3] == 1.0) return (frame.proj[3][2] - z) / frame.proj[2][2];
    // Inverts the perspective depth mapping using the near/far the clusters use.
    float n = frame.cluster_params.z, f = frame.cluster_params.w;
    return n * f / max(f - z * (f - n), 1e-6);
}

// Edge strength in 0..1 from the four neighbours' depth and normal.
float outline(Frame frame, vec2 uv) {
    vec2 px = frame.screen.zw;
    float d0 = linear_depth(frame, uv);
    vec3 n0 = oct_decode(packSnorm2x16(texture(TEX(frame.normal_tex), uv).rg * 2.0 - 1.0));
    float edge = 0.0;
    vec2 offsets[4] = vec2[4](vec2(px.x, 0.0), vec2(-px.x, 0.0), vec2(0.0, px.y), vec2(0.0, -px.y));
    for (int i = 0; i < 4; ++i) {
        float d = linear_depth(frame, uv + offsets[i]);
        vec3 n = oct_decode(packSnorm2x16(texture(TEX(frame.normal_tex), uv + offsets[i]).rg * 2.0 - 1.0));
        edge = max(edge, step(frame.post.y * max(d0, 1.0), abs(d - d0)));
        edge = max(edge, step(0.6, 1.0 - dot(n0, n)) * step(0.01, length(n)));
    }
    return edge * frame.post.x;
}

// A 256x16 strip: r indexes across a slice, g down it, b selects the slice.
vec3 lut(Frame frame, vec3 c) {
    float slice = c.b * 15.0;
    float s0 = floor(slice), s1 = min(s0 + 1.0, 15.0);
    vec2 uv0 = vec2((s0 * 16.0 + c.r * 15.0 + 0.5) / 256.0, (c.g * 15.0 + 0.5) / 16.0);
    vec2 uv1 = vec2((s1 * 16.0 + c.r * 15.0 + 0.5) / 256.0, uv0.y);
    vec3 graded = mix(texture(TEX(frame.lut_tex), uv0).rgb, texture(TEX(frame.lut_tex), uv1).rgb, slice - s0);
    return mix(c, graded, frame.post.z);
}

void main() {
    Frame frame = FrameRef(push.frame).frame;
    // The HDR target may be larger than the rendered area; frame.screen holds
    // the rendered size and the ring-side uv scale comes with it.
    vec2 uv = v_uv * vec2(frame.screen.x, frame.screen.y) / vec2(textureSize(TEX(push.param), 0));
    if (frame.debug_mode == VKMIN_DEBUG_SHADOW_ATLAS) {
        float d = texture(TEX(push.param3), v_uv).r;
        o_color = vec4(vec3(1.0 - d), 1.0);
        return;
    }
    vec3 hdr = texture(TEX(push.param), uv).rgb * frame.ambient.w;
    hdr *= 1.0 - outline(frame, uv);
    vec3 ldr = push.param2 == 1u ? aces(hdr) : push.param2 == 2u ? hdr / (1.0 + hdr) : clamp(hdr, 0.0, 1.0);
    ldr = lut(frame, ldr);
    o_color = vec4(to_srgb(ldr), 1.0);
}
