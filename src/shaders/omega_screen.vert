#version 450
#include "omega.glsl"
layout(location=0) out vec2 uv;
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    uv=p; gl_Position=vec4(p*2.-1.,.9999,1);
}
