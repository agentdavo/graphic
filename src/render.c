/* render.c -- see render.h. The frame is one function, vkr_frame, written top
 * to bottom in the order the GPU executes it. Everything above it exists to
 * make that function readable: target creation, the shadow view builder, the
 * transparent sort, and the text layout for the overlay.
 */
#include "render.h"
#include "cvar.h"
#include "font.h"
#include "mat4.h"
#include "shaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VKR_ASSERT(cond, ...)                                                     \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fprintf(stderr, "%s:%d: render: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fputc('\n', stderr);                                                  \
            abort();                                                              \
        }                                                                         \
    } while (0)

enum {
    VKR_MAX_OVERLAY_QUADS = 4096,
    VKR_MAX_TRANSPARENT = 512,
    VKR_MAX_SHADOW_LIGHTS = 16,
    VKR_LOCAL_TILES = 48,   /* three quadrants of 16 tiles each */
    VKR_TIMESTAMPS = 9,
    VKR_FRAMES = 2
};

const char *const vkr_pass_names[8] = {"cull", "shadows", "prepass", "clusters",
                                       "opaque", "transparent", "tonemap", "overlay"};

/* Every cvar the frame reads, read once at the top and never again. */
typedef struct {
    bool shadows, normal_maps, clustered, prepass, gpu_cull, transparent, overlay, freeze, compact;
    int cascades, shadow_lights, debug, tonemap;
    float shadow_bias, normal_bias, cascade_lambda, exposure, shadow_distance;
    uint32_t max_lights;
} settings;

static settings read_settings(void) {
    settings s = {
        .shadows = cvar_get_bool(CV_r_shadows),
        .normal_maps = cvar_get_bool(CV_r_normal_maps),
        .clustered = cvar_get_bool(CV_r_clustered),
        .prepass = cvar_get_bool(CV_r_prepass),
        .gpu_cull = cvar_get_bool(CV_r_gpu_cull),
        .transparent = cvar_get_bool(CV_r_transparent),
        .overlay = cvar_get_bool(CV_r_overlay),
        .freeze = cvar_get_bool(CV_r_freeze_cull),
        .compact = cvar_get_bool(CV_r_cull_compact),
        .cascades = cvar_get_int(CV_r_cascades),
        .shadow_lights = cvar_get_int(CV_r_shadow_lights),
        .debug = cvar_get_int(CV_r_debug),
        .tonemap = cvar_get_int(CV_r_tonemap),
        .shadow_bias = cvar_get(CV_r_shadow_bias),
        .normal_bias = cvar_get(CV_r_normal_bias),
        .cascade_lambda = cvar_get(CV_r_cascade_lambda),
        .exposure = cvar_get(CV_r_exposure),
        .shadow_distance = cvar_get(CV_r_shadow_distance),
        .max_lights = (uint32_t)cvar_get_int(CV_r_max_lights),
    };
    if (s.cascades < 1) s.cascades = 1;
    if (s.cascades > (int)VKMIN_CASCADES) s.cascades = (int)VKMIN_CASCADES;
    if (s.shadow_lights > VKR_MAX_SHADOW_LIGHTS) s.shadow_lights = VKR_MAX_SHADOW_LIGHTS;
    if (s.shadow_lights < 0) s.shadow_lights = 0;
    if (s.debug == VKMIN_DEBUG_OVERDRAW) s.prepass = false; /* overdraw counts every fragment */
    return s;
}

struct vkr {
    vkmin_ctx *gpu;
    vkr_desc desc;

    vkmin_image hdr, depth, atlas, font;
    uint32_t hdr_tex, atlas_shadow_tex, atlas_raw_tex, font_tex;

    vkmin_pipe cull, cluster;
    vkmin_pipe depth_cull, depth_nocull;
    vkmin_pipe fwd_cull, fwd_nocull, fwd_blend;
    vkmin_pipe tonemap, overlay;

    vkmin_buffer vertices, indices, skin_vertices, meshes, materials;
    vkmin_buffer draw_cmds, draw_counts, cluster_lights;
    uint32_t vertex_count, index_count, skin_count, mesh_count, material_count;

    /* Count readback: the ring block written this frame is read when the same
     * slot comes round again, by which time its fence has been waited. */
    const uint32_t *counts_host[VKR_FRAMES];
    uint32_t counts_views[VKR_FRAMES];
    bool counts_valid[VKR_FRAMES];

    /* Host copies of the two tables the CPU-side paths need: the reference
     * cull and the transparent sort. A few kilobytes. */
    Mesh *host_meshes;
    Material *host_materials;

    mat4 frozen_view_proj;
    bool frozen;
    vkr_stats stats;
};

/* ------------------------------------------------------------- creation --- */

static uint32_t register_solid(vkr *r, uint8_t red, uint8_t green, uint8_t blue, const char *label) {
    const uint8_t px[4] = {red, green, blue, 255};
    const vkmin_image img = vkmin_make_image(r->gpu, &(vkmin_image_desc){.width = 1, .height = 1, .format = VKMIN_FMT_RGBA8_UNORM,
                                                                          .usage = VKMIN_IMAGE_SAMPLED, .label = label});
    vkmin_image_upload(r->gpu, img, 0, px, sizeof px);
    return vkmin_register_texture(r->gpu, img, VKMIN_SAMPLER_LINEAR_REPEAT);
}

