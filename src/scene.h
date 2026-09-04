/* scene.h -- loads a cooked .vkm file into memory. The result is plain arrays
 * the demo uploads into the renderer; nothing here touches the GPU. */
#ifndef VKMIN_SCENE_H
#define VKMIN_SCENE_H

#include "vkm_format.h"

typedef struct {
    vkm_header header;
    const Vertex *vertices;
    const uint32_t *indices;
    const SkinVertex *skin_vertices;
    const Mesh *meshes;
    const Material *materials;
    const vkm_texture *textures;
    const vkm_node *nodes;
    const vkm_joint *joints;
    const vkm_channel *channels;
    const vkm_key *keys;
    char dir[512];          /* directory of the scene file, for texture paths */
    void *blob;             /* the whole file; every pointer above points into it */
} scene;

/* Aborts with a message on any malformed input. */
scene scene_load(const char *path);
void scene_free(scene *s);

#endif
