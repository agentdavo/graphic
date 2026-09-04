#version 450
// fullscreen.vert -- one triangle covering the screen; no vertex data.
#include "common.glsl"
layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    v_uv = p;
}
