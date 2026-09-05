/* gamekit.h -- what every example game needs and no game should write twice:
 * generated meshes, cooked-scene upload, a few procedural textures, the
 * lavapipe profile and the option parsing. Not part of the engine: a game
 * that wants none of it includes vkmin.h and render.h and goes its own way.
 * Everything here is static inline and runs before the first frame. */
#ifndef VKMIN_GAMEKIT_H
#define VKMIN_GAMEKIT_H

#include "anim.h"
#include "cvar.h"
#include "ktx2.h"
#include "pack.h"
#include "render.h"
#include "scene.h"
#include "vkmin.h"
#include "vkmin_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GK_PI 3.14159265358979f

static inline void gk_die(const char *why) {
    fprintf(stderr, "game: %s\n", why);
    exit(1);
}

/* ------------------------------------------------------- generated meshes -- */

typedef struct {
    Vertex *v; uint32_t vn, vcap;
    uint32_t *i; uint32_t in, icap;
} gk_mesh_builder;

static inline void gk_mb_reserve(gk_mesh_builder *b, uint32_t verts, uint32_t indices) {
    b->vcap = verts; b->icap = indices; b->vn = 0; b->in = 0;
    b->v = calloc(verts, sizeof(Vertex));
    b->i = calloc(indices, sizeof(uint32_t));
    if (!b->v || !b->i) gk_die("out of memory");
}

static inline void gk_mb_vertex(gk_mesh_builder *b, vec3 p, vec3 n, vec3 t, float u, float v) {
    if (b->vn >= b->vcap) gk_die("mesh builder overflow");
    b->v[b->vn++] = (Vertex){.px = p.x, .py = p.y, .pz = p.z, .normal = oct_encode(n.x, n.y, n.z),
                             .tangent = pack_tangent(t.x, t.y, t.z, 1.0f), .uv = pack_half2(u, v)};
}

static inline void gk_mb_tri(gk_mesh_builder *b, uint32_t a, uint32_t c, uint32_t d) {
    if (b->in + 3 > b->icap) gk_die("mesh builder overflow");
    b->i[b->in++] = a; b->i[b->in++] = c; b->i[b->in++] = d;
}

static inline void gk_build_sphere(gk_mesh_builder *b, int rings, int segments) {
    gk_mb_reserve(b, (uint32_t)((rings + 1) * (segments + 1)), (uint32_t)(rings * segments * 6));
    for (int r = 0; r <= rings; ++r) {
        const float phi = GK_PI * (float)r / (float)rings;
        for (int s = 0; s <= segments; ++s) {
            const float theta = 2.0f * GK_PI * (float)s / (float)segments;
            const vec3 n = {sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta)};
            const vec3 t = {-sinf(theta), 0.0f, cosf(theta)};
            gk_mb_vertex(b, n, n, t, (float)s / (float)segments, (float)r / (float)rings);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = (uint32_t)(r * (segments + 1) + s), c = a + (uint32_t)segments + 1;
            gk_mb_tri(b, a, a + 1, c); /* outward-facing, counter-clockwise from outside */
            gk_mb_tri(b, a + 1, c + 1, c);
        }
    }
}

/* A unit cube from -1 to 1. */
static inline void gk_build_cube(gk_mesh_builder *b) {
    gk_mb_reserve(b, 24, 36);
    static const vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        const vec3 n = normals[f];
        const vec3 up = fabsf(n.y) > 0.5f ? (vec3){0, 0, 1} : (vec3){0, 1, 0};
        const vec3 t = vkmin_vec3_cross(up, n); /* right-handed frame: t x up = n */
        const uint32_t base = b->vn;
        for (int k = 0; k < 4; ++k) {
            const float su = (k & 1) ? 1.0f : -1.0f, sv = (k & 2) ? 1.0f : -1.0f;
            const vec3 p = vkmin_vec3_add(n, vkmin_vec3_add(vkmin_vec3_scale(t, su), vkmin_vec3_scale(up, sv)));
            gk_mb_vertex(b, p, n, t, su * 0.5f + 0.5f, sv * 0.5f + 0.5f);
        }
        gk_mb_tri(b, base, base + 1, base + 3);
        gk_mb_tri(b, base, base + 3, base + 2);
    }
}

