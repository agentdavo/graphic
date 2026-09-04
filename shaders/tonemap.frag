#version 450
// tonemap.frag -- HDR target to the backbuffer in one pass. push.param is the
// HDR texture slot, push.param2 the tonemap curve, push.param3 the shadow
// atlas slot for the atlas debug view. The backbuffer is UNORM, so the sRGB
// encode is done here by hand.
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
    vec3 ldr = push.param2 == 1u ? aces(hdr) : push.param2 == 2u ? hdr / (1.0 + hdr) : clamp(hdr, 0.0, 1.0);
    o_color = vec4(to_srgb(ldr), 1.0);
}
