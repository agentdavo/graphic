#version 450
#define VKMIN_OWN_PUSH /* uses no push block; declaring the engine's would fail the pipeline's push_size check */
#include "common.glsl"
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(v_color, 1.0); }
