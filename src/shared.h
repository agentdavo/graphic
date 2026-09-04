/* shared.h -- every struct that crosses the CPU/GPU boundary, declared once.
 *
 * Included by C (vkmin, the renderer, the demo, the cooker) and by GLSL (with
 * VKMIN_GLSL defined). Both sides compile this same text. Shaders read these
 * through GL_EXT_scalar_block_layout, whose rule is "lay it out like C": no
 * std140/std430 padding surprises. The _Static_asserts at the bottom pin every
 * offset so that the day someone reorders a field, the build fails instead of
 * the picture going subtly wrong.
 *
 * Rules for this file: no vec3 (12-byte alignment traps), addresses (ADDR) are
 * 8-byte aligned, every struct's size is a multiple of its largest member.
 */
#ifndef VKMIN_SHARED_H
#define VKMIN_SHARED_H

#ifdef VKMIN_GLSL
#define U32 uint
#define I32 int
#define F32 float
#define ADDR uint64_t
#define VKMIN_STRUCT(name) struct name
#else
#include <stdint.h>
typedef uint32_t U32;
typedef int32_t I32;
typedef float F32;
typedef uint64_t ADDR;
typedef struct { float x, y; } vec2;
typedef struct { float x, y, z, w; } vec4;
typedef struct { uint32_t x, y, z, w; } uvec4;
typedef struct { float m[16]; } mat4; /* column-major, as GLSL */
#define VKMIN_STRUCT(name) typedef struct name name; struct name
#endif

/* ------------------------------------------------------------ constants --- */

#define VKMIN_NONE 0xffffffffu

#define VKMIN_MAX_TEXTURES 4096u   /* bindless array size */
#define VKMIN_MAX_LIGHTS 256u
#define VKMIN_MAX_VIEWS 40u        /* camera + shadow views culled per frame */
#define VKMIN_MAX_DRAWS 4096u      /* indirect commands per view */
#define VKMIN_CASCADES 4u

#define VKMIN_CLUSTER_X 16u
#define VKMIN_CLUSTER_Y 9u
#define VKMIN_CLUSTER_Z 24u
#define VKMIN_CLUSTER_COUNT (VKMIN_CLUSTER_X * VKMIN_CLUSTER_Y * VKMIN_CLUSTER_Z)
#define VKMIN_CLUSTER_MAX_LIGHTS 31u /* per cluster; slot 0 of each list is the count */
#define VKMIN_CLUSTER_STRIDE (VKMIN_CLUSTER_MAX_LIGHTS + 1u)

#define VKMIN_CULL_GROUP 64u
#define VKMIN_CLUSTER_GROUP 64u

/* Sampler presets. Users never create samplers; they pick one of these. */
#define VKMIN_SAMPLER_LINEAR_REPEAT 0u
#define VKMIN_SAMPLER_LINEAR_CLAMP 1u
#define VKMIN_SAMPLER_NEAREST_CLAMP 2u
#define VKMIN_SAMPLER_ANISO_REPEAT 3u
#define VKMIN_SAMPLER_SHADOW 4u
#define VKMIN_SAMPLER_COUNT 5u

#define VKMIN_MAT_MASKED 1u
#define VKMIN_MAT_BLEND 2u
#define VKMIN_MAT_DOUBLE_SIDED 4u
#define VKMIN_MAT_UNLIT 8u

#define VKMIN_INST_SKINNED 1u
#define VKMIN_INST_HIDDEN 2u

#define VKMIN_LIGHT_DIRECTIONAL 0u
#define VKMIN_LIGHT_POINT 1u
#define VKMIN_LIGHT_SPOT 2u

/* Frame.debug_mode: what the forward shader writes instead of lighting. */
#define VKMIN_DEBUG_NONE 0u
#define VKMIN_DEBUG_NORMALS 1u
#define VKMIN_DEBUG_CLUSTERS 2u  /* lights per cluster as a heat map */
#define VKMIN_DEBUG_CASCADES 3u  /* cascade index by colour */
#define VKMIN_DEBUG_OVERDRAW 4u
#define VKMIN_DEBUG_ALBEDO 5u
#define VKMIN_DEBUG_SHADOW_ATLAS 6u /* tonemap shows the raw atlas */

