/* 13_platformer -- a side-on orthographic camera, three parallax layers
 * through the quad batcher, one skinned character with idle, run and jump,
 * moving platforms as instances, cel shaded with a thin outline.
 *
 *   A / D run, Space jumps.
 *
 * Physics runs at 60 Hz. Idle/jump pose tracks and the imported run clip
 * share the character showcase's animation sampler. */
#include "play.h"
#include "shaders.h"

enum { HZ = 60, PLATFORMS = 14, MOVING = 2, LAYERS = 3, MAX_QUADS = 256 };

typedef struct { float x, y, w, h; } box;

/* The level: boxes the character stands on. The last MOVING ones move. */
static box platform_at(uint32_t i, uint32_t tick) {
    static const box fixed[PLATFORMS - MOVING] = {
        {-6, -1, 14, 1}, {10, 0, 4, 1}, {16, 1.5f, 4, 1}, {23, 0, 6, 1}, {31, 2, 3, 1}, {36, 0.5f, 5, 1},
        {44, -1, 10, 1}, {56, 1, 3, 1}, {61, 2.5f, 3, 1}, {66, 0, 8, 1}, {77, 1, 4, 1}, {84, -1, 12, 1}};
    if (i < PLATFORMS - MOVING) return fixed[i];
    const float t = (float)tick / HZ;
    if (i == PLATFORMS - MOVING) return (box){6.0f + 3.0f * sinf(t * 0.8f), 1.5f, 3, 0.6f};
    return (box){28.0f, 1.0f + 1.5f * sinf(t * 1.1f), 3, 0.6f};
}

typedef struct {
    float x, y, vx, vy;
    bool grounded, facing_right;
    float run_time;      /* seconds of the run clip played */
    uint32_t tick, jumps, landed, jump_tick, landing_tick, falls;
} player;

static void player_tick(player *p, const vkmin_inputs *in) {
    const float accel = 0.012f, max_speed = 0.11f, gravity = 0.018f;
    const float want = (vkmin_key_down(in, 'D') ? 1.0f : 0.0f) - (vkmin_key_down(in, 'A') ? 1.0f : 0.0f);
    p->vx += want * accel;
    if (want == 0.0f) p->vx *= 0.8f;
    p->vx = fminf(fmaxf(p->vx, -max_speed), max_speed);
    if (want != 0.0f) p->facing_right = want > 0.0f;
    const bool was_grounded = p->grounded;
    if (p->grounded && vkmin_key_pressed(in, VKMIN_KEY_SPACE)) { p->vy = 0.36f; p->jumps++; p->jump_tick = p->tick; }
    p->vy -= gravity;
    /* Carried by a moving platform: its delta this tick. */
    const float feet_before = p->y;
    p->x += p->vx;
    p->y += p->vy;
    p->grounded = false;
    for (uint32_t i = 0; i < PLATFORMS; ++i) {
        const box b = platform_at(i, p->tick + 1), prev = platform_at(i, p->tick);
        const float top = b.y + b.h;
        if (p->vy <= 0.0f && p->x + 0.3f > b.x && p->x - 0.3f < b.x + b.w && feet_before >= prev.y + prev.h - 0.01f && p->y <= top) {
            p->y = top;
            p->vy = 0.0f;
            p->grounded = true;
            p->x += b.x - prev.x;
        }
    }
    if (p->grounded && !was_grounded) { ++p->landed; p->landing_tick = p->tick; }
    if (p->y < -12.0f) { p->x = 0; p->y = 2; p->vx = 0; p->vy = 0; ++p->falls; }
    if (p->grounded && fabsf(p->vx) > 0.01f) p->run_time += 1.0f / HZ;
    p->tick++;
}

/* A silhouette texture: a skyline of `bumps` humps in `color`, transparent above. */
static uint32_t skyline_texture(vkmin_ctx *gpu, int size, int bumps, uint32_t color, uint32_t seed) {
    uint32_t *px = malloc((size_t)size * size * 4u);
    if (!px) gk_die("out of memory");
    for (int x = 0; x < size; ++x) {
        const float u = (float)x / (float)size * (float)bumps;
        const int k = (int)u;
        const float h0 = 0.3f + 0.5f * gk_hash(seed, (uint32_t)k), h1 = 0.3f + 0.5f * gk_hash(seed, (uint32_t)(k + 1) % (uint32_t)bumps);
        const float f = u - (float)k, h = h0 + (h1 - h0) * (0.5f - 0.5f * cosf(f * 3.14159f));
        for (int y = 0; y < size; ++y) px[y * size + x] = ((float)(size - 1 - y) / (float)size) < h ? color : 0u;
    }
    const uint32_t tex = gk_texture(gpu, size, px, VKMIN_SAMPLER_LINEAR_REPEAT, "platformer.skyline");
    free(px);
    return tex;
}

