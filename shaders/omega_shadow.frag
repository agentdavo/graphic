#version 450
#include "omega.glsl"
layout(location=0) in vec3 world;
layout(location=4) flat in int material;
void main() { if(material>=3) discard; }