/* Frame.flags */
#define VKMIN_FRAME_SHADOWS 1u
#define VKMIN_FRAME_NORMAL_MAPS 2u
#define VKMIN_FRAME_CLUSTERED 4u /* off: brute-force every light, the reference path */

/* -------------------------------------------------------------- structs --- */

/* 24 bytes. Position as three floats; normal and tangent octahedral-encoded
 * into two snorm16 each (the tangent steals its low bit for bitangent sign);
 * UV as two halfs. */
VKMIN_STRUCT(Vertex) {
    F32 px, py, pz;
    U32 normal;
    U32 tangent;
    U32 uv;
};

/* Parallel to Vertex for skinned meshes only: four u8 joints, four unorm8
 * weights. Indexed by (vertex index - mesh.vertex_offset + mesh.skin_offset). */
VKMIN_STRUCT(SkinVertex) {
    U32 joints;
    U32 weights;
};

VKMIN_STRUCT(Mesh) {
    U32 first_index;
    U32 index_count;
    U32 vertex_offset;
    U32 skin_offset; /* VKMIN_NONE when not skinned */
    vec4 bounds;     /* local-space bounding sphere: centre xyz, radius w */
};

VKMIN_STRUCT(Material) {
    vec4 base_color;
    vec4 emissive;   /* rgb, w unused */
    U32 albedo_tex;  /* bindless indices; VKMIN_NONE selects the 1x1 defaults */
    U32 normal_tex;
    U32 mr_tex;      /* glTF convention: G = roughness, B = metallic */
    U32 emissive_tex;
    F32 metallic;
    F32 roughness;
    F32 alpha_cutoff;
    F32 normal_scale;
    U32 flags;
    U32 pad0, pad1, pad2;
};

VKMIN_STRUCT(Instance) {
    mat4 transform;
    mat4 prev_transform;
    vec4 bounds;        /* world-space sphere, recomputed by whoever moves it */
    U32 mesh;
    U32 material;
    U32 bone_offset;    /* index of first mat4 in the bone buffer, or VKMIN_NONE */
    U32 flags;
};

VKMIN_STRUCT(Light) {
    vec4 pos_radius;    /* xyz position, w radius (directional: unused, huge) */
    vec4 color;         /* rgb already multiplied by intensity */
    vec4 dir_cone;      /* xyz direction (spot, directional), w cos(outer angle) */
    U32 type;
    F32 cos_inner;
    U32 shadow_view;    /* first View index for this light's shadow, or VKMIN_NONE */
    U32 shadow_views;   /* 1 for spot, 6 for point (cube faces), cascades for sun */
};

/* One thing the GPU culls against and may render depth for. View 0 is the
 * camera; the rest are shadow views. */
VKMIN_STRUCT(View) {
    mat4 view_proj;
    vec4 planes[6];     /* frustum planes, normalised, pointing inwards */
    vec4 atlas_rect;    /* uv rect in the shadow atlas: x, y, w, h */
    vec4 texel;         /* x: atlas texel size in uv, y: depth bias, z: normal bias, w: unused */
    U32 flags;
    U32 pad0, pad1, pad2;
};

/* Exactly VkDrawIndexedIndirectCommand. Written by the cull shader. The
 * instance index rides in first_instance and comes out as gl_InstanceIndex. */
VKMIN_STRUCT(DrawCmd) {
    U32 index_count;
    U32 instance_count;
    U32 first_index;
    I32 vertex_offset;
    U32 first_instance;
};

/* Everything the shaders need for a frame, at one address. Buffers are
 * reached by device address from here; nothing is bound except the bindless
 * texture set and the index buffer. */
