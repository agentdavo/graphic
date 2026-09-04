#version 450
/* Milestone 2: vertices come from gl_VertexIndex, so pipeline creation is
 * exercised with no memory management in the picture at all. */
layout(location = 0) out vec3 v_color;

void main() {
    const vec2 pos[3] = vec2[3](vec2(0.0, -0.6), vec2(-0.6, 0.5), vec2(0.6, 0.5));
    const vec3 col[3] = vec3[3](vec3(1.0, 0.15, 0.15), vec3(0.15, 1.0, 0.15), vec3(0.2, 0.3, 1.0));
    gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
    v_color = col[gl_VertexIndex];
}
