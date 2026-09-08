#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(buffer_reference, std430, buffer_reference_align=4) readonly buffer Values { float value; };
layout(push_constant, std430) uniform P { uint64_t address; uint64_t integer; } p;
layout(location=0) out vec4 color;
void main() { color=vec4(Values(p.address).value,0.25,0.5,1.0); }
