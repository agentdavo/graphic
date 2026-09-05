/* 11_rts -- a strategy game's rendering, whole: a tilted camera over two
 * square kilometres of heightfield, two thousand instanced units in two teams
 * on deterministic paths, box selection through the quad batcher, picking
 * through the ID target, health bars, one directional shadow.
 *
 *   WASD / arrows pan, wheel zooms, left drag selects, right click orders the
 *   selection to march there. Hover highlights what vkmin_pick returns.
 *
 * Simulation runs at 30 Hz from the frame index; rendering interpolates
 * nothing because units move per tick and ticks are cheap. Every number
 * below is a function of (tick, index) or of recorded input, so a demo file
 * replays the same battle. */
#include "gamekit.h"

enum { UNITS = 2000, TERRAIN_N = 181, TERRAIN_CHUNK = 30, HZ = 30, MAX_QUADS = 6000 };
#define CELL 8.0f
#define TERRAIN_SIZE ((float)(TERRAIN_N - 1) * CELL)

/* ---- the world, as pure functions of position ----------------------------- */

static float terrain_height(float x, float z) {
    return 45.0f * sinf(x * 0.0071f) * cosf(z * 0.0093f) + 14.0f * sinf(x * 0.031f + z * 0.019f) +
           4.0f * sinf(x * 0.09f) * sinf(z * 0.11f) + 60.0f;
}

typedef struct { float x, z; } pos2;

typedef struct {
    pos2 home, target;   /* home: where the orbit is centred; target: the ordered march destination */
    float hp;            /* 0..1 */
    uint32_t team;
    bool selected;
} unit;

typedef struct {
    unit units[UNITS];
    uint32_t tick;
    pos2 cam_target;
    float cam_distance;
    /* box select in progress */
    bool dragging;
    float drag_x0, drag_y0;
    uint32_t hovered;    /* unit id from the ID target, 0 = none */
    uint32_t selected_count, dead_count;
} game;

static pos2 unit_position(const unit *u, uint32_t i, uint32_t tick) {
    /* A slow orbit around home, plus a march towards the target that arrives
     * over a few seconds: both pure in the tick, so a unit is where it is. */
    const float t = (float)tick / (float)HZ;
    const float radius = 20.0f + 30.0f * gk_hash(7, i), speed = (0.05f + 0.1f * gk_hash(9, i)) * (u->team ? -1.0f : 1.0f);
    const float a = t * speed + 6.2831853f * gk_hash(11, i);
    return (pos2){u->target.x + radius * cosf(a), u->target.z + radius * sinf(a)};
}

static void game_init(game *g) {
    for (uint32_t i = 0; i < UNITS; ++i) {
        unit *u = &g->units[i];
        u->team = i & 1u;
        /* two armies, west and east of the centre, in loose clumps */
        const float cx = TERRAIN_SIZE * (u->team ? 0.62f : 0.38f) + (gk_hash(1, i) - 0.5f) * 300.0f;
        const float cz = TERRAIN_SIZE * 0.5f + (gk_hash(2, i) - 0.5f) * 500.0f;
        u->home = u->target = (pos2){cx, cz};
        u->hp = 1.0f;
    }
    g->cam_target = (pos2){TERRAIN_SIZE * 0.5f, TERRAIN_SIZE * 0.5f};
    g->cam_distance = 260.0f;
}

/* One tick: the march, and attrition where the armies meet. */
static void game_tick(game *g) {
    g->tick++;
    for (uint32_t i = 0; i < UNITS; ++i) {
        unit *u = &g->units[i];
        if (u->hp <= 0.0f) continue;
        const float dx = u->target.x - u->home.x, dz = u->target.z - u->home.z;
        const float d = sqrtf(dx * dx + dz * dz);
        const float step = 1.6f;
        if (d > step) u->home = (pos2){u->home.x + dx / d * step, u->home.z + dz / d * step};
        else u->home = u->target;
    }
    /* Attrition: a unit within 40 m of an enemy loses health; brute force
     * would be 4 million pairs, so bucket by team and check every 7th. */
    for (uint32_t i = 0; i < UNITS; ++i) {
        unit *u = &g->units[i];
        if (u->hp <= 0.0f) continue;
        const pos2 p = unit_position(u, i, g->tick);
        for (uint32_t j = (i + 1u) & 1u; j < UNITS; j += 14) {
            const unit *e = &g->units[j];
            if (e->hp <= 0.0f) continue;
            const pos2 q = unit_position(e, j, g->tick);
            const float dx = p.x - q.x, dz = p.z - q.z;
            if (dx * dx + dz * dz < 40.0f * 40.0f) { u->hp -= 0.02f; break; }
        }
    }
}

