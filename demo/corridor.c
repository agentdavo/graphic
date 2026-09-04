/* corridor.c -- "The Corridor": a deterministic flythrough of Sponza.
 *
 * One sun with cascades, sixty-four point lights on fixed orbits, a few
 * hundred instanced props, one skinned character on a loop, Sponza's own
 * alpha-masked foliage, and two transparent panes. The camera rides a closed
 * spline driven by the frame index and nothing else: `--frame 240` renders
 * exactly frame 240 whether or not 239 came before it.
 *
 *   corridor                              windowed; F1 cycles debug views, Space pauses, F12 saves
 *   corridor --headless --frame 240 --out shot.png
 *   corridor --headless --frames 0,120,240,360 --out-dir shots/
 *   corridor --profile lavapipe ...       320x180, few lights, small atlas: CI without a GPU
 *   corridor r_debug=2 r_shadows=0        any cvar, as name=value
 */
#include "anim.h"
#include "cvar.h"
#include "ktx2.h"
#include "vkmin_math.h"
#include "pack.h"
#include "render.h"
#include "scene.h"
#include "vkmin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979f
#define LOOP_FRAMES 720          /* twelve seconds at the fixed 60 Hz timestep */
#define FRAME_DT (1.0f / 60.0f)

enum { MAX_INSTANCES = 1024, PROP_COUNT = 300, POINT_LIGHTS = 64, MAX_BONES = 128 };

typedef struct {
    bool headless, probe;
    int device;
    const char *scene_path, *character_path, *profile;
} options;

/* ------------------------------------------------------- generated meshes -- */

typedef struct {
    Vertex *v; uint32_t vn, vcap;
    uint32_t *i; uint32_t in, icap;
} mesh_builder;

static void mb_reserve(mesh_builder *b, uint32_t verts, uint32_t indices) {
    b->vcap = verts; b->icap = indices;
    b->v = calloc(verts, sizeof(Vertex));
    b->i = calloc(indices, sizeof(uint32_t));
    if (!b->v || !b->i) { fprintf(stderr, "corridor: out of memory\n"); exit(1); }
}

static void mb_vertex(mesh_builder *b, vec3 p, vec3 n, vec3 t, float u, float v) {
    if (b->vn >= b->vcap) { fprintf(stderr, "corridor: mesh builder overflow\n"); exit(1); }
    b->v[b->vn++] = (Vertex){.px = p.x, .py = p.y, .pz = p.z, .normal = oct_encode(n.x, n.y, n.z),
                             .tangent = pack_tangent(t.x, t.y, t.z, 1.0f), .uv = pack_half2(u, v)};
}

static void mb_tri(mesh_builder *b, uint32_t a, uint32_t c, uint32_t d) {
    if (b->in + 3 > b->icap) { fprintf(stderr, "corridor: mesh builder overflow\n"); exit(1); }
    b->i[b->in++] = a; b->i[b->in++] = c; b->i[b->in++] = d;
}

static Mesh mesh_record(const mesh_builder *b, uint32_t first_vertex, uint32_t first_index, float radius) {
    return (Mesh){.first_index = first_index, .index_count = b->in, .vertex_offset = first_vertex,
                  .skin_offset = VKMIN_NONE, .bounds = {0, 0, 0, radius}};
}

static void build_sphere(mesh_builder *b, int rings, int segments) {
    mb_reserve(b, (uint32_t)((rings + 1) * (segments + 1)), (uint32_t)(rings * segments * 6));
    for (int r = 0; r <= rings; ++r) {
        const float phi = PI * (float)r / (float)rings;
        for (int s = 0; s <= segments; ++s) {
            const float theta = 2.0f * PI * (float)s / (float)segments;
            const vec3 n = {sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta)};
            const vec3 t = {-sinf(theta), 0.0f, cosf(theta)};
            mb_vertex(b, n, n, t, (float)s / (float)segments, (float)r / (float)rings);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = (uint32_t)(r * (segments + 1) + s), c = a + (uint32_t)segments + 1;
            mb_tri(b, a, a + 1, c);      /* outward-facing, counter-clockwise from outside */
            mb_tri(b, a + 1, c + 1, c);
        }
    }
}

