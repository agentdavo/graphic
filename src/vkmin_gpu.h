/* GPU transport contract, shared by C and GLSL. No scene policy. */
#ifndef VKMIN_GPU_H
#define VKMIN_GPU_H
#include "min_types.h"
/* The absent-index sentinel for every U32 field that names something by index
 * -- a bindless texture, a bone range, a shadow view. Zero is a real index, so
 * it cannot serve; callers test against this, never against 0. */
#define VKMIN_NONE 0xffffffffu
/* Descriptor array size, so also one past the largest index vkmin_index can
 * return. Shaders declare the array at this size, which is why changing it
 * means recompiling shaders, not just rebuilding vkmin. */
#define VKMIN_MAX_TEXTURES 4096u
/* Sampler presets. Users never create samplers; they pick one of these. */
#define VKMIN_SAMPLER_LINEAR_REPEAT 0u
#define VKMIN_SAMPLER_LINEAR_CLAMP 1u
#define VKMIN_SAMPLER_NEAREST_CLAMP 2u
#define VKMIN_SAMPLER_ANISO_REPEAT 3u
#define VKMIN_SAMPLER_SHADOW 4u
#define VKMIN_SAMPLER_COUNT 5u

/* Exactly VkDrawIndexedIndirectCommand. Written by the cull shader. The
 * instance index rides in first_instance and comes out as gl_InstanceIndex. */
VKMIN_STRUCT(DrawCmd) {
    U32 index_count;
    U32 instance_count;
    U32 first_index;
    I32 vertex_offset;
    U32 first_instance;
};

#ifndef VKMIN_GLSL
_Static_assert(sizeof(DrawCmd) == 20, "DrawCmd");
#endif
#endif
