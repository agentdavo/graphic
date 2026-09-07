/* scene -- the render layer's demo, and the only program that links it.
 *
 * A courtyard of cubes and spheres on a plain, one sun with cascades, four
 * orbiting point lights that cast shadows, and a few transparent billboards.
 * It exists to exercise vkr_frame: the cull dispatch, the shadow atlas, the
 * depth prepass, light clustering, the forward pass, transparent sorting and
 * the overlay. Until this existed, all of render.c ran nowhere.
 *
 * Everything is a function of the frame index -- positions, the sun, the
 * lights, the camera. No clock, no rand(). So:
 *
 *   ./build/scene --headless --frame 300 --out a.png --record f.vkj
 *   ./build/replay --replay f.vkj --frame 300 --path=legacy --out b.png
 *   cmp a.png b.png
 *
 * is a real check of the render layer, which is the point of the program.
 */
#include "gamekit.h"

enum { GRID = 12, OBJECTS = GRID * GRID, GROUND = OBJECTS, INSTANCES = OBJECTS + 1,
       LIGHTS = 4, BILLBOARDS = 6, SPACING = 4 };

/* The courtyard is a lattice with a per-object hash, so the scene is a pure
 * function of its indices and identical on every machine. */
static mat4 object_transform(uint32_t i, uint32_t frame) {
    const float fx = (float)(i % GRID) - (float)(GRID - 1) * 0.5f;
    const float fz = (float)(i / GRID) - (float)(GRID - 1) * 0.5f;
    const float t = (float)frame / 60.0f;
    const float phase = gk_hash(7u, i) * 6.2831853f;
    const float height = 1.0f + gk_hash(11u, i) * 2.5f + 0.35f * sinf(t + phase);
    const float spin = t * (0.2f + 0.4f * gk_hash(13u, i)) + phase;
    const float scale = 0.5f + 0.5f * gk_hash(17u, i);
    const mat4 rotate = min_mat4_rotate_y(spin);
    mat4 m = min_mat4_scale((vec3){scale, scale, scale});
    m = min_mat4_mul(rotate, m);
    m.m[12] = fx * (float)SPACING;
    m.m[13] = height;
    m.m[14] = fz * (float)SPACING;
    return m;
}

/* One orbit per light, staggered, inside the courtyard so their shadows fall
 * across the lattice rather than off the edge of it. */
static vec3 light_position(uint32_t k, uint32_t frame) {
    const float t = (float)frame / 60.0f;
    const float angle = t * 0.35f + (float)k * 1.5707963f;
    const float radius = 9.0f + 3.0f * (float)(k & 1u);
    return (vec3){sinf(angle) * radius, 3.5f + 1.5f * sinf(t * 0.7f + (float)k), cosf(angle) * radius};
}