static const char *usage = "usage: 13_platformer [--profile lavapipe] [vkmin flags] [cvar=value ...]\n  A/D run, Space jumps\n";

int main(int argc, char **argv) {
    const gk_options opt = gk_parse(argc, argv, usage);
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "13_platformer", .device_index = opt.device});
    int width = 0, height = 0;
    vkmin_size(gpu, &width, &height);
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = width, .height = height, .shadow_atlas = cvar_get_int(CV_r_shadow_atlas),
                                       .max_vertices = 16384, .max_indices = 65536, .max_skin_vertices = 8192, .max_meshes = 16,
                                       .max_materials = 8, .max_instances = 32, .fs = VKMIN_BYTES(lit_cel_frag_spv)});
    const gk_shapes shapes = gk_upload_shapes(r);
    const uint32_t disc = gk_disc_texture(gpu, 32), grade = gk_grade(gpu, .08f, 1.08f);
    const scene hero = scene_load("assets/cesium/scene.vkm");
    uint32_t hero_mat0 = 0;
    const uint32_t hero_mesh0 = gk_load_scene(r, gpu, &hero, &hero_mat0);
    const Material mats[2] = {gk_material(0.45f, 0.30f, 0.20f, 0.0f, 0.8f, 0), gk_material(0.55f, 0.60f, 0.65f, 0.2f, 0.5f, 0)};
    const uint32_t mat0 = vkr_upload_materials(r, mats, 2);
    const uint32_t layers[LAYERS] = {skyline_texture(gpu, 128, 5, gk_rgba(0.42f, 0.46f, 0.62f, 1), 41),
                                     skyline_texture(gpu, 128, 9, gk_rgba(0.30f, 0.40f, 0.36f, 1), 43),
                                     skyline_texture(gpu, 128, 14, gk_rgba(0.20f, 0.32f, 0.22f, 1), 47)};

    player p = {.x = 0.0f, .y = 2.0f, .facing_right = true};
    player previous = p;
    gk_clock clock = {0};
    bool help = true;
    FILE *trace = gk_trace_open(argc, argv, "tick jumps landings falls x y animation");
    Instance instances[PLATFORMS + 1];
    Quad quads[MAX_QUADS];
    mat4 bones[ANIM_MAX_JOINTS];
    char hud[64];
    while (vkmin_running(gpu)) {
        const vkmin_frame fr = vkmin_frame_begin(gpu, NULL); /* the one read of the outside world */
        const vkmin_inputs *in = &fr.input;
        const uint32_t frame = fr.index;
        width = fr.width;
        height = fr.height;
        if (vkmin_key_pressed(in, VKMIN_KEY_ESCAPE)) { vkmin_frame_end(gpu); break; }
        if (vkmin_key_pressed(in, VKMIN_KEY_F1)) help = !help;
        if (vkmin_key_pressed(in, 'R')) { p = (player){.y = 2, .facing_right = true}; previous = p; clock = (gk_clock){.origin = frame}; }
        const gk_step step = gk_step_frame(&clock, frame, HZ, *in);
        for (uint32_t k = 0; k < step.due; ++k) { previous = p; const vkmin_inputs tick_input = gk_tick_input(step, k); player_tick(&p, &tick_input); }
        const float rx = gk_lerp(previous.x, p.x, step.alpha), ry = gk_lerp(previous.y, p.y, step.alpha);

        const float aspect = (float)width / (float)height, half_h = 5.0f;
        const vkmin_camera cam = vkmin_camera_side((vec3){rx + 2.0f, 2.5f, 0.0f}, half_h, aspect, 30.0f);

        /* --- platforms as cubes, the hero as a skinned instance ----------- */
        uint32_t n = 0;
        for (uint32_t i = 0; i < PLATFORMS; ++i) {
            box b = platform_at(i, p.tick);
            const box before = platform_at(i, previous.tick);
            b.x = gk_lerp(before.x, b.x, step.alpha); b.y = gk_lerp(before.y, b.y, step.alpha);
            const mat4 t = vkmin_mat4_mul(vkmin_mat4_translate((vec3){b.x + b.w * 0.5f, b.y + b.h * 0.5f, 0.0f}),
                                          vkmin_mat4_scale((vec3){b.w * 0.5f, b.h * 0.5f, 1.5f}));
            instances[n++] = (Instance){.transform = t, .prev_transform = t, .bounds = gk_world_bounds(t, (vec4){0, 0, 0, 1.7321f}),
                                        .mesh = shapes.cube, .material = mat0 + (i >= PLATFORMS - MOVING ? 1u : 0u), .bone_offset = VKMIN_NONE, .id = i + 1u};
        }
        const uint32_t animation = !p.grounded ? ANIM_JUMP : fabsf(p.vx) > .01f ? ANIM_RUN : ANIM_IDLE;
        const float anim_time = animation == ANIM_RUN ? gk_lerp(previous.run_time, p.run_time, step.alpha) : (float)p.tick / HZ;
        const mat4 placement = vkmin_mat4_mul(vkmin_mat4_translate((vec3){rx, ry, 0.0f}),
                                              vkmin_mat4_mul(vkmin_mat4_rotate_y(p.facing_right ? 1.5708f : -1.5708f), vkmin_mat4_scale((vec3){1.1f, 1.1f, 1.1f})));
        instances[n] = gk_scene_instance(&hero, 0, hero_mesh0, hero_mat0, placement);
        instances[n].id = 100;
        const uint32_t bone_count = anim_character(&hero, anim_time, animation, (float)(p.tick - p.jump_tick) / HZ, gk_node_transform(&hero, 0), bones, ANIM_MAX_JOINTS);
        n++;

        /* --- parallax: each layer is a row of quads that follows the camera
         * at a fraction of its speed, so the far ones barely move ---------- */
        uint32_t nq = 0;
        for (int l = 0; l < LAYERS; ++l) {
            const float k = 0.15f + 0.3f * (float)l, tile_w = 24.0f - 6.0f * (float)l, z = -26.0f + 8.0f * (float)l;
            const float base_y = 6.5f - 2.2f * (float)l, tile_h = 8.0f - 1.5f * (float)l;
            const float scroll = cam.pos.x * (1.0f - k);
            const float first = floorf((cam.pos.x - scroll - half_h * aspect) / tile_w) - 1.0f;
            for (int i = 0; i < 5 && nq < MAX_QUADS; ++i) {
                const float x = scroll + (first + (float)i) * tile_w;
                quads[nq++] = (Quad){.pos = {x + tile_w * 0.5f, base_y, z, 0.0f}, .size_uv0 = {tile_w, tile_h, 0, 0}, .uv1 = {1, 1, 0, 0},
                                     .color = 0xffffffffu, .texture = layers[l]};
            }
        }
        const float dust_age = (float)(p.tick - p.landing_tick);
        if (p.landed && dust_age < 24) for (uint32_t i = 0; i < 6; ++i) {
            const float side = (float)i - 2.5f;
            quads[nq++] = (Quad){.pos = {rx + side * dust_age * .025f, ry + .1f + dust_age * .008f, 1.6f, 0},
                .size_uv0 = {.35f, .2f, 0, 0}, .uv1 = {1, 1, 0, 0}, .texture = disc,
                .color = gk_rgba(.85f, .7f, .45f, (1-dust_age/24)*.6f)};
        }
        snprintf(hud, sizeof hud, "%s  jumps %u  landings %u", p.x > 88 ? "COURSE CLEAR" : "Reach the far platform", p.jumps, p.landed);
        nq += gk_card(r, width, "SKYWAY / platformer", hud, "A/D run  SPACE jump  R restart  F1 help", help, quads + nq, MAX_QUADS - nq);
        const uint32_t words[] = {p.tick, p.jumps, p.landed, p.falls, gk_float_bits(p.x), gk_float_bits(p.y), animation};
        gk_trace(trace, frame, words, sizeof words / sizeof words[0]);

        const Light sun = gk_sun((vec3){-0.4f, -1.0f, -0.6f}, 3.5f);
        vkr_frame(r, &(vkr_frame_desc){.view = cam.view, .proj = cam.proj, .camera_pos = {cam.pos.x, cam.pos.y, cam.pos.z, 1},
                                       .near = 0.1f, .far = 60.0f, .instances = instances, .instance_count = n, .lights = &sun, .light_count = 1,
                                       .bones = bones, .bone_count = bone_count, .quads = quads, .quad_count = nq, .frame = fr,
                                       .look = {.rim_strength = 0.3f, .rim_power = 3.0f, .shadow_tint = {0.55f, 0.55f, 0.7f},
                                                .outline = 0.7f, .outline_depth = 0.04f, .lut_tex = grade, .lut_strength = .55f}});
        vkmin_frame_end(gpu);
    }
    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    scene_free((scene *)&hero);
    if (trace) fclose(trace);
    return 0;
}
