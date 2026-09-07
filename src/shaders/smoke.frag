#version 450
// smoke.frag -- third stage of the smoke test; see smoke.comp.
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() { o_color = vec4(texture(TEX(push.param), v_uv).rgb, 1.0); }