static void build_cube(mesh_builder *b) {
    mb_reserve(b, 24, 36);
    static const vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        const vec3 n = normals[f];
        const vec3 up = fabsf(n.y) > 0.5f ? (vec3){0, 0, 1} : (vec3){0, 1, 0};
        const vec3 t = vkmin_vec3_cross(up, n); /* right-handed frame: t x up = n */
        const uint32_t base = b->vn;
        for (int k = 0; k < 4; ++k) {
            const float su = (k & 1) ? 1.0f : -1.0f, sv = (k & 2) ? 1.0f : -1.0f;
            const vec3 p = vkmin_vec3_add(n, vkmin_vec3_add(vkmin_vec3_scale(t, su), vkmin_vec3_scale(up, sv)));
            mb_vertex(b, p, n, t, su * 0.5f + 0.5f, sv * 0.5f + 0.5f);
        }
        mb_tri(b, base, base + 1, base + 3);
        mb_tri(b, base, base + 3, base + 2);
    }
}

static void build_quad(mesh_builder *b) {
    mb_reserve(b, 4, 6);
    for (int k = 0; k < 4; ++k) {
        const float x = (k & 1) ? 1.0f : -1.0f, y = (k & 2) ? 1.0f : -1.0f;
        mb_vertex(b, (vec3){x, y, 0}, (vec3){0, 0, 1}, (vec3){1, 0, 0}, x * 0.5f + 0.5f, 0.5f - y * 0.5f);
    }
    mb_tri(b, 0, 1, 3);
    mb_tri(b, 0, 3, 2);
}

/* ------------------------------------------------------------ the world --- */

typedef struct {
    scene sponza, character;
    uint32_t sponza_mesh0, sponza_mat0, char_mesh0, char_mat0;
    uint32_t sphere_mesh, cube_mesh, quad_mesh, prop_mat0, glass_mat;
    Instance instances[MAX_INSTANCES];
    uint32_t instance_count;
    uint32_t prop_first, char_index, pane_first;
    Light lights[POINT_LIGHTS + 1];
    mat4 bones[MAX_BONES];
    uint32_t bone_count;
    mat4 char_node_world;
} world;

/* Textures a cooked scene references, loaded once and rewritten in place from
 * file indices to bindless slots. */
static uint32_t load_scene(vkr *r, vkmin_ctx *gpu, scene *s, uint32_t *first_material) {
    uint32_t *slots = malloc((s->header.texture_count + 1) * sizeof(uint32_t));
    if (!slots) exit(1);
    for (uint32_t i = 0; i < s->header.texture_count; ++i) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", s->dir, s->textures[i].file);
        slots[i] = vkmin_register_texture(gpu, ktx2_load(gpu, path), s->textures[i].sampler);
    }
    Material *mats = malloc(s->header.material_count * sizeof(Material));
    if (!mats) exit(1);
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
    return vkr_upload_geometry(r, &(vkr_geometry){
                                      .vertices = s->vertices, .vertex_count = s->header.vertex_count,
                                      .indices = s->indices, .index_count = s->header.index_count,
                                      .skin_vertices = s->skin_vertices, .skin_vertex_count = s->header.skin_vertex_count,
                                      .meshes = s->meshes, .mesh_count = s->header.mesh_count});
}

static void add_instance(world *w, Instance inst) {
    if (w->instance_count >= MAX_INSTANCES) { fprintf(stderr, "corridor: too many instances\n"); exit(1); }
    inst.prev_transform = inst.transform;
    w->instances[w->instance_count++] = inst;
}

static vec4 world_bounds(mat4 transform, vec4 local) {
    const vec3 c = vkmin_mat4_mul_point(transform, (vec3){local.x, local.y, local.z});
    const float sx = vkmin_vec3_length((vec3){transform.m[0], transform.m[1], transform.m[2]});
    const float sy = vkmin_vec3_length((vec3){transform.m[4], transform.m[5], transform.m[6]});
    const float sz = vkmin_vec3_length((vec3){transform.m[8], transform.m[9], transform.m[10]});
    return (vec4){c.x, c.y, c.z, local.w * fmaxf(sx, fmaxf(sy, sz))};
}

