#version 450
// 02: vertices from gl_VertexIndex; no buffers at all.
#define VKMIN_OWN_PUSH /* uses no push block; declaring the engine's would fail the pipeline's push_size check */
#include "common.glsl"
layout(location = 0) out vec3 v_color;
void main() {
    const vec2 pos[3] = vec2[3](vec2(0.0, -0.6), vec2(-0.6, 0.5), vec2(0.6, 0.5));
    const vec3 col[3] = vec3[3](vec3(1.0, 0.2, 0.2), vec3(0.2, 1.0, 0.2), vec3(0.2, 0.4, 1.0));
    gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
    v_color = col[gl_VertexIndex];
}