int main(int argc, char **argv) {
    const gk_options opt = gk_parse(argc, argv,
        "scene -- the render layer demo\n"
        "  --headless --frame N --out FILE   render one frame and exit\n"
        "  --record FILE                     journal every call after init\n"
        "  --profile lavapipe                small settings for a GPU-less runner\n"
        "  +r_gpu_cull 0                     any cvar; --cvars lists them\n");
    /* Outdoor is decided before vkr_init because it creates the sky, water,
     * history and bloom targets there, so it cannot be a per-frame cvar. */
    bool outdoor = false;
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--outdoor")) outdoor = true;
    cvar_state config = opt.config;
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "scene",
        .width = 1280, .height = 720, .vsync = true, .headless = opt.headless, .config = &config,
        .device_index = opt.device, .image_arena_bytes = 128u << 20});

    int width = 0, height = 0;
    vkmin_size(gpu, &width, &height);
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = width, .height = height, .shadow_atlas = 2048,
        .max_vertices = 8192, .max_indices = 16384, .max_meshes = 8, .max_materials = 8,
        .max_instances = INSTANCES + BILLBOARDS, .outdoor = outdoor});

    const gk_shapes shapes = gk_upload_shapes(r);
    const uint32_t checker = gk_checker_texture(gpu, 256, 16,
        gk_rgba(0.82f, 0.80f, 0.76f, 1.0f), gk_rgba(0.28f, 0.30f, 0.34f, 1.0f));
    const uint32_t disc = gk_disc_texture(gpu, 64);
    Material materials[4] = {
        gk_material(0.85f, 0.32f, 0.24f, 0.0f, 0.45f, 0),   /* clay   */
        gk_material(0.72f, 0.74f, 0.78f, 1.0f, 0.22f, 0),   /* metal  */
        gk_material(0.90f, 0.90f, 0.90f, 0.0f, 0.85f, 0),   /* ground */
        gk_material(1.00f, 0.86f, 0.55f, 0.0f, 1.00f, 0),   /* billboard */
    };
    materials[2].albedo_tex = checker;
    materials[3].albedo_tex = disc;
    const uint32_t first_material = vkr_upload_materials(r, materials, 4);

    Instance instances[INSTANCES];
    Light lights[LIGHTS + 1];
    Quad billboards[BILLBOARDS];
    uint32_t done = 0;

    while (vkmin_running(gpu)) {
        const vkmin_frame f = vkmin_frame_begin(gpu, NULL);
        const uint32_t frame = f.index;
        const float t = (float)frame / 60.0f;
        gk_ticks_due(frame, 60u, &done);   /* the fixed-step hook a game would use */

        for (uint32_t i = 0; i < OBJECTS; ++i) {
            const mat4 m = object_transform(i, frame);
            const bool sphere = (gk_hash(23u, i) > 0.5f);
            const float radius = sphere ? 1.0f : 1.7321f;
            instances[i] = (Instance){.transform = m, .prev_transform = object_transform(i, frame ? frame - 1u : 0u),
                .bounds = gk_world_bounds(m, (vec4){0, 0, 0, radius}),
                .mesh = sphere ? shapes.sphere : shapes.cube,
                .material = first_material + (i & 1u), .bone_offset = VKMIN_NONE, .id = i + 1u};
        }
        /* One huge ground instance would inflate the scene bounds the sun
         * cascades are fitted to, and the near cascade would lose most of its
         * resolution to empty plain. A tile the courtyard's own size keeps the
         * cascades tight enough that the shadows are visible, which is what
         * makes this program useful as a regression target. */
        mat4 ground = min_mat4_scale((vec3){28.0f, 0.25f, 28.0f});
        ground.m[13] = -0.25f;
        instances[GROUND] = (Instance){.transform = ground, .prev_transform = ground,
            .bounds = gk_world_bounds(ground, (vec4){0, 0, 0, 1.7321f}), .mesh = shapes.cube,
            .material = first_material + 2u, .bone_offset = VKMIN_NONE, .id = INSTANCES};

        /* The two helpers use opposite conventions and do not compose without
         * this negation: vkmin_sun_direction returns the direction *to* the
         * sun, while Light.dir_cone is the direction the light travels --
         * build_views negates it to recover to_sun. Passing it through
         * unnegated puts the sun below the horizon and every surface loses its
         * direct term, which looks like flat ambient rather than an error. */
        const vec4 to_sun = vkmin_sun_direction(9.0f + 2.0f * sinf(t * 0.05f));
        lights[0] = gk_sun((vec3){-to_sun.x, -to_sun.y, -to_sun.z}, 3.2f);
        for (uint32_t k = 0; k < LIGHTS; ++k) {
            const vec3 p = light_position(k, frame);
            const vec3 tint = {0.4f + 0.6f * gk_hash(31u, k), 0.4f + 0.6f * gk_hash(37u, k), 0.5f + 0.5f * gk_hash(41u, k)};
            lights[1u + k] = gk_point_light(p, 18.0f, tint, 40.0f);
        }

        /* Transparent, drawn after the opaque pass and sorted back to front. */
        for (uint32_t q = 0; q < BILLBOARDS; ++q) {
            const float angle = t * 0.25f + (float)q * 1.0471976f;
            billboards[q] = (Quad){
                .pos = {sinf(angle) * 13.0f, 4.0f + sinf(t + (float)q) * 1.2f, cosf(angle) * 13.0f, 0},
                .size_uv0 = {2.0f, 2.0f, 0.0f, 0.0f}, .uv1 = {1.0f, 1.0f, 0.0f, 0.0f},
                .color = gk_rgba(1.0f, 0.85f, 0.55f, 0.55f),
                .texture = disc, .flags = VKMIN_QUAD_BILLBOARD};
        }

        /* The outside path: analytic sky, the water plane and the bloom
         * composite. Terrain, grass and scatter are left out -- this exists to
         * put sky.frag, water.frag and taa.frag under the agreement check,
         * which nothing did before, not to rebuild a landscape. The maps are
         * the renderer's own default slots, which is what makes that possible
         * without an asset. The asserts in vkr_frame say which of these fields
         * must be positive; the rest are chosen to look like weather.
         *
         * Water sits just below the courtyard floor and the bounded plane is
         * wider than the tile, so it shows as a lake ringing the courtyard
         * rather than flooding it. The plane has to be visibly in frame: a
         * water pass that drew nothing would regress silently, and one that
         * drowns the scene hides every other regression behind it. */
        const Outdoor outside = {
            .terrain = {0.0f, 0.0f, 64.0f, 1.0f},
            .height = {-1.0f, 8.0f, -0.15f, 0.015f},
            .maps = {VKR_TEX_WHITE, VKR_TEX_WHITE, VKR_TEX_BLACK, VKR_TEX_WHITE},
            .albedo = {VKR_TEX_WHITE, VKR_TEX_WHITE, VKR_TEX_WHITE, VKR_TEX_WHITE},
            .normals = {VKR_TEX_FLAT_NORMAL, VKR_TEX_FLAT_NORMAL, VKR_TEX_FLAT_NORMAL, VKR_TEX_FLAT_NORMAL},
            .water_maps = {VKR_TEX_FLAT_NORMAL, VKR_TEX_FLAT_NORMAL, 0, 0},
            .weather = {0.35f, 0.6f, 0.05f, 60.0f},
            .water = {0.45f, 0.9f, 0.25f, 1.6f},
        };

        const float orbit = t * 0.12f;
        const vec3 eye = {sinf(orbit) * 34.0f, 12.0f + 4.0f * sinf(t * 0.09f), cosf(orbit) * 34.0f};
        const mat4 view = vkmin_mat4_look_at(eye, (vec3){0, 2.5f, 0}, (vec3){0, 1, 0});
        const mat4 proj = vkmin_mat4_perspective(1.0471976f, f.aspect, 0.1f, 300.0f);

        char overlay[160];
        const vkr_stats s = vkr_get_stats(r);
        snprintf(overlay, sizeof overlay, "scene  frame %u\ncamera draws %u\nshadow draws %u  views %u",
                 frame, s.draws_camera, s.draws_shadow, s.shadow_views);

        vkr_frame(r, &(vkr_frame_desc){.view = view, .proj = proj,
            .camera_pos = {eye.x, eye.y, eye.z, 1.0f}, .near = 0.1f, .far = 300.0f,
            .instances = instances, .instance_count = INSTANCES,
            .lights = lights, .light_count = LIGHTS + 1u,
            .quads = billboards, .quad_count = BILLBOARDS,
            .overlay_text = overlay,
            .look = {.outline = 0.35f, .fog = {0.42f, 0.48f, 0.58f}, .fog_density = 0.004f},
            .outdoor = outdoor ? &outside : NULL,
            .frame = f});
        vkmin_frame_end(gpu);
    }

    const vkr_stats s = vkr_finish(r);
    fprintf(stderr, "scene: %u camera draws, %u shadow draws over %u views, %u cull mismatches\n",
            s.draws_camera, s.draws_shadow, s.shadow_views, s.cull_mismatches);
    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    return 0;
}
