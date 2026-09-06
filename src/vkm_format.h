/* vkm_format.h -- the cooked scene file. Written by tools/cook.c from glTF,
 * read by src/scene.c at runtime. Everything is fixed-size records laid out
 * in the order below, so loading is one read and a handful of pointer
 * arithmetic; there is no JSON and no string parsing at runtime.
 *
 *   vkm_header
 *   Vertex      [vertex_count]
 *   uint32_t    [index_count]
 *   SkinVertex  [skin_vertex_count]
 *   Mesh        [mesh_count]
 *   Material    [material_count]   texture fields index vkm_texture, or VKMIN_NONE
 *   vkm_texture [texture_count]
 *   vkm_node    [node_count]       one per drawn primitive
 *   vkm_joint   [joint_count]
 *   vkm_channel [channel_count]
 *   vkm_key     [key_count]
 */
#ifndef VKM_FORMAT_H
#define VKM_FORMAT_H

#include "render_shared.h"

#define VKM_MAGIC 0x314d4b56u /* "VKM1" little-endian */
#define VKM_VERSION 1u

typedef struct {
    uint32_t magic, version;
    uint32_t vertex_count, index_count, skin_vertex_count;
    uint32_t mesh_count, material_count, texture_count, node_count;
    uint32_t joint_count, channel_count, key_count;
    float bounds_min[3], bounds_max[3];
    float anim_duration;
    uint32_t reserved[5];
    float skin_parent[16]; /* world transform of the root joint's parent node */
} vkm_header;

typedef struct {
    char file[64];      /* relative to the scene file's directory */
    uint32_t sampler;   /* VKMIN_SAMPLER_* */
    uint32_t reserved[3];
} vkm_texture;

typedef struct {
    float transform[16]; /* world, column-major */
    uint32_t mesh;
    uint32_t material;
    uint32_t skinned;    /* 1 when the mesh has SkinVertex data driven by the joints */
    uint32_t reserved;
} vkm_node;

typedef struct {
    float inverse_bind[16];
    int32_t parent;      /* joint index, or -1 */
    float rest_t[3];
    float rest_r[4];     /* quaternion xyzw */
    float rest_s[3];
    uint32_t reserved;
} vkm_joint;

enum { VKM_PATH_TRANSLATION = 0, VKM_PATH_ROTATION = 1, VKM_PATH_SCALE = 2 };

typedef struct {
    uint32_t joint;
    uint32_t path;
    uint32_t first_key;
    uint32_t key_count;
} vkm_channel;

typedef struct {
    float time;
    float value[4];
} vkm_key;

_Static_assert(sizeof(vkm_header) == 160, "vkm_header");
_Static_assert(sizeof(vkm_texture) == 80, "vkm_texture");
_Static_assert(sizeof(vkm_node) == 80, "vkm_node");
_Static_assert(sizeof(vkm_joint) == 112, "vkm_joint");

#endif