vkr *vkr_init(vkmin_ctx *gpu, const vkr_desc *desc) {
    VKR_ASSERT(gpu && desc, "vkr_init: null argument");
    VKR_ASSERT(desc->width > 0 && desc->height > 0, "vkr_init: bad size");
    VKR_ASSERT(desc->shadow_atlas >= 256 && (desc->shadow_atlas & (desc->shadow_atlas - 1)) == 0,
               "shadow atlas must be a power of two >= 256");
    VKR_ASSERT(desc->max_instances <= VKMIN_MAX_DRAWS, "max_instances above VKMIN_MAX_DRAWS");
    vkr *r = calloc(1, sizeof *r);
    VKR_ASSERT(r != NULL, "out of memory");
    r->gpu = gpu;
    r->desc = *desc;
    r->host_meshes = calloc(desc->max_meshes, sizeof(Mesh));
    r->host_materials = calloc(desc->max_materials, sizeof(Material));
    VKR_ASSERT(r->host_meshes && r->host_materials, "out of memory");

    /* Default texture slots 0, 1, 2, in that order, so materials without a
     * map index something sensible. */
    const uint32_t white = register_solid(r, 255, 255, 255, "vkr.white");
    const uint32_t flat = register_solid(r, 128, 128, 255, "vkr.flat_normal");
    const uint32_t black = register_solid(r, 0, 0, 0, "vkr.black");
    VKR_ASSERT(white == VKR_TEX_WHITE && flat == VKR_TEX_FLAT_NORMAL && black == VKR_TEX_BLACK,
               "renderer must be the first to register textures");

    r->hdr = vkmin_make_image(gpu, &(vkmin_image_desc){.width = desc->width, .height = desc->height,
                                                       .format = VKMIN_FMT_R11G11B10_FLOAT,
                                                       .usage = VKMIN_IMAGE_COLOR | VKMIN_IMAGE_SAMPLED, .label = "vkr.hdr"});
    r->depth = vkmin_make_image(gpu, &(vkmin_image_desc){.width = desc->width, .height = desc->height,
                                                         .format = VKMIN_FMT_D32_FLOAT, .usage = VKMIN_IMAGE_DEPTH, .label = "vkr.depth"});
    r->atlas = vkmin_make_image(gpu, &(vkmin_image_desc){.width = desc->shadow_atlas, .height = desc->shadow_atlas,
                                                         .format = VKMIN_FMT_D32_FLOAT,
                                                         .usage = VKMIN_IMAGE_DEPTH | VKMIN_IMAGE_SAMPLED, .label = "vkr.shadow_atlas"});
    r->hdr_tex = vkmin_register_texture(gpu, r->hdr, VKMIN_SAMPLER_LINEAR_CLAMP);
    r->atlas_shadow_tex = vkmin_register_texture(gpu, r->atlas, VKMIN_SAMPLER_SHADOW);
    r->atlas_raw_tex = vkmin_register_texture(gpu, r->atlas, VKMIN_SAMPLER_NEAREST_CLAMP);

    /* The baked font: coverage in every channel, sampled as .r. */
    uint8_t *font_px = malloc((size_t)FONT_ATLAS_W * FONT_ATLAS_H * 4);
    VKR_ASSERT(font_px != NULL, "out of memory");
    for (int i = 0; i < FONT_ATLAS_W * FONT_ATLAS_H; ++i) {
        font_px[i * 4] = font_px[i * 4 + 1] = font_px[i * 4 + 2] = font_px[i * 4 + 3] = FONT_ATLAS[i];
    }
    r->font = vkmin_make_image(gpu, &(vkmin_image_desc){.width = FONT_ATLAS_W, .height = FONT_ATLAS_H,
                                                        .format = VKMIN_FMT_RGBA8_UNORM, .usage = VKMIN_IMAGE_SAMPLED, .label = "vkr.font"});
    vkmin_image_upload(gpu, r->font, 0, font_px, (size_t)FONT_ATLAS_W * FONT_ATLAS_H * 4);
    free(font_px);
    r->font_tex = vkmin_register_texture(gpu, r->font, VKMIN_SAMPLER_LINEAR_CLAMP);

    /* Static geometry arenas and the GPU-written buffers. */
    r->vertices = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = desc->max_vertices * sizeof(Vertex), .label = "vkr.vertices"});
    r->indices = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = desc->max_indices * sizeof(uint32_t), .label = "vkr.indices"});
    r->skin_vertices = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = (desc->max_skin_vertices ? desc->max_skin_vertices : 1) * sizeof(SkinVertex), .label = "vkr.skin"});
    r->meshes = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = desc->max_meshes * sizeof(Mesh), .label = "vkr.meshes"});
    r->materials = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = desc->max_materials * sizeof(Material), .label = "vkr.materials"});
    r->draw_cmds = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = (size_t)VKMIN_MAX_VIEWS * 2 * VKMIN_MAX_DRAWS * sizeof(DrawCmd), .label = "vkr.draw_cmds"});
    r->draw_counts = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = (size_t)VKMIN_MAX_VIEWS * 2 * sizeof(uint32_t), .label = "vkr.draw_counts"});
    r->cluster_lights = vkmin_make_buffer(gpu, &(vkmin_buffer_desc){.size = (size_t)VKMIN_CLUSTER_COUNT * VKMIN_CLUSTER_STRIDE * sizeof(uint32_t), .label = "vkr.cluster_lights"});

    /* Nine pipelines for the whole engine, all created here, all against the
     * one layout. */
    r->cull = vkmin_make_compute(gpu, cull_comp_spv, sizeof cull_comp_spv, "vkr.cull");
    r->cluster = vkmin_make_compute(gpu, cluster_comp_spv, sizeof cluster_comp_spv, "vkr.cluster");
    const vkmin_pipe_desc depth_desc = {.vs = depth_vert_spv, .vs_bytes = sizeof depth_vert_spv,
                                        .fs = depth_frag_spv, .fs_bytes = sizeof depth_frag_spv,
                                        .depth = true, .depth_write = true, .depth_compare = VKMIN_CMP_LESS,
                                        .depth_bias = true, .cull = VKMIN_CULL_BACK, .label = "vkr.depth"};
    vkmin_pipe_desc d = depth_desc;
    r->depth_cull = vkmin_make_pipeline(gpu, &d);
    d.cull = VKMIN_CULL_NONE;
    d.label = "vkr.depth.double_sided";
    r->depth_nocull = vkmin_make_pipeline(gpu, &d);

    /* LESS_EQUAL with write on works with and without the prepass: after a
     * prepass it behaves as EQUAL, without one it is the ordinary depth test.
     * One pipeline set instead of two, for a negligible cost. */
    const vkmin_pipe_desc fwd_desc = {.vs = scene_vert_spv, .vs_bytes = sizeof scene_vert_spv,
                                      .fs = scene_frag_spv, .fs_bytes = sizeof scene_frag_spv,
                                      .color_format = VKMIN_FMT_R11G11B10_FLOAT, .depth = true, .depth_write = true,
                                      .depth_compare = VKMIN_CMP_LESS_EQUAL, .cull = VKMIN_CULL_BACK, .label = "vkr.forward"};
    vkmin_pipe_desc f = fwd_desc;
    r->fwd_cull = vkmin_make_pipeline(gpu, &f);
    f.cull = VKMIN_CULL_NONE;
    f.label = "vkr.forward.double_sided";
    r->fwd_nocull = vkmin_make_pipeline(gpu, &f);
    f.depth_write = false;
    f.blend = true;
    f.label = "vkr.forward.blend";
    r->fwd_blend = vkmin_make_pipeline(gpu, &f);

    const vkmin_format bb = vkmin_backbuffer_format(gpu);
    r->tonemap = vkmin_make_pipeline(gpu, &(vkmin_pipe_desc){.vs = fullscreen_vert_spv, .vs_bytes = sizeof fullscreen_vert_spv,
                                                              .fs = tonemap_frag_spv, .fs_bytes = sizeof tonemap_frag_spv,
                                                              .color_format = bb, .cull = VKMIN_CULL_NONE, .label = "vkr.tonemap"});
    r->overlay = vkmin_make_pipeline(gpu, &(vkmin_pipe_desc){.vs = overlay_vert_spv, .vs_bytes = sizeof overlay_vert_spv,
                                                              .fs = overlay_frag_spv, .fs_bytes = sizeof overlay_frag_spv,
                                                              .color_format = bb, .cull = VKMIN_CULL_NONE, .blend = true, .label = "vkr.overlay"});
    return r;
}