/* A quad from -1 to 1 in XY, facing +Z. */
static inline void gk_build_quad(gk_mesh_builder *b) {
    gk_mb_reserve(b, 4, 6);
    for (int k = 0; k < 4; ++k) {
        const float x = (k & 1) ? 1.0f : -1.0f, y = (k & 2) ? 1.0f : -1.0f;
        gk_mb_vertex(b, (vec3){x, y, 0}, (vec3){0, 0, 1}, (vec3){1, 0, 0}, x * 0.5f + 0.5f, 0.5f - y * 0.5f);
    }
    gk_mb_tri(b, 0, 1, 3);
    gk_mb_tri(b, 0, 3, 2);
}

/* Uploads one built mesh and returns its mesh index. */
static inline uint32_t gk_upload_mesh(vkr *r, gk_mesh_builder *b, float radius) {
    const Mesh m = {.first_index = 0, .index_count = b->in, .vertex_offset = 0, .skin_offset = VKMIN_NONE, .bounds = {0, 0, 0, radius}};
    const uint32_t first = vkr_upload_geometry(r, &(vkr_geometry){.vertices = b->v, .vertex_count = b->vn, .indices = b->i,
                                                                  .index_count = b->in, .meshes = &m, .mesh_count = 1});
    free(b->v);
    free(b->i);
    *b = (gk_mesh_builder){0};
    return first;
}

typedef struct { uint32_t sphere, cube, quad; } gk_shapes;

static inline gk_shapes gk_upload_shapes(vkr *r) {
    gk_mesh_builder b = {0};
    gk_shapes s;
    gk_build_sphere(&b, 12, 24);
    s.sphere = gk_upload_mesh(r, &b, 1.0f);
    gk_build_cube(&b);
    s.cube = gk_upload_mesh(r, &b, 1.7321f);
    gk_build_quad(&b);
    s.quad = gk_upload_mesh(r, &b, 1.4143f);
    return s;
}

/* ---------------------------------------------------------------- scenes --- */

/* World-space bounding sphere of a local one under a transform. */
static inline vec4 gk_world_bounds(mat4 transform, vec4 local) {
    const vec3 c = vkmin_mat4_mul_point(transform, (vec3){local.x, local.y, local.z});
    const float sx = vkmin_vec3_length((vec3){transform.m[0], transform.m[1], transform.m[2]});
    const float sy = vkmin_vec3_length((vec3){transform.m[4], transform.m[5], transform.m[6]});
    const float sz = vkmin_vec3_length((vec3){transform.m[8], transform.m[9], transform.m[10]});
    return (vec4){c.x, c.y, c.z, local.w * fmaxf(sx, fmaxf(sy, sz))};
}

static inline mat4 gk_mat4_from_array(const float *a) {
    mat4 m = vkmin_mat4_identity();
    memcpy(m.m, a, sizeof m.m);
    return m;
}

/* Uploads a cooked scene's textures, materials and geometry; returns the first
 * mesh index and writes the first material index. Node transforms are the
 * caller's to instance (see gk_scene_instance). */
static inline uint32_t gk_load_scene(vkr *r, vkmin_ctx *gpu, const scene *s, uint32_t *first_material) {
    uint32_t *slots = malloc((s->header.texture_count + 1) * sizeof(uint32_t));
    Material *mats = malloc((s->header.material_count + 1) * sizeof(Material));
    if (!slots || !mats) gk_die("out of memory");
    for (uint32_t i = 0; i < s->header.texture_count; ++i) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", s->dir, s->textures[i].file);
        slots[i] = vkmin_register_texture(gpu, ktx2_load(gpu, path), s->textures[i].sampler);
    }
    for (uint32_t i = 0; i < s->header.material_count; ++i) {
        mats[i] = s->materials[i];
        uint32_t *fields[4] = {&mats[i].albedo_tex, &mats[i].normal_tex, &mats[i].mr_tex, &mats[i].emissive_tex};
        for (int k = 0; k < 4; ++k) {
            if (*fields[k] != VKMIN_NONE) *fields[k] = slots[*fields[k]];
        }
    }
    *first_material = vkr_upload_materials(r, mats, s->header.material_count);
    free(mats);
    free(slots);
    return vkr_upload_geometry(r, &(vkr_geometry){.vertices = s->vertices, .vertex_count = s->header.vertex_count,
                                                  .indices = s->indices, .index_count = s->header.index_count,
                                                  .skin_vertices = s->skin_vertices, .skin_vertex_count = s->header.skin_vertex_count,
                                                  .meshes = s->meshes, .mesh_count = s->header.mesh_count});
}