static mat4 mat4_from_array(const float *a) {
    mat4 m = vkmin_mat4_identity();
    memcpy(m.m, a, sizeof m.m);
    return m;
}

static void world_build(world *w, vkr *r, vkmin_ctx *gpu, const options *opt) {
    w->sponza = scene_load(opt->scene_path);
    w->sponza_mesh0 = load_scene(r, gpu, &w->sponza, &w->sponza_mat0);
    for (uint32_t i = 0; i < w->sponza.header.node_count; ++i) {
        const vkm_node *n = &w->sponza.nodes[i];
        const mat4 t = mat4_from_array(n->transform);
        add_instance(w, (Instance){.transform = t, .bounds = world_bounds(t, w->sponza.meshes[n->mesh].bounds),
                                   .mesh = w->sponza_mesh0 + n->mesh, .material = w->sponza_mat0 + n->material,
                                   .bone_offset = VKMIN_NONE});
    }

    /* Generated props and panes: three materials, no textures. */
    const Material prop_mats[4] = {
        {.base_color = {0.95f, 0.75f, 0.35f, 1}, .albedo_tex = VKMIN_NONE, .normal_tex = VKMIN_NONE, .mr_tex = VKMIN_NONE,
         .emissive_tex = VKMIN_NONE, .metallic = 1.0f, .roughness = 0.25f, .alpha_cutoff = 0.5f, .normal_scale = 1},
        {.base_color = {0.75f, 0.12f, 0.10f, 1}, .albedo_tex = VKMIN_NONE, .normal_tex = VKMIN_NONE, .mr_tex = VKMIN_NONE,
         .emissive_tex = VKMIN_NONE, .metallic = 0.0f, .roughness = 0.7f, .alpha_cutoff = 0.5f, .normal_scale = 1},
        {.base_color = {0.15f, 0.35f, 0.9f, 1}, .albedo_tex = VKMIN_NONE, .normal_tex = VKMIN_NONE, .mr_tex = VKMIN_NONE,
         .emissive_tex = VKMIN_NONE, .metallic = 0.0f, .roughness = 0.2f, .alpha_cutoff = 0.5f, .normal_scale = 1},
        {.base_color = {0.45f, 0.75f, 1.0f, 0.35f}, .albedo_tex = VKMIN_NONE, .normal_tex = VKMIN_NONE, .mr_tex = VKMIN_NONE,
         .emissive_tex = VKMIN_NONE, .metallic = 0.0f, .roughness = 0.05f, .alpha_cutoff = 0.5f, .normal_scale = 1,
         .flags = VKMIN_MAT_BLEND | VKMIN_MAT_DOUBLE_SIDED},
    };
    w->prop_mat0 = vkr_upload_materials(r, prop_mats, 4);
    w->glass_mat = w->prop_mat0 + 3;

    mesh_builder sphere = {0}, cube = {0}, quad = {0};
    build_sphere(&sphere, 12, 24);
    build_cube(&cube);
    build_quad(&quad);
    const uint32_t vn = sphere.vn + cube.vn + quad.vn, in = sphere.in + cube.in + quad.in;
    Vertex *verts = malloc(vn * sizeof(Vertex));
    uint32_t *indices = malloc(in * sizeof(uint32_t));
    if (!verts || !indices) exit(1);
    memcpy(verts, sphere.v, sphere.vn * sizeof(Vertex));
    memcpy(verts + sphere.vn, cube.v, cube.vn * sizeof(Vertex));
    memcpy(verts + sphere.vn + cube.vn, quad.v, quad.vn * sizeof(Vertex));
    memcpy(indices, sphere.i, sphere.in * sizeof(uint32_t));
    memcpy(indices + sphere.in, cube.i, cube.in * sizeof(uint32_t));
    memcpy(indices + sphere.in + cube.in, quad.i, quad.in * sizeof(uint32_t));
    const Mesh meshes[3] = {mesh_record(&sphere, 0, 0, 1.0f), mesh_record(&cube, sphere.vn, sphere.in, 1.75f),
                            mesh_record(&quad, sphere.vn + cube.vn, sphere.in + cube.in, 1.42f)};
    const uint32_t gen0 = vkr_upload_geometry(r, &(vkr_geometry){.vertices = verts, .vertex_count = vn, .indices = indices,
                                                                 .index_count = in, .meshes = meshes, .mesh_count = 3});
    w->sphere_mesh = gen0;
    w->cube_mesh = gen0 + 1;
    w->quad_mesh = gen0 + 2;
    free(verts); free(indices);
    free(sphere.v); free(sphere.i); free(cube.v); free(cube.i); free(quad.v); free(quad.i);

    w->prop_first = w->instance_count;
    for (int i = 0; i < PROP_COUNT; ++i) add_instance(w, (Instance){.mesh = w->sphere_mesh, .material = w->prop_mat0, .bone_offset = VKMIN_NONE});

    w->pane_first = w->instance_count;
    const vec3 pane_pos[2] = {{-2.0f, 1.6f, 4.9f}, {3.0f, 1.6f, -4.9f}};
    for (int i = 0; i < 2; ++i) {
        const mat4 t = vkmin_mat4_mul(vkmin_mat4_translate(pane_pos[i]), vkmin_mat4_scale((vec3){1.4f, 1.2f, 1.0f}));
        add_instance(w, (Instance){.transform = t, .bounds = world_bounds(t, meshes[2].bounds), .mesh = w->quad_mesh,
                                   .material = w->glass_mat, .bone_offset = VKMIN_NONE});
    }

    /* The character: CesiumMan, scaled up a little, standing in the courtyard. */
    w->character = scene_load(opt->character_path);
    w->char_mesh0 = load_scene(r, gpu, &w->character, &w->char_mat0);
    w->char_index = w->instance_count;
    const vkm_node *cn = &w->character.nodes[0];
    w->char_node_world = mat4_from_array(cn->transform);
    const mat4 placement = vkmin_mat4_mul(vkmin_mat4_translate((vec3){0.5f, 0.0f, 1.5f}),
                                    vkmin_mat4_mul(vkmin_mat4_rotate_y(-0.6f), vkmin_mat4_scale((vec3){1.6f, 1.6f, 1.6f})));
    const mat4 t = vkmin_mat4_mul(placement, w->char_node_world);
    add_instance(w, (Instance){.transform = t, .bounds = world_bounds(t, w->character.meshes[cn->mesh].bounds),
                               .mesh = w->char_mesh0 + cn->mesh, .material = w->char_mat0 + cn->material,
                               .bone_offset = 0, .flags = cn->skinned ? VKMIN_INST_SKINNED : 0u});
}