/* Where the camera ray meets the terrain, by marching; false when it misses. */
static bool terrain_hit(vkmin_ray ray, pos2 *out) {
    float t = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const vec3 p = vkmin_vec3_add(ray.origin, vkmin_vec3_scale(ray.dir, t));
        if (p.x < 0.0f || p.z < 0.0f || p.x > TERRAIN_SIZE || p.z > TERRAIN_SIZE) { if (i > 0) return false; }
        else if (p.y <= terrain_height(p.x, p.z)) { *out = (pos2){p.x, p.z}; return true; }
        t += 4.0f;
    }
    return false;
}

static const char *usage =
    "usage: 11_rts [--profile lavapipe] [vkmin flags] [cvar=value ...]\n"
    "  WASD/arrows pan, wheel zooms, left drag selects, right click orders a march\n";

int main(int argc, char **argv) {
    const gk_options opt = gk_parse(argc, argv, usage);
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "11_rts", .device_index = opt.device});
    int width = 0, height = 0;
    vkmin_size(gpu, &width, &height);
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = width, .height = height, .shadow_atlas = cvar_get_int(CV_r_shadow_atlas),
                                       .max_vertices = 60000, .max_indices = 300000, .max_meshes = 64, .max_materials = 16,
                                       .max_instances = UNITS + 64});

    /* Terrain: the heightfield as chunks the cull can reject. */
    float *heights = malloc(sizeof(float) * TERRAIN_N * TERRAIN_N);
    if (!heights) gk_die("out of memory");
    for (int z = 0; z < TERRAIN_N; ++z) {
        for (int x = 0; x < TERRAIN_N; ++x) heights[z * TERRAIN_N + x] = terrain_height((float)x * CELL, (float)z * CELL);
    }
    const vkmin_heightfield_desc hd = {.heights = heights, .width = TERRAIN_N, .height = TERRAIN_N, .cell = CELL, .chunk = TERRAIN_CHUNK, .uv_per_unit = 1.0f / 32.0f};
    const vkmin_heightfield_size hs = vkmin_heightfield_sizes(&hd);
    Vertex *tv = malloc(hs.vertices * sizeof(Vertex));
    uint32_t *ti = malloc(hs.indices * sizeof(uint32_t));
    Mesh *tm = malloc(hs.meshes * sizeof(Mesh));
    if (!tv || !ti || !tm) gk_die("out of memory");
    vkmin_heightfield(&hd, tv, ti, tm);
    const uint32_t terrain_mesh0 = vkr_upload_geometry(r, &(vkr_geometry){.vertices = tv, .vertex_count = hs.vertices, .indices = ti,
                                                                          .index_count = hs.indices, .meshes = tm, .mesh_count = hs.meshes});
    const gk_shapes shapes = gk_upload_shapes(r);

    const uint32_t grass = gk_checker_texture(gpu, 64, 8, gk_rgba(0.34f, 0.46f, 0.20f, 1), gk_rgba(0.24f, 0.36f, 0.15f, 1));
    Material mats[5] = {gk_material(1, 1, 1, 0.0f, 0.9f, 0), gk_material(0.20f, 0.35f, 0.95f, 0.1f, 0.5f, 0),
                        gk_material(0.95f, 0.22f, 0.18f, 0.1f, 0.5f, 0), gk_material(1.0f, 1.0f, 0.3f, 0.0f, 0.4f, VKMIN_MAT_UNLIT),
                        gk_material(0.15f, 0.15f, 0.15f, 0.0f, 0.9f, 0)};
    mats[0].albedo_tex = grass;
    const uint32_t mat0 = vkr_upload_materials(r, mats, 5);
    enum { MAT_TERRAIN = 0, MAT_BLUE, MAT_RED, MAT_HOVER, MAT_DEAD };

    game *g = calloc(1, sizeof *g);
    Instance *instances = calloc(UNITS + hs.meshes, sizeof(Instance));
    Quad *quads = calloc(MAX_QUADS, sizeof(Quad));
    if (!g || !instances || !quads) gk_die("out of memory");
    game_init(g);
    for (uint32_t m = 0; m < hs.meshes; ++m) {
        const mat4 id = vkmin_mat4_identity();
        instances[m] = (Instance){.transform = id, .prev_transform = id, .bounds = tm[m].bounds, .mesh = terrain_mesh0 + m,
                                  .material = mat0 + MAT_TERRAIN, .bone_offset = VKMIN_NONE};
    }
    free(heights); free(tv); free(ti); free(tm);

    uint32_t ticks_done = 0;
    char hud[256];
    while (vkmin_running(gpu)) {
        const vkmin_frame fr = vkmin_frame_begin(gpu, NULL); /* the one read of the outside world */
        const vkmin_inputs *in = &fr.input;
        const uint32_t frame = fr.index;
        width = fr.width;
        height = fr.height;
        if (vkmin_key_pressed(in, VKMIN_KEY_ESCAPE)) { vkmin_frame_end(gpu); break; }

        /* --- input to camera and orders -------------------------------- */
        const float pan = 3.0f * g->cam_distance / 260.0f;
        if (vkmin_key_down(in, 'W') || vkmin_key_down(in, VKMIN_KEY_UP)) g->cam_target.z -= pan;
        if (vkmin_key_down(in, 'S') || vkmin_key_down(in, VKMIN_KEY_DOWN)) g->cam_target.z += pan;
        if (vkmin_key_down(in, 'A') || vkmin_key_down(in, VKMIN_KEY_LEFT)) g->cam_target.x -= pan;
        if (vkmin_key_down(in, 'D') || vkmin_key_down(in, VKMIN_KEY_RIGHT)) g->cam_target.x += pan;
        g->cam_distance = fminf(fmaxf(g->cam_distance * (1.0f - 0.1f * in->wheel), 60.0f), 900.0f); /* wheel up: closer */
        const vec3 target = {g->cam_target.x, terrain_height(g->cam_target.x, g->cam_target.z), g->cam_target.z};
        const vkmin_camera cam = vkmin_camera_rts(target, g->cam_distance, 0.9f, 0.0f, 0.8f, (float)width / (float)height, 2.0f, 3000.0f);

        if ((in->buttons_pressed & VKMIN_MOUSE_LEFT) && !g->dragging) { g->dragging = true; g->drag_x0 = in->mouse_x; g->drag_y0 = in->mouse_y; }
        if (g->dragging && !(in->buttons & VKMIN_MOUSE_LEFT)) {
            /* Release: select every live unit whose projected position is in the box. */
            g->dragging = false;
            const float x0 = fminf(g->drag_x0, in->mouse_x), x1 = fmaxf(g->drag_x0, in->mouse_x);
            const float y0 = fminf(g->drag_y0, in->mouse_y), y1 = fmaxf(g->drag_y0, in->mouse_y);
            const mat4 vp = vkmin_mat4_mul(cam.proj, cam.view);
            for (uint32_t i = 0; i < UNITS; ++i) {
                const pos2 p = unit_position(&g->units[i], i, g->tick);
                const vec4 clip = vkmin_mat4_mul_vec4(vp, (vec4){p.x, terrain_height(p.x, p.z) + 2.0f, p.z, 1.0f});
                const float sx = (clip.x / clip.w * 0.5f + 0.5f) * (float)width, sy = (clip.y / clip.w * 0.5f + 0.5f) * (float)height;
                g->units[i].selected = g->units[i].hp > 0.0f && clip.w > 0.0f && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
            }
        }
        if (in->buttons_pressed & VKMIN_MOUSE_RIGHT) {
            pos2 hit;
            if (terrain_hit(vkmin_ray_from_pixel(cam.view, cam.proj, in->mouse_x, in->mouse_y, (float)width, (float)height), &hit)) {
                for (uint32_t i = 0; i < UNITS; ++i) {
                    if (g->units[i].selected) g->units[i].target = (pos2){hit.x + (gk_hash(3, i) - 0.5f) * 60.0f, hit.z + (gk_hash(4, i) - 0.5f) * 60.0f};
                }
            }
        }

        /* --- simulate ---------------------------------------------------- */
        const uint32_t due = gk_ticks_due(frame, HZ, &ticks_done);
        for (uint32_t k = 0; k < due; ++k) game_tick(g);

        /* --- instances, lights, quads ------------------------------------ */
        uint32_t n = hs.meshes, nq = 0;
        g->selected_count = 0;
        g->dead_count = 0;
        for (uint32_t i = 0; i < UNITS; ++i) {
            const unit *u = &g->units[i];
            const pos2 p = unit_position(u, i, g->tick);
            const float y = terrain_height(p.x, p.z);
            const bool dead = u->hp <= 0.0f;
            const float s = dead ? 1.2f : 1.5f;
            const mat4 t = vkmin_mat4_mul(vkmin_mat4_translate((vec3){p.x, y + (dead ? 0.3f : 1.8f), p.z}),
                                          vkmin_mat4_mul(vkmin_mat4_rotate_y(6.2831853f * gk_hash(5, i)), vkmin_mat4_scale((vec3){s, dead ? 0.3f : 1.8f, s})));
            const uint32_t mat = dead ? MAT_DEAD : g->hovered == i + 1u ? MAT_HOVER : u->team ? MAT_RED : MAT_BLUE;
            instances[n++] = (Instance){.transform = t, .prev_transform = t, .bounds = gk_world_bounds(t, (vec4){0, 0, 0, 1.7321f}),
                                        .mesh = shapes.cube, .material = mat0 + mat, .bone_offset = VKMIN_NONE, .id = i + 1u};
            g->dead_count += dead;
            if (u->selected && !dead && nq + 3 <= MAX_QUADS) {
                /* health bar: dark back, coloured front, and a marker on the ground */
                g->selected_count++;
                const vec4 above = {p.x, y + 4.6f, p.z, 0.0f};
                quads[nq++] = (Quad){.pos = above, .size_uv0 = {6.4f, 1.2f, 0, 0}, .uv1 = {1, 1, 0, 0}, .color = 0xff202020u, .flags = VKMIN_QUAD_BILLBOARD};
                quads[nq++] = (Quad){.pos = {p.x - 3.0f * (1.0f - u->hp), y + 4.6f, p.z, 0.0f}, .size_uv0 = {6.0f * u->hp, 0.8f, 0, 0}, .uv1 = {1, 1, 0, 0},
                                     .color = u->hp > 0.5f ? 0xff40e040u : 0xff40c0f0u, .flags = VKMIN_QUAD_BILLBOARD};
                quads[nq++] = (Quad){.pos = {p.x, y + 0.2f, p.z, 0.0f}, .size_uv0 = {5.0f, 5.0f, 0, 0}, .uv1 = {1, 1, 0, 0}, .color = 0x60ffffffu, .flags = VKMIN_QUAD_GROUND};
            }
        }
        if (g->dragging) {
            /* the selection rectangle as four thin screen quads */
            const float x0 = fminf(g->drag_x0, in->mouse_x), x1 = fmaxf(g->drag_x0, in->mouse_x);
            const float y0 = fminf(g->drag_y0, in->mouse_y), y1 = fmaxf(g->drag_y0, in->mouse_y);
            const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, w = x1 - x0 + 2.0f, h = y1 - y0 + 2.0f;
            const Quad edge = {.uv1 = {1, 1, 0, 0}, .color = 0xff80ff80u, .flags = VKMIN_QUAD_SCREEN};
            Quad e[4] = {edge, edge, edge, edge};
            e[0].pos = (vec4){cx, y0, 0, 0}; e[0].size_uv0 = (vec4){w, 1, 0, 0};
            e[1].pos = (vec4){cx, y1, 0, 0}; e[1].size_uv0 = (vec4){w, 1, 0, 0};
            e[2].pos = (vec4){x0, cy, 0, 0}; e[2].size_uv0 = (vec4){1, h, 0, 0};
            e[3].pos = (vec4){x1, cy, 0, 0}; e[3].size_uv0 = (vec4){1, h, 0, 0};
            for (int k = 0; k < 4 && nq < MAX_QUADS; ++k) quads[nq++] = e[k];
        }
        snprintf(hud, sizeof hud, "units %u  fallen %u  selected %u  hover %u", UNITS, g->dead_count, g->selected_count, g->hovered);
        nq += vkr_text(r, hud, 6.0f, (float)height - 18.0f, 12.0f, 0xffffffffu, quads + nq, MAX_QUADS - nq);

        const Light sun = gk_sun((vec3){0.4f, -1.0f, 0.3f}, 4.0f);
        vkr_frame(r, &(vkr_frame_desc){.view = cam.view, .proj = cam.proj, .camera_pos = {cam.pos.x, cam.pos.y, cam.pos.z, 1},
                                       .near = 2.0f, .far = 3000.0f, .instances = instances, .instance_count = n, .lights = &sun, .light_count = 1,
                                       .quads = quads, .quad_count = nq, .frame = fr,
                                       .look = {.fog = {0.55f, 0.65f, 0.80f}, .fog_density = 0.0006f}});
        vkmin_frame_end(gpu);
        /* Picking reads the frame just finished, so the highlight is one frame late. */
        g->hovered = vkmin_pick(gpu, vkr_id_target(r), (int)in->mouse_x, (int)in->mouse_y);
    }
    const vkr_stats st = vkr_get_stats(r);
    if (cvar_get_bool(CV_d_check_cull)) printf("11_rts: cull check: %u mismatches\n", st.cull_mismatches);
    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    free(g); free(instances); free(quads);
    return st.cull_mismatches ? 1 : 0;
}