void vkr_shutdown(vkr *r) {
    /* Every GPU object belongs to the vkmin context and dies with it. */
    if (!r) return;
    free(r->host_meshes);
    free(r->host_materials);
    free(r);
}

uint32_t vkr_upload_geometry(vkr *r, const vkr_geometry *g) {
    VKR_ASSERT(r && g, "vkr_upload_geometry: null argument");
    VKR_ASSERT(r->vertex_count + g->vertex_count <= r->desc.max_vertices, "vertex arena full");
    VKR_ASSERT(r->index_count + g->index_count <= r->desc.max_indices, "index arena full");
    VKR_ASSERT(r->skin_count + g->skin_vertex_count <= r->desc.max_skin_vertices || g->skin_vertex_count == 0, "skin arena full");
    VKR_ASSERT(r->mesh_count + g->mesh_count <= r->desc.max_meshes, "mesh table full");

    vkmin_buffer_upload(r->gpu, r->vertices, r->vertex_count * sizeof(Vertex), g->vertices, g->vertex_count * sizeof(Vertex));
    vkmin_buffer_upload(r->gpu, r->indices, r->index_count * sizeof(uint32_t), g->indices, g->index_count * sizeof(uint32_t));
    if (g->skin_vertex_count) {
        vkmin_buffer_upload(r->gpu, r->skin_vertices, r->skin_count * sizeof(SkinVertex), g->skin_vertices, g->skin_vertex_count * sizeof(SkinVertex));
    }
    Mesh *rebased = malloc(g->mesh_count * sizeof(Mesh));
    VKR_ASSERT(rebased != NULL, "out of memory");
    for (uint32_t i = 0; i < g->mesh_count; ++i) {
        rebased[i] = g->meshes[i];
        rebased[i].first_index += r->index_count;
        rebased[i].vertex_offset += r->vertex_count;
        if (rebased[i].skin_offset != VKMIN_NONE) rebased[i].skin_offset += r->skin_count;
    }
    vkmin_buffer_upload(r->gpu, r->meshes, r->mesh_count * sizeof(Mesh), rebased, g->mesh_count * sizeof(Mesh));
    memcpy(r->host_meshes + r->mesh_count, rebased, g->mesh_count * sizeof(Mesh));
    free(rebased);
    const uint32_t first = r->mesh_count;
    r->vertex_count += g->vertex_count;
    r->index_count += g->index_count;
    r->skin_count += g->skin_vertex_count;
    r->mesh_count += g->mesh_count;
    return first;
}

uint32_t vkr_upload_materials(vkr *r, const Material *m, uint32_t count) {
    VKR_ASSERT(r && m, "vkr_upload_materials: null argument");
    VKR_ASSERT(r->material_count + count <= r->desc.max_materials, "material table full");
    vkmin_buffer_upload(r->gpu, r->materials, r->material_count * sizeof(Material), m, count * sizeof(Material));
    memcpy(r->host_materials + r->material_count, m, count * sizeof(Material));
    const uint32_t first = r->material_count;
    r->material_count += count;
    return first;
}

vkr_stats vkr_get_stats(const vkr *r) { return r->stats; }

/* ---------------------------------------------------------------- views --- */

/* Gribb-Hartmann plane extraction for Vulkan clip space (0 <= z <= w). */
static void view_planes(View *v) {
    const float *m = v->view_proj.m;
    /* rows of the matrix: row r = (m[r], m[4+r], m[8+r], m[12+r]) */
    const float rows[4][4] = {{m[0], m[4], m[8], m[12]}, {m[1], m[5], m[9], m[13]},
                              {m[2], m[6], m[10], m[14]}, {m[3], m[7], m[11], m[15]}};
    float planes[6][4];
    for (int k = 0; k < 4; ++k) {
        planes[0][k] = rows[3][k] + rows[0][k]; /* left   */
        planes[1][k] = rows[3][k] - rows[0][k]; /* right  */
        planes[2][k] = rows[3][k] + rows[1][k]; /* bottom */
        planes[3][k] = rows[3][k] - rows[1][k]; /* top    */
        planes[4][k] = rows[2][k];              /* near   */
        planes[5][k] = rows[3][k] - rows[2][k]; /* far    */
    }
    for (int p = 0; p < 6; ++p) {
        const float len = sqrtf(planes[p][0] * planes[p][0] + planes[p][1] * planes[p][1] + planes[p][2] * planes[p][2]);
        const float inv = len > 0.0f ? 1.0f / len : 1.0f;
        v->planes[p] = (vec4){planes[p][0] * inv, planes[p][1] * inv, planes[p][2] * inv, planes[p][3] * inv};
    }
}