VKMIN_STRUCT(Frame) {
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    vec4 camera_pos;    /* xyz, w = frame index as float */
    vec4 cascade_splits; /* view-space distances where cascade 0..3 end */
    vec4 cluster_params; /* x: z-slice scale, y: z-slice bias, z: near, w: far */
    uvec4 cluster_dims;  /* x, y, z clusters; w: unused */
    vec4 screen;         /* w, h, 1/w, 1/h */
    vec4 ambient;        /* rgb ambient radiance, w: exposure */
    vec4 sun;            /* xyz direction towards the sun, w: unused */
    U32 light_count;
    U32 view_count;
    U32 debug_mode;
    U32 flags;
    U32 frame_index;
    U32 sun_light;       /* index into lights, or VKMIN_NONE */
    U32 instance_count;
    U32 shadow_atlas_tex;
    ADDR vertices;
    ADDR skin_vertices;
    ADDR meshes;
    ADDR materials;
    ADDR instances;
    ADDR lights;
    ADDR views;
    ADDR bones;
    ADDR draw_cmds;     /* DrawCmd[VKMIN_MAX_VIEWS][VKMIN_MAX_DRAWS] */
    ADDR draw_counts;   /* U32[VKMIN_MAX_VIEWS] */
    ADDR cluster_lights; /* U32[VKMIN_CLUSTER_COUNT * VKMIN_CLUSTER_STRIDE] */
    ADDR pad;
};

/* The one push constant block for every pipeline. */
VKMIN_STRUCT(Push) {
    ADDR frame;
    ADDR aux;           /* pass-specific buffer: overlay quads, transparent list */
    U32 view;           /* which View a depth pass renders / the cull shader culls */
    U32 param;          /* pass-specific scalar */
    U32 param2;
    U32 param3;
};

/* The examples' push block and vertex. A program defines its own like this:
 * a struct in shared.h, seen identically by C and GLSL, with resources as
 * numbers -- a device address for the vertices, a bindless index for the
 * texture -- and whatever else the draw needs. */
VKMIN_STRUCT(ExVertex) {
    F32 px, py, pz;
    U32 color;      /* rgba8 */
    F32 u, v;
    F32 pad0, pad1;
};

VKMIN_STRUCT(ExPush) {
    mat4 mvp;
    ADDR vertices;
    U32 texture;
    U32 frame;
    F32 time;
    F32 pad;
};

/* Overlay text: one quad per glyph, written by the CPU into the ring buffer. */
VKMIN_STRUCT(OverlayQuad) {
    vec4 rect;          /* x0, y0, x1, y1 in pixels */
    vec4 uv;            /* u0, v0, u1, v1 */
    U32 color;          /* rgba8 */
    U32 pad0, pad1, pad2;
};

/* --------------------------------------------------- the layout contract --- */
#ifndef VKMIN_GLSL
#include <stddef.h>
_Static_assert(sizeof(Vertex) == 24, "Vertex");
_Static_assert(sizeof(SkinVertex) == 8, "SkinVertex");
_Static_assert(sizeof(Mesh) == 32 && offsetof(Mesh, bounds) == 16, "Mesh");
_Static_assert(sizeof(Material) == 80 && offsetof(Material, albedo_tex) == 32 &&
                   offsetof(Material, metallic) == 48 && offsetof(Material, flags) == 64,
               "Material");
_Static_assert(sizeof(Instance) == 160 && offsetof(Instance, bounds) == 128 &&
                   offsetof(Instance, mesh) == 144,
               "Instance");
_Static_assert(sizeof(Light) == 64 && offsetof(Light, type) == 48, "Light");
_Static_assert(sizeof(View) == 208 && offsetof(View, planes) == 64 &&
                   offsetof(View, atlas_rect) == 160 && offsetof(View, flags) == 192,
               "View");
_Static_assert(sizeof(DrawCmd) == 20, "DrawCmd");
_Static_assert(sizeof(Frame) == 496 && offsetof(Frame, camera_pos) == 256 &&
                   offsetof(Frame, sun) == 352 && offsetof(Frame, light_count) == 368 &&
                   offsetof(Frame, vertices) == 400 && offsetof(Frame, cluster_lights) == 480,
               "Frame");
_Static_assert(sizeof(Push) == 32 && offsetof(Push, view) == 16, "Push");
_Static_assert(sizeof(OverlayQuad) == 48, "OverlayQuad");
_Static_assert(sizeof(ExVertex) == 32, "ExVertex");
_Static_assert(sizeof(ExPush) == 88 && offsetof(ExPush, vertices) == 64, "ExPush");
#endif

#endif /* VKMIN_SHARED_H */
