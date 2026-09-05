// common.glsl -- the preamble every shader in the codebase includes first.
// Declares the shared structs, the buffer references used to reach them by
// device address, the one bindless texture array, and the one push block.
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require

#define VKMIN_GLSL
#include "shared.h"

layout(buffer_reference, scalar) readonly buffer OutdoorRef { Outdoor o; };
layout(buffer_reference, scalar) readonly buffer ScatterRef { Scatter s[]; };
layout(buffer_reference, scalar) buffer InstanceWrite { Instance i[]; };
layout(buffer_reference, scalar) buffer QuadWrite { Quad q[]; };
layout(buffer_reference, scalar) readonly buffer FrameRef { Frame frame; };
layout(buffer_reference, scalar) readonly buffer VertexRef { Vertex v[]; };
layout(buffer_reference, scalar) readonly buffer SkinVertexRef { SkinVertex v[]; };
layout(buffer_reference, scalar) readonly buffer MeshRef { Mesh m[]; };
layout(buffer_reference, scalar) readonly buffer MaterialRef { Material m[]; };
layout(buffer_reference, scalar) readonly buffer InstanceRef { Instance i[]; };
layout(buffer_reference, scalar) readonly buffer LightRef { Light l[]; };
layout(buffer_reference, scalar) readonly buffer ViewRef { View v[]; };
layout(buffer_reference, scalar) readonly buffer BoneRef { mat4 m[]; };
layout(buffer_reference, scalar) buffer DrawCmdRef { DrawCmd d[]; };
layout(buffer_reference, scalar) buffer CountRef { uint n[]; };
layout(buffer_reference, scalar) buffer ClusterRef { uint lights[]; };
layout(buffer_reference, scalar) readonly buffer QuadRef { Quad q[]; };

// The one descriptor set. Declared as both a colour sampler and a shadow
// sampler over the same binding; a slot is only ever used as one of them.
layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(set = 0, binding = 0) uniform sampler2DShadow shadow_textures[];

#ifndef VKMIN_OWN_PUSH
layout(push_constant, scalar) uniform PushBlock { Push push; };
#endif

#define TEX(i) textures[nonuniformEXT(i)]
#define SHADOW_TEX(i) shadow_textures[nonuniformEXT(i)]

// --- packing helpers, mirrored in tools/cook.c -------------------------------

vec2 oct_wrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 oct_decode(uint packed) {
    vec2 e = vec2(unpackSnorm2x16(packed));
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = oct_wrap(v.xy);
    return normalize(v);
}

// The tangent's low bit of the x component carries the bitangent sign.
vec4 tangent_decode(uint packed) {
    float sign_w = (packed & 1u) != 0u ? -1.0 : 1.0;
    vec3 t = oct_decode(packed & ~1u);
    return vec4(t, sign_w);
}

vec2 uv_decode(uint packed) { return unpackHalf2x16(packed); }

// The inverse of oct_decode, for the RG16 normal target.
vec2 oct_encode(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    return e * 0.5 + 0.5;
}

vec4 rgba8_decode(uint c) { return unpackUnorm4x8(c); }