static vec3 perpendicular_up(vec3 dir) {
    return fabsf(dir.y) < 0.99f ? (vec3){0, 1, 0} : (vec3){1, 0, 0};
}

/* The eight corners of the camera frustum between two view-space depths. */
static void frustum_slice_corners(mat4 inv_view, mat4 proj, float d0, float d1, vec3 out[8]) {
    const mat4 inv_proj = mat4_inverse(proj);
    for (int k = 0; k < 8; ++k) {
        const float nx = (k & 1) ? 1.0f : -1.0f, ny = (k & 2) ? 1.0f : -1.0f;
        const vec3 on_near = mat4_mul_point(inv_proj, (vec3){nx, ny, 0.0f});
        const float depth = (k & 4) ? d1 : d0;
        const vec3 view_p = vec3_scale(on_near, depth / -on_near.z);
        out[k] = mat4_mul_point(inv_view, view_p);
    }
}

typedef struct {
    View views[VKMIN_MAX_VIEWS];
    uint32_t count;
    uint32_t shadow_draw_views; /* views 1..count-1 are shadow views */
    vec4 cascade_splits;
    uint32_t lights_shadowed;
} view_set;

/* Assigns an atlas rectangle to a view, in texels. */
static void view_set_tile(View *v, int atlas, int x, int y, int size, float depth_range_world, float world_extent,
                          const settings *s) {
    const float inv = 1.0f / (float)atlas;
    v->atlas_rect = (vec4){(float)x * inv, (float)y * inv, (float)size * inv, (float)size * inv};
    const float world_texel = world_extent / (float)size;
    v->texel = (vec4){inv, s->shadow_bias * world_texel / depth_range_world, s->normal_bias * world_texel, 0.0f};
}

static void build_sun_cascades(view_set *vs, const vkr_frame_desc *f, const settings *s, vec3 to_sun,
                               int atlas, float scene_radius) {
    const float near = f->near, far = fminf(f->far, s->shadow_distance);
    float splits[VKMIN_CASCADES + 1];
    splits[0] = near;
    for (int i = 1; i <= s->cascades; ++i) {
        const float t = (float)i / (float)s->cascades;
        const float log_split = near * powf(far / near, t);
        const float lin_split = near + (far - near) * t;
        splits[i] = s->cascade_lambda * log_split + (1.0f - s->cascade_lambda) * lin_split;
    }
    for (int i = s->cascades; i < (int)VKMIN_CASCADES; ++i) splits[i + 1] = far;
    vs->cascade_splits = (vec4){splits[1], splits[2], splits[3], splits[4]};

    const mat4 inv_view = mat4_inverse(f->view);
    const int cell = atlas / 4; /* four cascades share quadrant 0 */
    for (int c = 0; c < s->cascades; ++c) {
        vec3 corners[8];
        frustum_slice_corners(inv_view, f->proj, splits[c], splits[c + 1], corners);
        vec3 centre = {0, 0, 0};
        for (int k = 0; k < 8; ++k) centre = vec3_add(centre, corners[k]);
        centre = vec3_scale(centre, 1.0f / 8.0f);
        float radius = 0.0f;
        for (int k = 0; k < 8; ++k) radius = fmaxf(radius, vec3_length(vec3_sub(corners[k], centre)));
        /* Snap the centre to the shadow texel grid so the cascade does not
         * shimmer as the camera moves; the animation is deterministic anyway
         * but the shimmer is still ugly. */
        const float world_texel = 2.0f * radius / (float)cell;
        const vec3 eye_dir = to_sun;
        const mat4 light_view = mat4_look_at(vec3_add(centre, vec3_scale(eye_dir, radius + scene_radius)), centre,
                                             perpendicular_up(eye_dir));
        vec3 centre_ls = mat4_mul_point(light_view, centre);
        centre_ls.x = floorf(centre_ls.x / world_texel) * world_texel;
        centre_ls.y = floorf(centre_ls.y / world_texel) * world_texel;
        const vec3 snapped = mat4_mul_point(mat4_inverse(light_view), centre_ls);
        const mat4 snapped_view = mat4_look_at(vec3_add(snapped, vec3_scale(eye_dir, radius + scene_radius)), snapped,
                                               perpendicular_up(eye_dir));
        const float depth_range = 2.0f * (radius + scene_radius);
        const mat4 proj = mat4_ortho(-radius, radius, -radius, radius, 0.0f, depth_range);
        View *v = &vs->views[vs->count++];
        *v = (View){.view_proj = mat4_mul(proj, snapped_view), .flags = 1};
        view_planes(v);
        view_set_tile(v, atlas, (c & 1) * cell, (c >> 1) * cell, cell, depth_range, 2.0f * radius, s);
    }
}

typedef struct { float importance; uint32_t index; } light_rank;

static int rank_compare(const void *a, const void *b) {
    const light_rank *x = a, *y = b;
    if (x->importance > y->importance) return -1;
    if (x->importance < y->importance) return 1;
    return x->index < y->index ? -1 : x->index > y->index; /* stable tiebreak: deterministic */
}

