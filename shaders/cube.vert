#version 450
/* One vertex shader for both cube pipelines. The only per-frame state is the
 * push-constant MVP, which the demo computes from an integer frame index. */
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
layout(location = 2) in vec2 a_uv;

layout(push_constant) uniform Push { mat4 mvp; } pc;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_uv;

void main() {
    gl_Position = pc.mvp * vec4(a_pos, 1.0);
    v_color = a_color;
    v_uv = a_uv;
}