/* Everything that moves, as a function of the frame index. */
static void world_animate(world *w, int frame) {
    const float time = (float)frame * FRAME_DT;
    for (int i = 0; i < PROP_COUNT; ++i) {
        Instance *inst = &w->instances[w->prop_first + i];
        inst->prev_transform = inst->transform;
        /* three rings around the courtyard at different heights, slowly turning */
        const int ring = i % 3, k = i / 3;
        const float per_ring = (float)(PROP_COUNT / 3);
        const float angle = 2.0f * PI * (float)k / per_ring + time * (0.05f + 0.02f * (float)ring) * (ring == 1 ? -1.0f : 1.0f);
        const float rx = 6.5f - 1.0f * (float)ring, rz = 1.6f - 0.3f * (float)ring;
        const float y = 1.8f + 2.2f * (float)ring + 0.4f * sinf(angle * 3.0f + time);
        const float scale = 0.10f + 0.04f * (float)((k * 7) % 5);
        const mat4 rot = vkmin_mat4_mul(vkmin_mat4_rotate_y(time * 0.7f + (float)i), vkmin_mat4_rotate_x(time * 0.4f));
        inst->transform = vkmin_mat4_mul(vkmin_mat4_translate((vec3){rx * cosf(angle), y, rz * sinf(angle)}),
                                   vkmin_mat4_mul(rot, vkmin_mat4_scale((vec3){scale, scale, scale})));
        inst->mesh = (k % 4 == 0) ? w->cube_mesh : w->sphere_mesh;
        inst->material = w->prop_mat0 + (uint32_t)(k % 3);
        inst->bounds = world_bounds(inst->transform, (vec4){0, 0, 0, inst->mesh == w->cube_mesh ? 1.75f : 1.0f});
    }

    /* Sixty-four point lights on two counter-rotating ellipses. */
    const float sun_intensity = cvar_get(CV_r_sun_intensity);
    const vec3 sun_dir = vkmin_vec3_normalize((vec3){0.35f, -1.0f, 0.25f});
    w->lights[0] = (Light){.pos_radius = {0, 0, 0, 1e9f}, .color = {sun_intensity, sun_intensity * 0.96f, sun_intensity * 0.88f, 0},
                           .dir_cone = {sun_dir.x, sun_dir.y, sun_dir.z, 0}, .type = VKMIN_LIGHT_DIRECTIONAL,
                           .shadow_view = VKMIN_NONE};
    for (int i = 0; i < POINT_LIGHTS; ++i) {
        const int ring = i & 1;
        const float phase = 2.0f * PI * (float)(i / 2) / (float)(POINT_LIGHTS / 2);
        const float angle = phase + time * 0.25f * (ring ? -1.0f : 1.0f);
        const vec3 p = {11.0f * cosf(angle), 1.6f + 3.6f * (float)ring + 0.5f * sinf(time + phase * 3.0f), 5.6f * sinf(angle)};
        const float hue = (float)i / (float)POINT_LIGHTS;
        const vec3 c = {0.5f + 0.5f * cosf(2 * PI * hue), 0.5f + 0.5f * cosf(2 * PI * (hue + 0.333f)), 0.5f + 0.5f * cosf(2 * PI * (hue + 0.667f))};
        const float intensity = 5.0f;
        w->lights[1 + i] = (Light){.pos_radius = {p.x, p.y, p.z, 3.5f}, .color = {c.x * intensity, c.y * intensity, c.z * intensity, 0},
                                   .dir_cone = {0, -1, 0, 0}, .type = VKMIN_LIGHT_POINT, .shadow_view = VKMIN_NONE};
    }

    Instance *ch = &w->instances[w->char_index];
    ch->prev_transform = ch->transform;
    w->bone_count = anim_bones(&w->character, time, w->char_node_world, w->bones, MAX_BONES);
}

