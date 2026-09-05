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
#include "gamekit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI GK_PI
#define LOOP_FRAMES 720          /* twelve seconds at the fixed 60 Hz timestep */
#define FRAME_DT (1.0f / 60.0f)

enum { MAX_INSTANCES = 1024, PROP_COUNT = 300, POINT_LIGHTS = 64, MAX_BONES = 128 };

typedef struct {
    bool headless, probe;
    int device;
    const char *scene_path, *character_path, *profile;
} options;

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
} world;

static void add_instance(world *w, Instance inst) {
    if (w->instance_count >= MAX_INSTANCES) { fprintf(stderr, "corridor: too many instances\n"); exit(1); }
    inst.prev_transform = inst.transform;
    w->instances[w->instance_count++] = inst;
}

static void world_build(world *w, vkr *r, vkmin_ctx *gpu, const options *opt) {
    w->sponza = scene_load(opt->scene_path);
    w->sponza_mesh0 = gk_load_scene(r, gpu, &w->sponza, &w->sponza_mat0);
    for (uint32_t i = 0; i < w->sponza.header.node_count; ++i) {
        add_instance(w, gk_scene_instance(&w->sponza, i, w->sponza_mesh0, w->sponza_mat0, vkmin_mat4_identity()));
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

    const gk_shapes shapes = gk_upload_shapes(r);
    w->sphere_mesh = shapes.sphere;
    w->cube_mesh = shapes.cube;
    w->quad_mesh = shapes.quad;
    const vec4 quad_bounds = {0, 0, 0, 1.4143f};

    w->prop_first = w->instance_count;
    for (int i = 0; i < PROP_COUNT; ++i) add_instance(w, (Instance){.mesh = w->sphere_mesh, .material = w->prop_mat0, .bone_offset = VKMIN_NONE});

    w->pane_first = w->instance_count;
    const vec3 pane_pos[2] = {{-2.0f, 1.6f, 4.9f}, {3.0f, 1.6f, -4.9f}};
    for (int i = 0; i < 2; ++i) {
        const mat4 t = vkmin_mat4_mul(vkmin_mat4_translate(pane_pos[i]), vkmin_mat4_scale((vec3){1.4f, 1.2f, 1.0f}));
        add_instance(w, (Instance){.transform = t, .bounds = gk_world_bounds(t, quad_bounds), .mesh = w->quad_mesh,
                                   .material = w->glass_mat, .bone_offset = VKMIN_NONE});
    }

    /* The character: CesiumMan, scaled up a little, standing in the courtyard. */
    w->character = scene_load(opt->character_path);
    w->char_mesh0 = gk_load_scene(r, gpu, &w->character, &w->char_mat0);
    w->char_index = w->instance_count;
    const mat4 placement = vkmin_mat4_mul(vkmin_mat4_translate((vec3){0.5f, 0.0f, 1.5f}),
                                    vkmin_mat4_mul(vkmin_mat4_rotate_y(-0.6f), vkmin_mat4_scale((vec3){1.6f, 1.6f, 1.6f})));
    add_instance(w, gk_scene_instance(&w->character, 0, w->char_mesh0, w->char_mat0, placement));
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
        inst->bounds = gk_world_bounds(inst->transform, (vec4){0, 0, 0, inst->mesh == w->cube_mesh ? 1.75f : 1.0f});
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
    w->bone_count = anim_bones(&w->character, time, gk_node_transform(&w->character, 0), w->bones, MAX_BONES);
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
    /* d_check_cull: the GPU draw list must agree with the CPU reference. */
    const vkr_stats final = vkr_get_stats(r);
    const int status = final.cull_mismatches ? 1 : 0;
    if (cvar_get_bool(CV_d_check_cull)) printf("corridor: cull check: %u mismatches\n", final.cull_mismatches);

    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    scene_free(&w->sponza);
    scene_free(&w->character);
    free(w);
    return status;
}