/* A node's own world transform, as cooked. Bone matrices are built against
 * this, not the placed instance transform: anim_bones cancels what it is
 * given, so give it the placement too and the placement cancels as well. */
static inline mat4 gk_node_transform(const scene *s, uint32_t node) { return gk_mat4_from_array(s->nodes[node].transform); }

/* The instance for one node of a loaded scene, placed by `placement`. */
static inline Instance gk_scene_instance(const scene *s, uint32_t node, uint32_t first_mesh, uint32_t first_material, mat4 placement) {
    const vkm_node *n = &s->nodes[node];
    const mat4 t = vkmin_mat4_mul(placement, gk_mat4_from_array(n->transform));
    return (Instance){.transform = t, .prev_transform = t, .bounds = gk_world_bounds(t, s->meshes[n->mesh].bounds),
                      .mesh = first_mesh + n->mesh, .material = first_material + n->material,
                      .bone_offset = n->skinned ? 0u : VKMIN_NONE, .flags = n->skinned ? VKMIN_INST_SKINNED : 0u};
}

/* A material with no maps. */
static inline Material gk_material(float r, float g, float b, float metallic, float roughness, uint32_t flags) {
    return (Material){.base_color = {r, g, b, 1.0f}, .albedo_tex = VKMIN_NONE, .normal_tex = VKMIN_NONE, .mr_tex = VKMIN_NONE,
                      .emissive_tex = VKMIN_NONE, .metallic = metallic, .roughness = roughness, .alpha_cutoff = 0.5f,
                      .normal_scale = 1.0f, .flags = flags};
}

static inline Light gk_sun(vec3 dir, float intensity) {
    const vec3 d = vkmin_vec3_normalize(dir);
    return (Light){.pos_radius = {0, 0, 0, 1e9f}, .color = {intensity, intensity * 0.96f, intensity * 0.88f, 0},
                   .dir_cone = {d.x, d.y, d.z, 0}, .type = VKMIN_LIGHT_DIRECTIONAL, .shadow_view = VKMIN_NONE};
}

static inline Light gk_point_light(vec3 p, float radius, vec3 color, float intensity) {
    return (Light){.pos_radius = {p.x, p.y, p.z, radius}, .color = {color.x * intensity, color.y * intensity, color.z * intensity, 0},
                   .dir_cone = {0, -1, 0, 0}, .type = VKMIN_LIGHT_POINT, .shadow_view = VKMIN_NONE};
}

/* -------------------------------------------------------------- textures --- */

static inline uint32_t gk_rgba(float r, float g, float b, float a) {
    const uint32_t ir = (uint32_t)(fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f + 0.5f), ig = (uint32_t)(fminf(fmaxf(g, 0.0f), 1.0f) * 255.0f + 0.5f);
    const uint32_t ib = (uint32_t)(fminf(fmaxf(b, 0.0f), 1.0f) * 255.0f + 0.5f), ia = (uint32_t)(fminf(fmaxf(a, 0.0f), 1.0f) * 255.0f + 0.5f);
    return ir | (ig << 8) | (ib << 16) | (ia << 24);
}

/* Uploads a square RGBA8 texture and returns its bindless slot. */
static inline uint32_t gk_texture(vkmin_ctx *gpu, int size, const uint32_t *pixels, uint32_t sampler, const char *label) {
    const vkmin_image img = vkmin_make_image(gpu, &(vkmin_image_desc){.width = size, .height = size, .format = VKMIN_FMT_RGBA8_UNORM,
                                                                      .usage = VKMIN_IMAGE_SAMPLED, .sampler = sampler, .label = label});
    vkmin_image_upload(gpu, img, 0, pixels, (size_t)size * size * 4u);
    return vkmin_index(gpu, img);
}