/* -------------------------------------------------------------- camera ---- */

static vec3 catmull_rom(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    vec3 out;
    out.x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
    out.y = 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
    out.z = 0.5f * ((2 * p1.z) + (-p0.z + p2.z) * t + (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * t2 + (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * t3);
    return out;
}

/* Closed spline through control points, evaluated at u in [0, 1). */
static vec3 spline_eval(const vec3 *pts, int n, float u) {
    const float s = (u - floorf(u)) * (float)n;
    const int seg = (int)s;
    const float t = s - (float)seg;
    return catmull_rom(pts[(seg + n - 1) % n], pts[seg % n], pts[(seg + 1) % n], pts[(seg + 2) % n], t);
}

/* Through the south arcade (lights occluded by columns), up to the north
 * balcony (cascade boundary in view), across the open atrium (most of the
 * level culled behind the camera), and back. */
static const vec3 camera_points[8] = {
    {-11.5f, 1.7f, -0.5f}, {-5.0f, 1.7f, 4.4f}, {4.5f, 1.9f, 4.8f}, {11.0f, 2.4f, 0.5f},
    {7.0f, 5.8f, -5.6f}, {-2.0f, 6.2f, -5.9f}, {-9.0f, 6.4f, -2.5f}, {-12.0f, 3.2f, 1.0f},
};
static const vec3 target_points[8] = {
    {0.0f, 2.5f, 0.0f}, {4.0f, 2.0f, 4.5f}, {12.0f, 2.5f, 3.0f}, {2.0f, 4.0f, -4.0f},
    {-4.0f, 5.0f, -3.0f}, {-2.0f, 1.5f, 3.0f}, {2.0f, 2.0f, 1.0f}, {0.0f, 2.5f, 0.0f},
};

static void camera_at(int frame, int width, int height, mat4 *view, mat4 *proj, vec4 *pos, float near, float far) {
    const float u = (float)(frame % LOOP_FRAMES) / (float)LOOP_FRAMES;
    const vec3 eye = spline_eval(camera_points, 8, u);
    const vec3 target = spline_eval(target_points, 8, u);
    *view = vkmin_mat4_look_at(eye, target, (vec3){0, 1, 0});
    *proj = vkmin_mat4_perspective(60.0f * PI / 180.0f, (float)width / (float)height, near, far);
    *pos = (vec4){eye.x, eye.y, eye.z, 1.0f};
}

/* -------------------------------------------------------------- overlay --- */

static void compose_overlay(char *buf, size_t cap, const vkr_stats *st, int frame, uint32_t lights, bool timings) {
    int n = snprintf(buf, cap, "vkmin v0.1  frame %d  %u draws (cam)  %u shadow draws / %u views  %u lights (%u shadowed)\n",
                     frame, st->draws_camera, st->draws_shadow, st->shadow_views, lights, st->lights_shadowed);
    if (timings) {
        n += snprintf(buf + n, cap - (size_t)n, "gpu %.2f ms  ", st->frame_ms);
        for (int i = 0; i < 8 && n < (int)cap; ++i) {
            n += snprintf(buf + n, cap - (size_t)n, "%s %.2f  ", vkr_pass_names[i], st->pass_ms[i]);
        }
        n += snprintf(buf + n, cap - (size_t)n, "\n");
    }
    n += snprintf(buf + n, cap - (size_t)n, "device %zu/%zu MB  ring %zu/%zu KB\n", st->device_used >> 20, st->device_cap >> 20,
                  st->ring_used >> 10, st->ring_cap >> 10);
    char overrides[512];
    if (cvar_format_overrides(overrides, sizeof overrides) > 0 && n < (int)cap) {
        snprintf(buf + n, cap - (size_t)n, "cvars: %s\n", overrides);
    }
}

/* ------------------------------------------------------------- the demo --- */

static const char *usage_text =
    "usage: corridor [options] [cvar=value ...]\n"
    "  --scene PATH            cooked scene (default assets/sponza/scene.vkm)\n"
    "  --character PATH        cooked skinned character (default assets/cesium/scene.vkm)\n"
    "  --profile lavapipe      320x180, 8 lights, 1024 atlas, 1 cascade, no transparents, no overlay\n"
    "  --probe                 print what the device offers and which path would be chosen\n"
    "  plus every vkmin flag: --headless --frame N --frames a,b --out P --out-dir D --exit-after N\n"
    "  --size W H --path=legacy|modern --sync-naive --no-readback --device N --verbose --cvars\n"
    "  windowed: F1 cycles debug views, Space pauses, F12 saves a PNG, Escape quits\n";

int main(int argc, char **argv) {
    options opt = {.scene_path = "assets/sponza/scene.vkm", .character_path = "assets/cesium/scene.vkm"};
    /* The demo reads only its own flags; vkmin_init reads the rest. The one
     * thing both need to know is whether the run is headless. */
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const bool next = i + 1 < argc;
        if (!strcmp(a, "--headless") || !strcmp(a, "--frame") || !strcmp(a, "--frames")) opt.headless = true;
        else if (!strcmp(a, "--probe")) opt.probe = true;
        else if (!strcmp(a, "--scene") && next) opt.scene_path = argv[++i];
        else if (!strcmp(a, "--character") && next) opt.character_path = argv[++i];
        else if (!strcmp(a, "--profile") && next) opt.profile = argv[++i];
        else if (!strcmp(a, "--device") && next) opt.device = atoi(argv[++i]);
        else if (!strcmp(a, "--help")) { fprintf(stderr, "%s", usage_text); return 0; }
        else if (a[0] != '-' && strchr(a, '=')) { if (!cvar_parse_assignment(a)) return 2; }
    }
    if (opt.profile) {
        if (strcmp(opt.profile, "lavapipe") != 0) { fprintf(stderr, "corridor: unknown profile '%s'\n", opt.profile); return 2; }
        /* Explicit cvar assignments on the command line still win, even
         * ones that assign the default: the profile fills in only what was
         * not named. */
        struct { cvar_id id; float v; } profile[] = {{CV_r_width, 320}, {CV_r_height, 180}, {CV_r_max_lights, 8},
                                                       {CV_r_shadow_atlas, 1024}, {CV_r_cascades, 1}, {CV_r_transparent, 0},
                                                       {CV_r_shadow_lights, 1}, {CV_r_overlay, 0}};
        for (size_t k = 0; k < sizeof profile / sizeof profile[0]; ++k) {
            if (!cvar_was_set(profile[k].id)) cvar_set(profile[k].id, profile[k].v);
        }
    }
    if (opt.probe) {
        const vkmin_report r = vkmin_probe(opt.device);
        printf("device: %s (Vulkan %u.%u)\nhostImageCopy=%d maintenance5=%d pushDescriptor=%d pipelineRobustness=%d "
               "robustBufferAccess2=%d\nscalarBlockLayout=%d bufferDeviceAddress=%d descriptorIndexing=%d "
               "drawIndirectCount=%d\npath: %s (%s)\n", r.device_name, r.api_major, r.api_minor, r.host_image_copy,
               r.maintenance5, r.push_descriptor, r.pipeline_robustness, r.robust_buffer_access2, r.scalar_block_layout,
               r.buffer_device_address, r.descriptor_indexing, r.draw_indirect_count,
               r.would_choose == VKMIN_PATH_MODERN ? "modern" : "legacy", r.reason ? r.reason : "");
        return r.vulkan_1_3 ? 0 : 1;
    }
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "corridor", .device_index = opt.device});
    int width = 0, height = 0;
    vkmin_size(gpu, &width, &height);
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = width, .height = height, .shadow_atlas = cvar_get_int(CV_r_shadow_atlas),
                                       .max_vertices = 400000, .max_indices = 1200000, .max_skin_vertices = 8192,
                                       .max_meshes = 256, .max_materials = 64, .max_instances = MAX_INSTANCES});
    world *w = calloc(1, sizeof *w);
    if (!w) return 1;
    world_build(w, r, gpu, &opt);
    printf("corridor: %u instances, %d lights, %u bones\n", w->instance_count, POINT_LIGHTS + 1, w->character.header.joint_count);

    char overlay[2048];
    const float near = 0.1f, far = 120.0f;
    int shot = 0;
    while (vkmin_frame_begin(gpu, NULL)) {
        if (vkmin_key_hit(gpu, VKMIN_KEY_ESCAPE)) { vkmin_frame_end(gpu); break; }
        if (vkmin_key_hit(gpu, VKMIN_KEY_SPACE)) cvar_set(CV_d_frame_step, cvar_get(CV_d_frame_step) > 0 ? 0.0f : 1.0f);
        if (vkmin_key_hit(gpu, VKMIN_KEY_F1)) cvar_set(CV_r_debug, (float)((cvar_get_int(CV_r_debug) + 1) % 7));
        const int frame = (int)vkmin_frame_index(gpu);
        int ww = 0, wh = 0;
        vkmin_size(gpu, &ww, &wh);
        world_animate(w, frame);
        vkr_frame_desc fd = {.instances = w->instances, .instance_count = w->instance_count, .lights = w->lights,
                             .light_count = POINT_LIGHTS + 1, .bones = w->bones, .bone_count = w->bone_count,
                             .near = near, .far = far, .frame_index = (uint32_t)frame};
        camera_at(frame, ww, wh, &fd.view, &fd.proj, &fd.camera_pos, near, far);
        /* Headless overlays omit timings so the image is reproducible. */
        const vkr_stats st = vkr_get_stats(r);
        compose_overlay(overlay, sizeof overlay, &st, frame, POINT_LIGHTS + 1, !opt.headless);
        fd.overlay_text = overlay;
        vkr_frame(r, &fd);
        vkmin_frame_end(gpu);
        if (vkmin_key_hit(gpu, VKMIN_KEY_F12)) {
            char path[64];
            snprintf(path, sizeof path, "shot_%04d.png", shot++);
            if (vkmin_save_png(gpu, path)) printf("wrote %s\n", path);
        }
    }
    const int status = 0;

    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    scene_free(&w->sponza);
    scene_free(&w->character);
    free(w);
    return status;
}
