/* 10_shooter -- a first-person camera through Sponza, two dozen point lights,
 * the sun through the windows with cascaded shadows, two skinned enemies on
 * patrol loops, muzzle flash and smoke particles on fire, and a HUD. The
 * Corridor demo, made playable.
 *
 *   WASD moves, the mouse looks, left button fires.
 *
 * Everything the player did not do is a pure function of the tick: patrol
 * positions, light colours, smoke puffs (each is spawn tick plus arithmetic).
 * What the player did do is in the input snapshot, which the journal keeps. */
#include "gamekit.h"

enum { HZ = 60, LIGHTS = 24, ENEMIES = 2, PUFFS = 48, MAX_QUADS = 512, MAX_BONES = 2 * ANIM_MAX_JOINTS };
#define EYE_HEIGHT 1.7f

typedef struct { float x, y, z, vx, vy, vz; uint32_t born; } puff;

typedef struct {
    vec3 pos;
    float yaw, pitch;
    float prev_mouse_x, prev_mouse_y;
    bool mouse_valid;
    uint32_t tick, fired_tick, shots, hits;
    uint32_t hit_until[ENEMIES];  /* the tick an enemy's hit flash ends */
    puff puffs[PUFFS];
    uint32_t next_puff;
} player;

/* Patrol loops: a rounded rectangle around the courtyard, the second enemy
 * half a lap behind and on the upper floor. */
static vec3 enemy_position(uint32_t e, uint32_t tick, float *yaw) {
    const float t = (float)tick / HZ * 0.45f + (float)e * 3.14159f;
    const vec3 p = {8.5f * cosf(t) * (e ? 0.75f : 1.0f), e ? 4.35f : 0.0f, 4.0f * sinf(t)};
    *yaw = atan2f(-8.5f * sinf(t), 4.0f * cosf(t)); /* facing along the tangent */
    return p;
}

static void player_tick(player *p, const vkmin_inputs *in) {
    const float speed = 0.06f;
    const vec3 fwd = {-sinf(p->yaw), 0.0f, -cosf(p->yaw)}, right = {cosf(p->yaw), 0.0f, -sinf(p->yaw)};
    vec3 move = {0, 0, 0};
    if (vkmin_key_down(in, 'W')) move = vkmin_vec3_add(move, fwd);
    if (vkmin_key_down(in, 'S')) move = vkmin_vec3_sub(move, fwd);
    if (vkmin_key_down(in, 'D')) move = vkmin_vec3_add(move, right);
    if (vkmin_key_down(in, 'A')) move = vkmin_vec3_sub(move, right);
    if (vkmin_vec3_length(move) > 0.0f) p->pos = vkmin_vec3_add(p->pos, vkmin_vec3_scale(vkmin_vec3_normalize(move), speed));
    /* The courtyard's walkable floor, as a box: the cheapest collision that keeps the camera out of the walls. */
    p->pos.x = fminf(fmaxf(p->pos.x, -11.0f), 11.0f);
    p->pos.z = fminf(fmaxf(p->pos.z, -5.2f), 5.2f);
    p->pos.y = EYE_HEIGHT;
    p->tick++;
}

static void fire(player *p) {
    p->shots++;
    p->fired_tick = p->tick;
    const vec3 dir = {-sinf(p->yaw) * cosf(p->pitch), sinf(p->pitch), -cosf(p->yaw) * cosf(p->pitch)};
    const vkmin_ray ray = {.origin = p->pos, .dir = dir};
    for (uint32_t e = 0; e < ENEMIES; ++e) {
        float yaw, t;
        const vec3 c = enemy_position(e, p->tick, &yaw);
        if (vkmin_ray_aabb(ray, (vec3){c.x - 0.5f, c.y, c.z - 0.5f}, (vec3){c.x + 0.5f, c.y + 2.0f, c.z + 0.5f}, &t)) {
            p->hits++;
            p->hit_until[e] = p->tick + 20;
        }
    }
    /* Six smoke puffs from the muzzle, drifting up and forward. */
    const vec3 muzzle = vkmin_vec3_add(p->pos, vkmin_vec3_add(vkmin_vec3_scale(dir, 0.6f), (vec3){0.15f, -0.15f, 0}));
    for (int k = 0; k < 6; ++k) {
        const uint32_t i = p->next_puff++ % PUFFS;
        const float s = (float)p->shots * 17.0f + (float)k;
        p->puffs[i] = (puff){muzzle.x, muzzle.y, muzzle.z, dir.x * 0.02f + (gk_hash(51, (uint32_t)s) - 0.5f) * 0.02f,
                             0.01f + gk_hash(52, (uint32_t)s) * 0.01f, dir.z * 0.02f + (gk_hash(53, (uint32_t)s) - 0.5f) * 0.02f, p->tick};
    }
}

