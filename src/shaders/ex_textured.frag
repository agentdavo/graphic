#version 450
#define VKMIN_OWN_PUSH
#include "common.glsl"
layout(push_constant, scalar) uniform ExBlock { ExPush ex; } ;
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(texture(TEX(ex.texture), v_uv).rgb * v_color, 1.0); }
