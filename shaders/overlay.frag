#version 450
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;

void main() {
    float a = texture(TEX(push.param), v_uv).r * v_color.a;
    o_color = vec4(v_color.rgb * a, a); // premultiplied
}