static const char *usage = "usage: 10_shooter [--profile lavapipe] [vkmin flags] [cvar=value ...]\n  WASD moves, mouse looks, left button fires\n";

int main(int argc, char **argv) {
    const gk_options opt = gk_parse(argc, argv, usage);
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "10_shooter", .device_index = opt.device});
    int width = 0, height = 0;
    vkmin_size(gpu, &width, &height);
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = width, .height = height, .shadow_atlas = cvar_get_int(CV_r_shadow_atlas),
                                       .max_vertices = 400000, .max_indices = 1200000, .max_skin_vertices = 8192, .max_meshes = 256,
                                       .max_materials = 64, .max_instances = 512});
    const scene sponza = scene_load("assets/sponza/scene.vkm"), hero = scene_load("assets/cesium/scene.vkm");
    uint32_t sponza_mat0 = 0, hero_mat0 = 0;
    const uint32_t sponza_mesh0 = gk_load_scene(r, gpu, &sponza, &sponza_mat0);
    const uint32_t hero_mesh0 = gk_load_scene(r, gpu, &hero, &hero_mat0);
    const Material flash_mat = gk_material(1.0f, 0.2f, 0.1f, 0.0f, 0.5f, VKMIN_MAT_UNLIT);
    const uint32_t hit_mat = vkr_upload_materials(r, &flash_mat, 1);
    const uint32_t disc = gk_disc_texture(gpu, 64);

    Instance *instances = calloc(sponza.header.node_count + ENEMIES, sizeof(Instance));
    if (!instances) gk_die("out of memory");
    uint32_t n_static = 0;
    for (uint32_t i = 0; i < sponza.header.node_count; ++i) instances[n_static++] = gk_scene_instance(&sponza, i, sponza_mesh0, sponza_mat0, vkmin_mat4_identity());

    Light lights[LIGHTS + 1];
    lights[0] = gk_sun((vec3){0.35f, -1.0f, 0.25f}, 5.0f);
    for (uint32_t i = 0; i < LIGHTS; ++i) {
        /* fixed lamps along the arcades, warm and cool by turns */
        const float x = -10.0f + 20.0f * (float)(i % 12) / 11.0f, z = (i < 12 ? -1.0f : 1.0f) * 3.8f;
        const vec3 c = (i & 1) ? (vec3){1.0f, 0.7f, 0.4f} : (vec3){0.4f, 0.7f, 1.0f};
        lights[1 + i] = gk_point_light((vec3){x, 1.8f + 2.4f * (float)((i / 3) & 1), z}, 4.5f, c, 4.0f);
    }

    player p = {.pos = {-9.0f, EYE_HEIGHT, 0.0f}, .yaw = -1.5708f};
    Quad quads[MAX_QUADS];
    mat4 bones[MAX_BONES];
    uint32_t ticks_done = 0;
    char hud[128];
    while (vkmin_frame_begin(gpu, NULL)) {
        const vkmin_inputs *in = vkmin_input(gpu);
        const uint32_t frame = vkmin_frame_index(gpu);
        vkmin_size(gpu, &width, &height);
        if (vkmin_key_hit(gpu, VKMIN_KEY_ESCAPE)) { vkmin_frame_end(gpu); break; }

        /* Mouse look from the snapshot's deltas; the first frame only records. */
        if (p.mouse_valid) {
            p.yaw -= (in->mouse_x - p.prev_mouse_x) * 0.004f;
            p.pitch = fminf(fmaxf(p.pitch - (in->mouse_y - p.prev_mouse_y) * 0.004f, -1.2f), 1.2f);
        }
        p.prev_mouse_x = in->mouse_x;
        p.prev_mouse_y = in->mouse_y;
        p.mouse_valid = true;
        const uint32_t due = gk_ticks_due(frame, HZ, &ticks_done);
        for (uint32_t k = 0; k < due; ++k) player_tick(&p, in);
        if (in->buttons_pressed & VKMIN_MOUSE_LEFT) fire(&p);

        const vkmin_camera cam = vkmin_camera_fps(p.pos, p.yaw, p.pitch, 1.2f, (float)width / (float)height, 0.1f, 120.0f);

        /* --- enemies: skinned, on their loops, red for a moment when hit --- */
        uint32_t n = n_static, bone_count = 0;
        for (uint32_t e = 0; e < ENEMIES; ++e) {
            float yaw;
            const vec3 c = enemy_position(e, p.tick, &yaw);
            const mat4 placement = vkmin_mat4_mul(vkmin_mat4_translate(c), vkmin_mat4_mul(vkmin_mat4_rotate_y(yaw), vkmin_mat4_scale((vec3){1.3f, 1.3f, 1.3f})));
            instances[n] = gk_scene_instance(&hero, 0, hero_mesh0, hero_mat0, placement);
            instances[n].bone_offset = bone_count;
            instances[n].id = 1000 + e;
            if (p.tick < p.hit_until[e]) instances[n].material = hit_mat;
            bone_count += anim_bones(&hero, (float)p.tick / HZ * 1.1f + (float)e, gk_node_transform(&hero, 0), bones + bone_count, ANIM_MAX_JOINTS);
            n++;
        }

        /* --- particles: the flash for three ticks, puffs for two seconds --- */
        uint32_t nq = 0;
        const vec3 fwd = {-sinf(p.yaw) * cosf(p.pitch), sinf(p.pitch), -cosf(p.yaw) * cosf(p.pitch)};
        if (p.shots && p.tick - p.fired_tick < 3) {
            const vec3 m = vkmin_vec3_add(p.pos, vkmin_vec3_add(vkmin_vec3_scale(fwd, 1.2f), (vec3){0.25f, -0.2f, 0}));
            quads[nq++] = (Quad){.pos = {m.x, m.y, m.z, (float)p.tick}, .size_uv0 = {0.22f, 0.22f, 0, 0}, .uv1 = {1, 1, 0, 0},
                                 .color = 0xff80e0ffu, .texture = disc, .flags = VKMIN_QUAD_BILLBOARD};
        }
        for (uint32_t i = 0; i < PUFFS; ++i) {
            const puff *q = &p.puffs[i];
            const uint32_t age = p.tick - q->born;
            if (q->born == 0 || age > 2 * HZ) continue;
            const float t = (float)age, fade = 1.0f - t / (2.0f * HZ);
            quads[nq++] = (Quad){.pos = {q->x + q->vx * t, q->y + q->vy * t + 0.00005f * t * t, q->z + q->vz * t, t * 0.02f},
                                 .size_uv0 = {0.15f + 0.01f * t, 0.15f + 0.01f * t, 0, 0}, .uv1 = {1, 1, 0, 0},
                                 .color = gk_rgba(0.6f, 0.6f, 0.65f, 0.5f * fade), .texture = disc, .flags = VKMIN_QUAD_BILLBOARD};
        }
        /* --- HUD: crosshair, health bar, text ----------------------------- */
        const float cx = (float)width * 0.5f, cy = (float)height * 0.5f;
        const Quad bar = {.uv1 = {1, 1, 0, 0}, .color = 0xffe0e0e0u, .flags = VKMIN_QUAD_SCREEN};
        Quad cross[4] = {bar, bar, bar, bar};
        cross[0].pos = (vec4){cx - 6, cy, 0, 0}; cross[0].size_uv0 = (vec4){4, 1, 0, 0};
        cross[1].pos = (vec4){cx + 6, cy, 0, 0}; cross[1].size_uv0 = (vec4){4, 1, 0, 0};
        cross[2].pos = (vec4){cx, cy - 6, 0, 0}; cross[2].size_uv0 = (vec4){1, 4, 0, 0};
        cross[3].pos = (vec4){cx, cy + 6, 0, 0}; cross[3].size_uv0 = (vec4){1, 4, 0, 0};
        for (int k = 0; k < 4; ++k) quads[nq++] = cross[k];
        const float health = 0.6f + 0.4f * cosf((float)p.tick / HZ * 0.5f);
        quads[nq++] = (Quad){.pos = {46, (float)height - 10, 0, 0}, .size_uv0 = {84, 8, 0, 0}, .uv1 = {1, 1, 0, 0}, .color = 0xa0202020u, .flags = VKMIN_QUAD_SCREEN};
        quads[nq++] = (Quad){.pos = {6 + 40 * health, (float)height - 10, 0, 0}, .size_uv0 = {80 * health, 5, 0, 0}, .uv1 = {1, 1, 0, 0},
                             .color = 0xff40d040u, .flags = VKMIN_QUAD_SCREEN};
        snprintf(hud, sizeof hud, "shots %u  hits %u", p.shots, p.hits);
        nq += vkr_text(r, hud, (float)width - 100.0f, (float)height - 18.0f, 12.0f, 0xffffffffu, quads + nq, MAX_QUADS - nq);

        vkr_frame(r, &(vkr_frame_desc){.view = cam.view, .proj = cam.proj, .camera_pos = {p.pos.x, p.pos.y, p.pos.z, 1},
                                       .near = 0.1f, .far = 120.0f, .instances = instances, .instance_count = n, .lights = lights, .light_count = LIGHTS + 1,
                                       .bones = bones, .bone_count = bone_count, .quads = quads, .quad_count = nq, .frame_index = frame,
                                       .look = {.fog = {0.5f, 0.55f, 0.6f}, .fog_density = 0.012f}});
        vkmin_frame_end(gpu);
    }
    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    scene_free((scene *)&sponza);
    scene_free((scene *)&hero);
    free(instances);
    return 0;
}
