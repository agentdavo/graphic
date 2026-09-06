/* render.h -- the renderer: eight stages in one function, in the order they
 * execute.
 *
 *   cull -> shadow atlas -> depth prepass -> light clusters
 *        -> forward opaque (colour + id + normal) -> transparent + world quads
 *        -> post (outline, tonemap, LUT) -> screen quads
 *
 * The caller owns the scene: instances, lights, bones and quads are plain
 * arrays it hands over every frame; static geometry and materials are
 * uploaded once. Textures are bindless slots the caller obtained from vkmin.
 * The renderer never allocates after init and never reads a cvar in a loop.
 * There is no scene graph, no entity, no physics: a game is arrays in, one
 * call, pixels out, and the ID target answers "what is under the mouse".
 *
 * Outside (v0.5, opt in with vkr_desc.outdoor): scatter -> existing cull,
 * shadows/prepass -> analytic sky -> opaque -> grass -> impostor batcher ->
 * water composite -> TAA -> bloom -> existing tonemap/overlay. Outdoor and
 * Scatter are plain render_shared.h descriptors. Indirect storage is sized to the
 * caller's max_instances, up to 16384; at most 16 scatter layers. Scatter
 * requires r_gpu_cull=1 and d_check_cull=0 (there is no CPU scatter list).
 * Terrain nodes remain ordinary instances. A tree cell reserves two instance
 * slots and one quad; its leaf part shares the trunk's hash/scale/rotation.
 * Mipmapped alpha textures are the caller's; the valley demonstrates coverage
 * preservation at upload. Tree impostors crossfade over 50..60% of the layer's
 * distance limit, and the mesh continues casting shadows in that interval.
 *
 * Pure reusable primitives: terrain/flow/scatter/grass/foliage/wind/sky/water
 * can serve 10_shooter and 11_rts; bloom/TAA can serve those or 14_anime.
 * These are integration candidates, not claims that those games use them yet.
 * No core feature is valley-only. Its camera route, morning schedule and
 * authored assets are demo features labelled in examples/20_valley.c.
 *
 * Set vkmin_desc.history when enabling cvar taa BEFORE vkmin_init, so frame
 * selection warms from zero. TAA reprojects camera motion, rejects depth
 * disocclusions, and clamps a 3x3 neighbourhood. It has no animated-object
 * velocity target: rapid foliage motion can still ghost. Exposure is fixed.
 * Water intersects a flat bounded plane analytically in a fullscreen shader;
 * it samples completed colour/depth, never the attachment it is writing.
 * Reflection is analytic sky. The noted parallel alternative is rendering
 * the actual scene with a mirrored camera into a planar reflection target;
 * that implementation (trees visible in water) is not included in v0.5.
 *
 * The caller owns the frame: vkr_frame records between the caller's
 * vkmin_frame_begin(ctx, NULL) and vkmin_frame_end(ctx).
 */
#ifndef VKMIN_RENDER_H
#define VKMIN_RENDER_H

#include "vkmin.h"
#include "render_geometry.h"

typedef struct vkr vkr;

typedef struct {
    int width, height;         /* maximum render size; targets are made once, at this size */
    int shadow_atlas;          /* texels on a side, power of two */
    size_t max_vertices, max_indices, max_skin_vertices;
    uint32_t max_meshes, max_materials, max_instances;
    vkmin_bytes fs;            /* game's SPIR-V composed from shaders/lib; empty = canonical PBR */
    bool outdoor;             /* create sky, scatter, water, bloom and temporal resources */
    const char *fs_path;       /* optional compiled shader path for hot reload */
} vkr_desc;

/* The look: everything the shader library reads that is not geometry. All
 * zero is plain PBR with no post-processing. */
typedef struct {
    uint32_t cel_ramp_tex;     /* 1D N.L ramp; 0 = the renderer's three-step default */
    float rim_strength, rim_power, spec_step;   /* cel(): rim light; stepped specular threshold, 0 = none */
    float shadow_tint[3];      /* cel(): colour multiplied into shadow; 0 = 0.5 grey */
    float fog[3], fog_density; /* exponential fog; density 0 = none */
    float outline;             /* 0..1 darkening on depth/normal edges; 0 = off */
    float outline_depth;       /* edge threshold as a fraction of view depth; 0 = 0.05 */
    uint32_t lut_tex; float lut_strength;  /* 256x16 grading strip; 0 = none */
} vkr_look;

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
    const Quad *quads; uint32_t quad_count;  /* sprites, particles, billboards, UI; drawn in order */
    const char *overlay_text;  /* '\n'-separated lines drawn at the top left, may be NULL */
    vkr_look look;
    const Outdoor *outdoor;    /* required with scatter; sky/water/material parameters */
    const Scatter *scatter; uint32_t scatter_count; /* regenerated on GPU every frame */
    vkmin_frame frame;         /* what vkmin_frame_begin returned: index, slot, size */
} vkr_frame_desc;

typedef struct {
    double pass_ms[8];         /* cull, shadows, prepass, clusters, opaque, transparent, tonemap, overlay */
    double outside_ms[5];     /* scatter, grass, sky, water, TAA (separate timestamp pairs) */
    double frame_ms;
    uint32_t draws_camera;     /* visible instances in the camera view, one frame late */
    uint32_t draws_shadow;     /* visible instances summed over shadow views */
    uint32_t shadow_views;
    uint32_t lights_shadowed;
    uint32_t triangles_camera;
    uint32_t cull_mismatches;  /* d_check_cull: GPU draw list entries the CPU reference disagreed with */
    size_t device_used, device_cap, ring_used, ring_cap;
} vkr_stats;

extern const char *const vkr_pass_names[8];

vkr *vkr_init(vkmin_ctx *gpu, const vkr_desc *desc);
void vkr_shutdown(vkr *r);
uint32_t vkr_upload_geometry(vkr *r, const vkr_geometry *g);     /* returns first mesh index */
uint32_t vkr_upload_materials(vkr *r, const Material *m, uint32_t count); /* returns first material index */
void vkr_frame(vkr *r, const vkr_frame_desc *f);
vkr_stats vkr_get_stats(const vkr *r);
vkr_stats vkr_finish(vkr *r); /* between frames: wait and check the final pending cull results */
/* Lays text out as SDF screen quads: top-left at (x, y) pixels, `px` tall,
 * '\n' starts a line. Returns the quad count. Pure but for the font slot. */
uint32_t vkr_text(const vkr *r, const char *text, float x, float y, float px, uint32_t rgba, Quad *out, uint32_t cap);
vkmin_image vkr_id_target(const vkr *r);   /* R32_UINT instance ids, for vkmin_pick */
uint32_t vkr_font_tex(const vkr *r);       /* the SDF font, for quads the game lays out itself */

/* Default bindless slots the renderer registers at init, for callers that
 * build materials without a texture. */
enum { VKR_TEX_WHITE = 0, VKR_TEX_FLAT_NORMAL = 1, VKR_TEX_BLACK = 2 };

#endif