/* A soft disc with a bright core: the one particle texture. */
static inline uint32_t gk_disc_texture(vkmin_ctx *gpu, int size) {
    uint32_t *px = malloc((size_t)size * size * 4u);
    if (!px) gk_die("out of memory");
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = ((float)x + 0.5f) / (float)size - 0.5f, dy = ((float)y + 0.5f) / (float)size - 0.5f;
            const float d = sqrtf(dx * dx + dy * dy) * 2.0f;
            const float a = fmaxf(0.0f, 1.0f - d), core = fmaxf(0.0f, 1.0f - d * 3.0f);
            px[y * size + x] = gk_rgba(1.0f, 1.0f, 1.0f, a * a * (0.6f + 0.4f * core));
        }
    }
    const uint32_t tex = gk_texture(gpu, size, px, VKMIN_SAMPLER_LINEAR_CLAMP, "gk.disc");
    free(px);
    return tex;
}

/* A checkerboard in two colours, `cells` squares across. */
static inline uint32_t gk_checker_texture(vkmin_ctx *gpu, int size, int cells, uint32_t c0, uint32_t c1) {
    uint32_t *px = malloc((size_t)size * size * 4u);
    if (!px) gk_die("out of memory");
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) px[y * size + x] = ((x * cells / size + y * cells / size) & 1) ? c1 : c0;
    }
    const uint32_t tex = gk_texture(gpu, size, px, VKMIN_SAMPLER_LINEAR_REPEAT, "gk.checker");
    free(px);
    return tex;
}

/* --------------------------------------------------------------- options --- */

typedef struct {
    bool headless;
    const char *profile;
    int device;
} gk_options;

/* Reads the game's flags and the cvars; leaves vkmin's flags for vkmin_init.
 * `--profile lavapipe` sets small settings for CI without a GPU, without
 * overriding any cvar the user named. */
static inline gk_options gk_parse(int argc, char **argv, const char *usage) {
    gk_options o = {0};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const bool next = i + 1 < argc;
        if (!strcmp(a, "--headless") || !strcmp(a, "--frame") || !strcmp(a, "--frames") || !strcmp(a, "--replay")) o.headless = true;
        else if (!strcmp(a, "--profile") && next) o.profile = argv[++i];
        else if (!strcmp(a, "--device") && next) o.device = atoi(argv[++i]);
        else if (!strcmp(a, "--help")) { fprintf(stderr, "%s", usage); exit(0); }
        else if (a[0] != '-' && strchr(a, '=')) { if (!cvar_parse_assignment(a)) exit(2); }
    }
    if (o.profile) {
        if (strcmp(o.profile, "lavapipe") != 0) { fprintf(stderr, "unknown profile '%s'\n", o.profile); exit(2); }
        const struct { cvar_id id; float v; } profile[] = {{CV_r_width, 320}, {CV_r_height, 180}, {CV_r_max_lights, 8},
                                                           {CV_r_shadow_atlas, 1024}, {CV_r_cascades, 1}, {CV_r_shadow_lights, 1},
                                                           {CV_r_overlay, 0}};
        for (size_t k = 0; k < sizeof profile / sizeof profile[0]; ++k) {
            if (!cvar_was_set(profile[k].id)) cvar_set(profile[k].id, profile[k].v);
        }
    }
    return o;
}

/* Fixed-step simulation: how many ticks to step this frame. The game keeps
 * `done`; a headless jump from frame 0 to 240 steps them all in one frame. */
static inline uint32_t gk_ticks_due(uint32_t frame, uint32_t hz, uint32_t *done) {
    const uint32_t total = vkmin_ticks_for_frame(frame, hz).ticks;
    const uint32_t due = total > *done ? total - *done : 0u;
    *done = total;
    return due;
}

/* Bounded pseudo-random: a hash of (seed, i), so the world is a function of
 * its indices and the same everywhere. */
static inline float gk_hash(uint32_t seed, uint32_t i) {
    uint32_t h = seed * 747796405u + i * 2891336453u;
    h = ((h >> ((h >> 28) + 4u)) ^ h) * 277803737u;
    h = (h >> 22) ^ h;
    return (float)(h & 0xffffffu) / 16777216.0f;
}

#endif
