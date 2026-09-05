/* scene.c -- see scene.h. One read, then pointer arithmetic. */
#include "scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *path, const char *why) {
    fprintf(stderr, "scene: %s: %s\n", path, why);
    abort();
}

scene scene_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) fail(path, "cannot open");
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < (long)sizeof(vkm_header)) fail(path, "too small");
    uint8_t *blob = malloc((size_t)size);
    if (!blob || fread(blob, 1, (size_t)size, f) != (size_t)size) fail(path, "read failed");
    fclose(f);

    scene s = {.blob = blob};
    memcpy(&s.header, blob, sizeof s.header);
    if (s.header.magic != VKM_MAGIC) fail(path, "not a .vkm file");
    if (s.header.version != VKM_VERSION) fail(path, "wrong .vkm version; re-run the cooker");

    /* Walk the records in file order, checking each one fits. */
    size_t off = sizeof(vkm_header);
#define TAKE(field, type, count)                                                   \
    do {                                                                          \
        if ((size_t)(count) > ((size_t)size-off)/sizeof(type))                    \
            fail(path, "truncated: " #field);                                   \
        const size_t bytes__ = (size_t)(count) * sizeof(type);                    \
        const void *record__ = blob + off;                                       \
        if ((uintptr_t)record__ % _Alignof(type)) fail(path, "unaligned: " #field); \
        s.field = record__;                                                       \
        off += bytes__;                                                           \
    } while (0)
    TAKE(vertices, Vertex, s.header.vertex_count);
    TAKE(indices, uint32_t, s.header.index_count);
    TAKE(skin_vertices, SkinVertex, s.header.skin_vertex_count);
    TAKE(meshes, Mesh, s.header.mesh_count);
    TAKE(materials, Material, s.header.material_count);
    TAKE(textures, vkm_texture, s.header.texture_count);
    TAKE(nodes, vkm_node, s.header.node_count);
    TAKE(joints, vkm_joint, s.header.joint_count);
    TAKE(channels, vkm_channel, s.header.channel_count);
    TAKE(keys, vkm_key, s.header.key_count);
#undef TAKE
    if (off != (size_t)size) fail(path, "trailing bytes; file and header disagree");

    const char *slash = strrchr(path, '/');
    if (slash) snprintf(s.dir, sizeof s.dir, "%.*s", (int)(slash - path), path);
    else snprintf(s.dir, sizeof s.dir, ".");
    return s;
}

void scene_free(scene *s) {
    free(s->blob);
    memset(s, 0, sizeof *s);
}