/* Builds every View for the frame and rewrites lights[].shadow_view. */
static view_set build_views(const vkr *r, const vkr_frame_desc *f, const settings *s, Light *lights,
                            uint32_t light_count, uint32_t *sun_index, vec3 *to_sun_out) {
    view_set vs = {0};
    const int atlas = r->desc.shadow_atlas;

    /* View 0: the camera. Frozen culling keeps yesterday's planes with today's
     * matrices, which is how you see what the cull is throwing away. */
    View *cam = &vs.views[vs.count++];
    *cam = (View){.view_proj = mat4_mul(f->proj, f->view)};
    View cull_from = *cam;
    if (s->freeze && r->frozen) cull_from.view_proj = r->frozen_view_proj;
    view_planes(&cull_from);
    memcpy(cam->planes, cull_from.planes, sizeof cam->planes);

    *sun_index = VKMIN_NONE;
    vec3 to_sun = {0, 1, 0};
    for (uint32_t i = 0; i < light_count; ++i) {
        lights[i].shadow_view = VKMIN_NONE;
        lights[i].shadow_views = 0;
        if (lights[i].type == VKMIN_LIGHT_DIRECTIONAL && *sun_index == VKMIN_NONE) {
            *sun_index = i;
            const vec3 d = {lights[i].dir_cone.x, lights[i].dir_cone.y, lights[i].dir_cone.z};
            to_sun = vec3_normalize(vec3_scale(d, -1.0f));
        }
    }
    *to_sun_out = to_sun;
    if (!s->shadows) return vs;

    const float scene_radius = fmaxf(s->shadow_distance, 30.0f);
    if (*sun_index != VKMIN_NONE) {
        lights[*sun_index].shadow_view = vs.count;
        lights[*sun_index].shadow_views = (uint32_t)s->cascades;
        build_sun_cascades(&vs, f, s, to_sun, atlas, scene_radius);
    }

    /* Local lights: rank by projected size, hand out tiles until they run out. */
    light_rank ranks[VKMIN_MAX_LIGHTS];
    uint32_t rank_count = 0;
    const vec3 cam_pos = {f->camera_pos.x, f->camera_pos.y, f->camera_pos.z};
    for (uint32_t i = 0; i < light_count; ++i) {
        if (lights[i].type == VKMIN_LIGHT_DIRECTIONAL) continue;
        const vec3 p = {lights[i].pos_radius.x, lights[i].pos_radius.y, lights[i].pos_radius.z};
        const float dist = fmaxf(vec3_length(vec3_sub(p, cam_pos)) - lights[i].pos_radius.w, 0.1f);
        ranks[rank_count++] = (light_rank){.importance = lights[i].pos_radius.w / dist, .index = i};
    }
    if (rank_count > 1) qsort(ranks, rank_count, sizeof ranks[0], rank_compare);

    const int tile = atlas / 8;
    int next_tile = 0;
    for (uint32_t k = 0; k < rank_count && (int)vs.lights_shadowed < s->shadow_lights; ++k) {
        Light *l = &lights[ranks[k].index];
        const int faces = l->type == VKMIN_LIGHT_POINT ? 6 : 1;
        if (next_tile + faces > VKR_LOCAL_TILES || vs.count + (uint32_t)faces > VKMIN_MAX_VIEWS) break;
        l->shadow_view = vs.count;
        l->shadow_views = (uint32_t)faces;
        const vec3 pos = {l->pos_radius.x, l->pos_radius.y, l->pos_radius.z};
        const float radius = l->pos_radius.w, near = 0.05f;
        for (int face = 0; face < faces; ++face) {
            vec3 dir, up;
            float fovy;
            if (l->type == VKMIN_LIGHT_POINT) {
                static const vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                static const vec3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
                dir = dirs[face];
                up = ups[face];
                fovy = 3.14159265f * 0.5f;
            } else {
                dir = vec3_normalize((vec3){l->dir_cone.x, l->dir_cone.y, l->dir_cone.z});
                up = perpendicular_up(dir);
                fovy = 2.0f * acosf(fminf(fmaxf(l->dir_cone.w, -1.0f), 1.0f)) + 0.1f;
                if (fovy > 3.0f) fovy = 3.0f;
            }
            const mat4 view = mat4_look_at(pos, vec3_add(pos, dir), up);
            const mat4 proj = mat4_perspective(fovy, 1.0f, near, radius);
            View *v = &vs.views[vs.count++];
            *v = (View){.view_proj = mat4_mul(proj, view), .flags = 1};
            view_planes(v);
            const int t = next_tile++;
            const int quadrant = 1 + t / 16, kx = (t % 16) % 4, ky = (t % 16) / 4;
            const int ox = (quadrant & 1) ? atlas / 2 : 0, oy = (quadrant & 2) ? atlas / 2 : 0;
            /* Perspective depth is non-linear; the biases below are in units
             * that behave reasonably across the light's range. */
            view_set_tile(v, atlas, ox + kx * tile, oy + ky * tile, tile, radius * 20.0f,
                          2.0f * tanf(fovy * 0.5f) * radius * 0.25f, s);
        }
        vs.lights_shadowed++;
    }
    vs.shadow_draw_views = vs.count - 1;
    return vs;
}

/* -------------------------------------------------------- CPU reference --- */

/* The reference cull: same test as cull.comp, on the CPU, in instance order.
 * Slow and obviously right; flip r_gpu_cull to compare. */
static uint32_t cpu_cull(const View *v, const Instance *inst, uint32_t n, const Material *mats,
                         DrawCmd *out, const Mesh *meshes, uint32_t list) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (inst[i].flags & VKMIN_INST_HIDDEN) continue;
        const Material *m = &mats[inst[i].material];
        if (m->flags & VKMIN_MAT_BLEND) continue;
        if (((m->flags & VKMIN_MAT_DOUBLE_SIDED) ? 1u : 0u) != list) continue;
        bool visible = true;
        for (int p = 0; p < 6 && visible; ++p) {
            const float d = v->planes[p].x * inst[i].bounds.x + v->planes[p].y * inst[i].bounds.y +
                            v->planes[p].z * inst[i].bounds.z + v->planes[p].w;
            if (d < -inst[i].bounds.w) visible = false;
        }
        if (!visible) continue;
        const Mesh *mesh = &meshes[inst[i].mesh];
        out[count++] = (DrawCmd){.index_count = mesh->index_count, .instance_count = 1, .first_index = mesh->first_index,
                                 .vertex_offset = (int32_t)mesh->vertex_offset, .first_instance = i};
    }
    return count;
}

/* --------------------------------------------------------- transparents --- */

typedef struct { float depth; uint32_t index; } depth_key;

static int far_first(const void *a, const void *b) {
    const depth_key *x = a, *y = b;
    if (x->depth > y->depth) return -1;
    if (x->depth < y->depth) return 1;
    return x->index < y->index ? -1 : x->index > y->index;
}

