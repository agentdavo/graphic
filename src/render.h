/* render.h -- the renderer: eight stages in one function, in the order they
 * execute.
 *
 *   cull -> shadow atlas -> depth prepass -> light clusters
 *        -> forward opaque -> forward transparent -> tonemap -> overlay
 *
 * The caller owns the scene: instances, lights and bones are plain arrays it
 * hands over every frame; static geometry and materials are uploaded once.
 * Textures are bindless slots the caller obtained from vkmin. The renderer
 * never allocates after init and never reads a cvar in a loop.
 *
 * The caller owns the frame: vkr_frame records between the caller's
 * vkmin_frame_begin(ctx, NULL) and vkmin_frame_end(ctx).
 */
#ifndef VKMIN_RENDER_H
#define VKMIN_RENDER_H

#include "vkmin.h"

typedef struct vkr vkr;

typedef struct {
    int width, height;         /* maximum render size; targets are made once, at this size */
    int shadow_atlas;          /* texels on a side, power of two */
    size_t max_vertices, max_indices, max_skin_vertices;
    uint32_t max_meshes, max_materials, max_instances;
} vkr_desc;

/* Static uploads. Each returns the base index the caller adds to its own. */
typedef struct {
    const Vertex *vertices; uint32_t vertex_count;
    const uint32_t *indices; uint32_t index_count;
    const SkinVertex *skin_vertices; uint32_t skin_vertex_count;
    const Mesh *meshes; uint32_t mesh_count;   /* offsets relative to this upload */
} vkr_geometry;

typedef struct { uint32_t first_mesh, first_material; } vkr_upload_result;

typedef struct {
    mat4 view, proj;
    vec4 camera_pos;
    float near, far;
    const Instance *instances; uint32_t instance_count;
    const Light *lights; uint32_t light_count;
    const mat4 *bones; uint32_t bone_count;
    const char *overlay_text;  /* '\n'-separated lines drawn at the top left, may be NULL */
    uint32_t frame_index;
} vkr_frame_desc;

typedef struct {
    double pass_ms[8];         /* cull, shadows, prepass, clusters, opaque, transparent, tonemap, overlay */
    double frame_ms;
    uint32_t draws_camera;     /* visible instances in the camera view, one frame late */
    uint32_t draws_shadow;     /* visible instances summed over shadow views */
    uint32_t shadow_views;
    uint32_t lights_shadowed;
    uint32_t triangles_camera;
    size_t device_used, device_cap, ring_used, ring_cap;
} vkr_stats;

extern const char *const vkr_pass_names[8];

vkr *vkr_init(vkmin_ctx *gpu, const vkr_desc *desc);
void vkr_shutdown(vkr *r);
uint32_t vkr_upload_geometry(vkr *r, const vkr_geometry *g);     /* returns first mesh index */
uint32_t vkr_upload_materials(vkr *r, const Material *m, uint32_t count); /* returns first material index */
void vkr_frame(vkr *r, const vkr_frame_desc *f);
vkr_stats vkr_get_stats(const vkr *r);

/* Default bindless slots the renderer registers at init, for callers that
 * build materials without a texture. */
enum { VKR_TEX_WHITE = 0, VKR_TEX_FLAT_NORMAL = 1, VKR_TEX_BLACK = 2 };

#endif
