#version 450
#include "common.glsl"
layout(location = 0) out vec2 v_uv;

void main() {
    VertexRef verts = VertexRef(push.aux);
    Vertex v = verts.v[gl_VertexIndex];
    gl_Position = vec4(v.px, v.py, v.pz, 1.0);
    v_uv = uv_decode(v.uv);
}