static uint32_t sort_transparents(const vkr_frame_desc *f, const Material *mats, const Mesh *meshes, DrawCmd *out) {
    depth_key keys[VKR_MAX_TRANSPARENT];
    uint32_t n = 0;
    for (uint32_t i = 0; i < f->instance_count && n < VKR_MAX_TRANSPARENT; ++i) {
        const Instance *inst = &f->instances[i];
        if ((inst->flags & VKMIN_INST_HIDDEN) || !(mats[inst->material].flags & VKMIN_MAT_BLEND)) continue;
        const float dx = inst->bounds.x - f->camera_pos.x, dy = inst->bounds.y - f->camera_pos.y, dz = inst->bounds.z - f->camera_pos.z;
        keys[n++] = (depth_key){.depth = dx * dx + dy * dy + dz * dz, .index = i};
    }
    if (n > 1) qsort(keys, n, sizeof keys[0], far_first);
    for (uint32_t k = 0; k < n; ++k) {
        const Instance *inst = &f->instances[keys[k].index];
        const Mesh *mesh = &meshes[inst->mesh];
        out[k] = (DrawCmd){.index_count = mesh->index_count, .instance_count = 1, .first_index = mesh->first_index,
                           .vertex_offset = (int32_t)mesh->vertex_offset, .first_instance = keys[k].index};
    }
    return n;
}

/* ---------------------------------------------------------------- text ---- */

static uint32_t layout_text(const char *text, OverlayQuad *quads, uint32_t cap) {
    if (!text) return 0;
    uint32_t n = 0;
    float x = 8.0f, y = 8.0f + FONT_LINE_HEIGHT;
    for (const char *p = text; *p && n + 2 <= cap; ++p) {
        if (*p == '\n') {
            x = 8.0f;
            y += FONT_LINE_HEIGHT + 2.0f;
            continue;
        }
        const int ch = (unsigned char)*p;
        if (ch < FONT_FIRST || ch >= FONT_FIRST + FONT_COUNT) continue;
        const font_glyph *g = &FONT_GLYPHS[ch - FONT_FIRST];
        const vec4 uv = {(float)g->x0 / FONT_ATLAS_W, (float)g->y0 / FONT_ATLAS_H, (float)g->x1 / FONT_ATLAS_W, (float)g->y1 / FONT_ATLAS_H};
        const float w = (float)(g->x1 - g->x0), h = (float)(g->y1 - g->y0);
        const float gx = x + g->xoff, gy = y + g->yoff;
        /* drop shadow first, then the glyph, so text reads over anything */
        quads[n++] = (OverlayQuad){.rect = {gx + 1, gy + 1, gx + w + 1, gy + h + 1}, .uv = uv, .color = 0xff000000u};
        quads[n++] = (OverlayQuad){.rect = {gx, gy, gx + w, gy + h}, .uv = uv, .color = 0xffffffffu};
        x += g->xadvance;
    }
    return n;
}

/* ---------------------------------------------------------------- frame --- */

static void draw_lists(vkr *r, uint32_t view, vkmin_pipe culled, vkmin_pipe double_sided, const Push *base,
                       bool gpu_cull, const uint64_t *host_cmds, const uint32_t *host_counts) {
    Push push = *base;
    push.view = view;
    for (uint32_t list = 0; list < 2; ++list) {
        vkmin_bind_pipeline(r->gpu, list == 0 ? culled : double_sided);
        vkmin_push(r->gpu, &push);
        if (gpu_cull) {
            const size_t cmd_offset = (size_t)(view * 2 + list) * VKMIN_MAX_DRAWS * sizeof(DrawCmd);
            vkmin_draw_indexed_indirect_count(r->gpu, r->draw_cmds, cmd_offset, r->draw_counts,
                                              (size_t)(view * 2 + list) * sizeof(uint32_t), VKMIN_MAX_DRAWS);
        } else {
            vkmin_draw_indexed_indirect_host(r->gpu, host_cmds[view * 2 + list], host_counts[view * 2 + list]);
        }
    }
}

