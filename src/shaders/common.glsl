// common.glsl -- the preamble every shader in the codebase includes first.
// Declares the shared structs, the buffer references used to reach them by
// device address, the one bindless texture array, and the one push block.
//
// THE C/GLSL TRANSPORT CONTRACT. This is the sharpest edge in the codebase and
// it is invisible from either side alone, so read this before editing anything
// here or in render_shared.h.
//
// * The #include below is a real C header. render_shared.h is compiled twice
//   from the same text: once by the C compiler for the renderer, the demo and
//   the cooker, and once by glslang with VKMIN_GLSL defined, which is what
//   makes min_types.h spell VKMIN_STRUCT/F32/I32/U32/ADDR the GLSL way instead
//   of the C way. A struct is therefore never "declared in the shader" -- it
//   is declared once, on the C side, and this file only borrows it. Never add
//   a field, reorder one, or change a type in a way that only one of the two
//   compilers would accept.
// * Layout is C layout, not std140/std430. GL_EXT_scalar_block_layout is
//   `require`d above precisely so the two sides agree without padding rules
//   having to be reasoned about; every buffer_reference below is declared
//   `scalar` for the same reason. The offsets are then pinned by
//   _Static_assert at the bottom of render_shared.h -- every size and the
//   load-bearing offsets -- so a field reordered on the C side fails the build
//   rather than silently shifting what the shader reads. Nothing in GLSL can
//   assert that, which is why the asserts live over there (guarded by
//   #ifndef VKMIN_GLSL) and why "no vec3" is a rule of that file.
// * Buffers arrive as device addresses, not as bound descriptors. Every
//   FooRef(addr) below is a pointer cast: the address comes out of Frame or
//   out of the push block, and there is no descriptor set, no binding number
//   and no per-draw descriptor update behind it. A zero address is the "not
//   present" value, hence the `!= uint64_t(0)` guards scattered through the
//   shaders -- dereferencing one is a device fault, not a validation error.
// * The push block is the one part of the contract checked mechanically at
//   runtime. vkm_spirv_push_size (vkmin_spirv.h) walks the SPIR-V for the
//   PushConstant variable and computes its size from the module's own
//   decorations; vkmin_make_pipeline then rejects the pipeline unless every
//   stage that declares a block declares exactly push_size bytes of it. So a
//   shader wanting a different push layout must #define VKMIN_OWN_PUSH before
//   including this file and declare its own block -- inheriting the engine's
//   Push while the pipeline was created for something else is a creation-time
//   failure, not a wrong picture. The ex_* shaders show both spellings: a
//   private block (ExPush) and none at all.
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require

#define VKMIN_GLSL
#include "render_shared.h"

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

// The one descriptor set in the whole engine -- textures are the single thing
// that cannot travel as a device address. One unbounded array, so an index is
// all a struct ever stores (VKMIN_NONE meaning "no texture") and the shaders
// never see a binding number. Declared twice over the same binding, as a
// colour sampler and as a shadow sampler; a slot is only ever used as one of
// them, and reading it as the other yields nonsense rather than a diagnostic.
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