void vkr_frame(vkr *r, const vkr_frame_desc *f) {
    VKR_ASSERT(r && f, "vkr_frame: null argument");
    VKR_ASSERT(f->instance_count <= r->desc.max_instances, "too many instances (%u > %u)", f->instance_count, r->desc.max_instances);
    VKR_ASSERT(f->light_count <= VKMIN_MAX_LIGHTS, "too many lights");
    vkmin_ctx *gpu = r->gpu;
    const settings s = read_settings();

    if (s.freeze && !r->frozen) {
        r->frozen_view_proj = mat4_mul(f->proj, f->view);
        r->frozen = true;
    }
    if (!s.freeze) r->frozen = false;

    int win_w = 0, win_h = 0;
    vkmin_size(gpu, &win_w, &win_h);
    /* Targets are made once at the maximum size; a larger window renders at
     * the maximum and is upscaled by the tonemap pass. */
    const int rw = win_w < r->desc.width ? win_w : r->desc.width;
    const int rh = win_h < r->desc.height ? win_h : r->desc.height;

    vkmin_frame_begin(gpu);
    const uint32_t slot = vkmin_frame_slot(gpu);

    /* Stats from the frame that last used this slot: its fence is waited. */
    double ts[VKR_TIMESTAMPS];
    const int ts_n = vkmin_timestamps_read(gpu, ts, VKR_TIMESTAMPS);
    for (int i = 0; i + 1 < ts_n && i < 8; ++i) r->stats.pass_ms[i] = ts[i + 1] - ts[i];
    if (ts_n == VKR_TIMESTAMPS) r->stats.frame_ms = ts[VKR_TIMESTAMPS - 1] - ts[0];
    if (r->counts_valid[slot]) {
        r->stats.draws_camera = r->counts_host[slot][0] + r->counts_host[slot][1];
        r->stats.draws_shadow = 0;
        for (uint32_t v = 1; v < r->counts_views[slot]; ++v) {
            r->stats.draws_shadow += r->counts_host[slot][v * 2] + r->counts_host[slot][v * 2 + 1];
        }
    }
    vkmin_memory_stats(gpu, &r->stats.device_used, &r->stats.device_cap, &r->stats.ring_used, &r->stats.ring_cap);

    /* --- per-frame data into the ring ------------------------------------ */
    uint64_t lights_addr = 0, views_addr = 0, inst_addr = 0, bones_addr = 0, frame_addr = 0;
    const uint32_t light_count = f->light_count < s.max_lights ? f->light_count : s.max_lights;
    Light *lights = vkmin_ring_alloc(gpu, (light_count ? light_count : 1) * sizeof(Light), &lights_addr);
    memcpy(lights, f->lights, light_count * sizeof(Light));
    uint32_t sun_index = VKMIN_NONE;
    vec3 to_sun;
    view_set vs = build_views(r, f, &s, lights, light_count, &sun_index, &to_sun);
    View *views = vkmin_ring_alloc(gpu, vs.count * sizeof(View), &views_addr);
    memcpy(views, vs.views, vs.count * sizeof(View));
    Instance *instances = vkmin_ring_alloc(gpu, (f->instance_count ? f->instance_count : 1) * sizeof(Instance), &inst_addr);
    memcpy(instances, f->instances, f->instance_count * sizeof(Instance));
    mat4 *bones = vkmin_ring_alloc(gpu, (f->bone_count ? f->bone_count : 1) * sizeof(mat4), &bones_addr);
    if (f->bone_count) memcpy(bones, f->bones, f->bone_count * sizeof(mat4));
    Frame *frame = vkmin_ring_alloc(gpu, sizeof(Frame), &frame_addr);

    const float cluster_far = fminf(f->far, 200.0f);
    const float log_ratio = logf(cluster_far / f->near);
    *frame = (Frame){
        .view = f->view,
        .proj = f->proj,
        .view_proj = mat4_mul(f->proj, f->view),
        .inv_view_proj = mat4_inverse(mat4_mul(f->proj, f->view)),
        .camera_pos = {f->camera_pos.x, f->camera_pos.y, f->camera_pos.z, (float)f->frame_index},
        .cascade_splits = vs.cascade_splits,
        .cluster_params = {(float)VKMIN_CLUSTER_Z / log_ratio, -(float)VKMIN_CLUSTER_Z * logf(f->near) / log_ratio, f->near, cluster_far},
        .cluster_dims = {VKMIN_CLUSTER_X, VKMIN_CLUSTER_Y, VKMIN_CLUSTER_Z, 0},
        .screen = {(float)rw, (float)rh, 1.0f / (float)rw, 1.0f / (float)rh},
        .ambient = {cvar_get(CV_r_ambient), cvar_get(CV_r_ambient), cvar_get(CV_r_ambient), s.debug != 0 ? 1.0f : s.exposure},
        .sun = {to_sun.x, to_sun.y, to_sun.z, 0.0f},
        .light_count = light_count,
        .view_count = vs.count,
        .debug_mode = (uint32_t)s.debug,
        .flags = (s.shadows ? VKMIN_FRAME_SHADOWS : 0u) | (s.normal_maps ? VKMIN_FRAME_NORMAL_MAPS : 0u) |
                 (s.clustered ? VKMIN_FRAME_CLUSTERED : 0u),
        .frame_index = f->frame_index,
        .sun_light = sun_index,
        .instance_count = f->instance_count,
        .shadow_atlas_tex = r->atlas_shadow_tex,
        .vertices = vkmin_buffer_addr(gpu, r->vertices),
        .skin_vertices = vkmin_buffer_addr(gpu, r->skin_vertices),
        .meshes = vkmin_buffer_addr(gpu, r->meshes),
        .materials = vkmin_buffer_addr(gpu, r->materials),
        .instances = inst_addr,
        .lights = lights_addr,
        .views = views_addr,
        .bones = bones_addr,
        .draw_cmds = vkmin_buffer_addr(gpu, r->draw_cmds),
        .draw_counts = vkmin_buffer_addr(gpu, r->draw_counts),
        .cluster_lights = vkmin_buffer_addr(gpu, r->cluster_lights),
    };
    const Push base_push = {.frame = frame_addr};

    /* The CPU reference cull writes its lists into the ring; only when selected. */
    uint64_t host_cmds[VKMIN_MAX_VIEWS * 2] = {0};
    uint32_t host_counts[VKMIN_MAX_VIEWS * 2] = {0};
    if (!s.gpu_cull) {
        for (uint32_t v = 0; v < vs.count; ++v) {
            for (uint32_t list = 0; list < 2; ++list) {
                DrawCmd *cmds = vkmin_ring_alloc(gpu, (f->instance_count ? f->instance_count : 1) * sizeof(DrawCmd), &host_cmds[v * 2 + list]);
                host_counts[v * 2 + list] = cpu_cull(&vs.views[v], f->instances, f->instance_count, r->host_materials, cmds, r->host_meshes, list);
            }
        }
    }
    uint64_t transparent_addr = 0;
    uint32_t transparent_count = 0;
    if (s.transparent) {
        DrawCmd *cmds = vkmin_ring_alloc(gpu, VKR_MAX_TRANSPARENT * sizeof(DrawCmd), &transparent_addr);
        transparent_count = sort_transparents(f, r->host_materials, r->host_meshes, cmds);
    }
    uint64_t quads_addr = 0;
    uint32_t quad_count = 0;
    if (s.overlay && f->overlay_text) {
        OverlayQuad *quads = vkmin_ring_alloc(gpu, VKR_MAX_OVERLAY_QUADS * sizeof(OverlayQuad), &quads_addr);
        quad_count = layout_text(f->overlay_text, quads, VKR_MAX_OVERLAY_QUADS);
    }
    uint64_t counts_addr = 0;
    uint32_t *counts_host = vkmin_ring_alloc(gpu, VKMIN_MAX_VIEWS * 2 * sizeof(uint32_t), &counts_addr);
    r->counts_host[slot] = counts_host;
    r->counts_views[slot] = vs.count;
    r->counts_valid[slot] = s.gpu_cull;

    /* --- 1. cull ---------------------------------------------------------- */
    vkmin_timestamp(gpu, 0);
    vkmin_barrier(gpu, &(vkmin_barrier_desc){.frame_start = true});
    vkmin_fill_buffer(gpu, r->draw_counts, 0, VKMIN_MAX_VIEWS * 2 * sizeof(uint32_t), s.compact ? 0u : f->instance_count);
    vkmin_barrier(gpu, &(vkmin_barrier_desc){.transfer_to_compute = true});
    if (s.gpu_cull && f->instance_count) {
        vkmin_bind_pipeline(gpu, r->cull);
        Push push = base_push;
        push.param = s.compact ? 1u : 0u;
        vkmin_push(gpu, &push);
        vkmin_dispatch(gpu, (f->instance_count + VKMIN_CULL_GROUP - 1) / VKMIN_CULL_GROUP, vs.count, 1);
    }
    vkmin_timestamp(gpu, 1);
    vkmin_barrier(gpu, &(vkmin_barrier_desc){.compute_to_indirect_draw = true, .compute_to_transfer = true});
    vkmin_copy_to_ring(gpu, r->draw_counts, 0, VKMIN_MAX_VIEWS * 2 * sizeof(uint32_t), counts_addr);

    /* --- 2. shadow atlas -------------------------------------------------- */
    vkmin_pass_begin(gpu, &(vkmin_pass_desc){.depth = r->atlas, .clear_depth = true, .label = "shadows"});
    vkmin_bind_index_buffer(gpu, r->indices, 0);
    for (uint32_t v = 1; v < vs.count; ++v) {
        const View *sv = &vs.views[v];
        const int atlas = r->desc.shadow_atlas;
        vkmin_set_viewport(gpu, (int)(sv->atlas_rect.x * atlas), (int)(sv->atlas_rect.y * atlas),
                           (int)(sv->atlas_rect.z * atlas), (int)(sv->atlas_rect.w * atlas));
        vkmin_set_depth_bias(gpu, 1.0f, s.shadow_bias);
        draw_lists(r, v, r->depth_cull, r->depth_nocull, &base_push, s.gpu_cull, host_cmds, host_counts);
    }
    vkmin_pass_end(gpu);
    vkmin_timestamp(gpu, 2);

    /* --- 3. depth prepass ------------------------------------------------- */
    vkmin_pass_begin(gpu, &(vkmin_pass_desc){.depth = r->depth, .clear_depth = true, .w = rw, .h = rh, .label = "prepass"});
    if (s.prepass) {
        vkmin_bind_index_buffer(gpu, r->indices, 0);
        vkmin_set_depth_bias(gpu, 0.0f, 0.0f);
        draw_lists(r, 0, r->depth_cull, r->depth_nocull, &base_push, s.gpu_cull, host_cmds, host_counts);
    }
    vkmin_pass_end(gpu);
    vkmin_timestamp(gpu, 3);

    /* --- 4. light clusters ------------------------------------------------ */
    vkmin_bind_pipeline(gpu, r->cluster);
    vkmin_push(gpu, &base_push);
    vkmin_dispatch(gpu, (VKMIN_CLUSTER_COUNT + VKMIN_CLUSTER_GROUP - 1) / VKMIN_CLUSTER_GROUP, 1, 1);
    vkmin_timestamp(gpu, 4);
    const vkmin_transition to_sampled_atlas[] = {{r->atlas, VKMIN_USE_SAMPLED}};
    vkmin_barrier(gpu, &(vkmin_barrier_desc){.images = to_sampled_atlas, .image_count = 1, .compute_to_fragment = true});

    /* --- 5. forward opaque, 6. forward transparent ------------------------ */
    /* The clear is the sky: there is no skybox, and Sponza's courtyard is open. */
    vkmin_pass_begin(gpu, &(vkmin_pass_desc){.color = r->hdr, .depth = r->depth, .clear_color = true,
                                             .clear = {0.30f * 2.5f, 0.50f * 2.5f, 0.95f * 2.5f, 1.0f}, .w = rw, .h = rh, .label = "forward"});
    vkmin_bind_index_buffer(gpu, r->indices, 0);
    const bool overdraw = s.debug == VKMIN_DEBUG_OVERDRAW;
    draw_lists(r, 0, overdraw ? r->fwd_blend : r->fwd_cull, overdraw ? r->fwd_blend : r->fwd_nocull, &base_push,
               s.gpu_cull, host_cmds, host_counts);
    vkmin_timestamp(gpu, 5);
    if (transparent_count) {
        vkmin_bind_pipeline(gpu, r->fwd_blend);
        vkmin_push(gpu, &base_push);
        vkmin_draw_indexed_indirect_host(gpu, transparent_addr, transparent_count);
    }
    vkmin_pass_end(gpu);
    vkmin_timestamp(gpu, 6);

    /* --- 7. tonemap, 8. overlay ------------------------------------------- */
    const vkmin_transition to_sampled_hdr[] = {{r->hdr, VKMIN_USE_SAMPLED}};
    vkmin_barrier(gpu, &(vkmin_barrier_desc){.images = to_sampled_hdr, .image_count = 1});
    vkmin_pass_begin(gpu, &(vkmin_pass_desc){.color = vkmin_backbuffer(gpu), .clear_color = true, .label = "tonemap"});
    vkmin_bind_pipeline(gpu, r->tonemap);
    Push tm = base_push;
    tm.param = r->hdr_tex;
    /* Debug views are diagnostic colours, not radiance: pass them through. */
    tm.param2 = s.debug != 0 ? 0u : (uint32_t)s.tonemap;
    tm.param3 = r->atlas_raw_tex;
    vkmin_push(gpu, &tm);
    vkmin_draw(gpu, 3, 1);
    vkmin_timestamp(gpu, 7);
    if (quad_count) {
        vkmin_bind_pipeline(gpu, r->overlay);
        Push ov = base_push;
        ov.aux = quads_addr;
        ov.param = r->font_tex;
        vkmin_push(gpu, &ov);
        vkmin_draw(gpu, quad_count * 6, 1);
    }
    vkmin_pass_end(gpu);
    vkmin_timestamp(gpu, 8);
    vkmin_frame_end(gpu);

    r->stats.shadow_views = vs.shadow_draw_views;
    r->stats.lights_shadowed = vs.lights_shadowed;
}
