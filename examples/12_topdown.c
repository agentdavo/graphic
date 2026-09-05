/* 12_topdown -- an orthographic camera straight down onto a heightfield floor
 * drawn as tiles, sprites for the characters mixed with 3D props, a point
 * light that follows the player, a ground cursor from a pixel ray, and
 * picking through the ID target: click a prop and it lights up.
 *
 *   WASD moves, the mouse points, left click picks. */
#include "gamekit.h"

enum { FLOOR_N = 65, FLOOR_CHUNK = 16, PROPS = 40, ENEMIES = 24, HZ = 30, MAX_QUADS = 512 };
#define FLOOR_SIZE ((float)(FLOOR_N - 1))

static float floor_height(float x, float z) { return 0.6f * sinf(x * 0.35f) * cosf(z * 0.27f) + 0.3f * sinf(z * 0.9f); }

typedef struct {
    float px, pz;        /* player */
    uint32_t tick;
    uint32_t picked;     /* prop id under the last click, 0 = none */
    uint32_t hovered;
} game;

/* A character sprite: a filled disc with a dark rim and an eye-dot, so its
 * facing reads at a glance. Colour comes from the quad tint. */
static uint32_t sprite_texture(vkmin_ctx *gpu) {
    enum { S = 32 };
    uint32_t px[S * S];
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const float dx = ((float)x + 0.5f) / S - 0.5f, dy = ((float)y + 0.5f) / S - 0.5f;
            const float d = sqrtf(dx * dx + dy * dy) * 2.0f;
            const float ex = dx - 0.18f, ey = dy - 0.05f;
            const bool eye = ex * ex + ey * ey < 0.012f;
            px[y * S + x] = d > 0.98f ? 0u : d > 0.82f ? gk_rgba(0.1f, 0.1f, 0.12f, 1) : eye ? gk_rgba(0.05f, 0.05f, 0.05f, 1) : gk_rgba(1, 1, 1, 1);
        }
    }
    return gk_texture(gpu, S, px, VKMIN_SAMPLER_LINEAR_CLAMP, "topdown.sprite");
}

static const char *usage = "usage: 12_topdown [--profile lavapipe] [vkmin flags] [cvar=value ...]\n  WASD moves, left click picks a prop\n";

int main(int argc, char **argv) {
    const gk_options opt = gk_parse(argc, argv, usage);
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "12_topdown", .device_index = opt.device});
    int width = 0, height = 0;
    vkmin_size(gpu, &width, &height);
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = width, .height = height, .shadow_atlas = cvar_get_int(CV_r_shadow_atlas),
                                       .max_vertices = 16384, .max_indices = 65536, .max_meshes = 32, .max_materials = 8,
                                       .max_instances = PROPS + 32});

    /* The floor: a flat-ish heightfield in chunks, with a tile texture. */
    float heights[FLOOR_N * FLOOR_N];
    for (int z = 0; z < FLOOR_N; ++z) {
        for (int x = 0; x < FLOOR_N; ++x) heights[z * FLOOR_N + x] = floor_height((float)x, (float)z);
    }
    const vkmin_heightfield_desc hd = {.heights = heights, .width = FLOOR_N, .height = FLOOR_N, .cell = 1.0f, .chunk = FLOOR_CHUNK, .uv_per_unit = 0.5f};
    const vkmin_heightfield_size hs = vkmin_heightfield_sizes(&hd);
    Vertex *fv = malloc(hs.vertices * sizeof(Vertex));
    uint32_t *fi = malloc(hs.indices * sizeof(uint32_t));
    Mesh *fm = malloc(hs.meshes * sizeof(Mesh));
    if (!fv || !fi || !fm) gk_die("out of memory");
    vkmin_heightfield(&hd, fv, fi, fm);
    const uint32_t floor_mesh0 = vkr_upload_geometry(r, &(vkr_geometry){.vertices = fv, .vertex_count = hs.vertices, .indices = fi,
                                                                        .index_count = hs.indices, .meshes = fm, .mesh_count = hs.meshes});
    const gk_shapes shapes = gk_upload_shapes(r);
    const uint32_t tiles = gk_checker_texture(gpu, 64, 2, gk_rgba(0.55f, 0.50f, 0.42f, 1), gk_rgba(0.42f, 0.38f, 0.33f, 1));
    const uint32_t sprite = sprite_texture(gpu);
    Material mats[4] = {gk_material(1, 1, 1, 0.0f, 0.9f, 0), gk_material(0.35f, 0.55f, 0.75f, 0.0f, 0.6f, 0),
                        gk_material(0.75f, 0.55f, 0.25f, 0.3f, 0.4f, 0), gk_material(1.0f, 0.95f, 0.3f, 0.0f, 0.3f, VKMIN_MAT_UNLIT)};
    mats[0].albedo_tex = tiles;
    const uint32_t mat0 = vkr_upload_materials(r, mats, 4);
    enum { MAT_FLOOR, MAT_BLUE, MAT_WOOD, MAT_PICKED };

    Instance *instances = calloc(PROPS + hs.meshes, sizeof(Instance));
    Quad quads[MAX_QUADS];
    if (!instances) gk_die("out of memory");
    for (uint32_t m = 0; m < hs.meshes; ++m) {
        const mat4 id = vkmin_mat4_identity();
        instances[m] = (Instance){.transform = id, .prev_transform = id, .bounds = fm[m].bounds, .mesh = floor_mesh0 + m,
                                  .material = mat0 + MAT_FLOOR, .bone_offset = VKMIN_NONE};
    }
    free(fv); free(fi); free(fm);

    game g = {.px = FLOOR_SIZE * 0.5f, .pz = FLOOR_SIZE * 0.5f};
    uint32_t ticks_done = 0;
    Light lights[2];
    char hud[128];
    while (vkmin_running(gpu)) {
        const vkmin_frame fr = vkmin_frame_begin(gpu, NULL); /* the one read of the outside world */
        const vkmin_inputs *in = &fr.input;
        const uint32_t frame = fr.index;
        width = fr.width;
        height = fr.height;
        if (vkmin_key_pressed(in, VKMIN_KEY_ESCAPE)) { vkmin_frame_end(gpu); break; }

        /* --- simulate: the player walks, the enemies are functions of the tick --- */
        const uint32_t due = gk_ticks_due(frame, HZ, &ticks_done);
        for (uint32_t k = 0; k < due; ++k) {
            const float speed = 0.22f;
            g.px += (vkmin_key_down(in, 'D') ? speed : 0.0f) - (vkmin_key_down(in, 'A') ? speed : 0.0f);
            g.pz += (vkmin_key_down(in, 'S') ? speed : 0.0f) - (vkmin_key_down(in, 'W') ? speed : 0.0f);
            g.px = fminf(fmaxf(g.px, 1.0f), FLOOR_SIZE - 1.0f);
            g.pz = fminf(fmaxf(g.pz, 1.0f), FLOOR_SIZE - 1.0f);
            g.tick++;
        }
        const float py = floor_height(g.px, g.pz);
        const vkmin_camera cam = vkmin_camera_ortho_topdown((vec3){g.px, py, g.pz}, 9.0f, (float)width / (float)height, 40.0f);

        /* The ground cursor: the pixel ray meets the floor's mean plane. */
        const vkmin_ray ray = vkmin_ray_from_pixel(cam.view, cam.proj, in->mouse_x, in->mouse_y, (float)width, (float)height);
        const float t_ground = ray.dir.y != 0.0f ? (py - ray.origin.y) / ray.dir.y : 0.0f;
        const vec3 cursor = vkmin_vec3_add(ray.origin, vkmin_vec3_scale(ray.dir, t_ground));

        /* --- props: the same forty every frame, one lit up when picked ------ */
        uint32_t n = hs.meshes, nq = 0;
        for (uint32_t i = 0; i < PROPS; ++i) {
            const float x = 2.0f + gk_hash(21, i) * (FLOOR_SIZE - 4.0f), z = 2.0f + gk_hash(22, i) * (FLOOR_SIZE - 4.0f);
            const bool cube = (i % 3) != 0;
            const float s = 0.35f + 0.35f * gk_hash(23, i);
            const mat4 t = vkmin_mat4_mul(vkmin_mat4_translate((vec3){x, floor_height(x, z) + s, z}),
                                          vkmin_mat4_mul(vkmin_mat4_rotate_y(gk_hash(24, i) * 6.2831853f), vkmin_mat4_scale((vec3){s, s, s})));
            const uint32_t mat = g.picked == i + 1u ? MAT_PICKED : cube ? MAT_WOOD : MAT_BLUE;
            instances[n++] = (Instance){.transform = t, .prev_transform = t, .bounds = gk_world_bounds(t, (vec4){0, 0, 0, cube ? 1.7321f : 1.0f}),
                                        .mesh = cube ? shapes.cube : shapes.sphere, .material = mat0 + mat, .bone_offset = VKMIN_NONE, .id = i + 1u};
        }
        /* --- sprites: the player and the enemies, as billboards ------------- */
        quads[nq++] = (Quad){.pos = {cursor.x, py + 0.05f, cursor.z, (float)frame * 0.05f}, .size_uv0 = {1.2f, 1.2f, 0, 0}, .uv1 = {1, 1, 0, 0},
                             .color = 0x80ffffffu, .texture = sprite, .flags = VKMIN_QUAD_GROUND};
        quads[nq++] = (Quad){.pos = {g.px, py + 0.9f, g.pz, 0.0f}, .size_uv0 = {1.4f, 1.4f, 0, 0}, .uv1 = {1, 1, 0, 0},
                             .color = 0xff60d080u, .texture = sprite, .flags = VKMIN_QUAD_BILLBOARD};
        for (uint32_t i = 0; i < ENEMIES; ++i) {
            const float a = (float)g.tick / HZ * (0.3f + 0.4f * gk_hash(31, i)) + 6.2831853f * gk_hash(32, i);
            const float cx = 6.0f + gk_hash(33, i) * (FLOOR_SIZE - 12.0f), cz = 6.0f + gk_hash(34, i) * (FLOOR_SIZE - 12.0f);
            const float x = cx + 4.0f * cosf(a), z = cz + 4.0f * sinf(a);
            quads[nq++] = (Quad){.pos = {x, floor_height(x, z) + 0.8f, z, 0.0f}, .size_uv0 = {1.2f, 1.2f, 0, 0}, .uv1 = {1, 1, 0, 0},
                                 .color = 0xff5050f0u, .texture = sprite, .flags = VKMIN_QUAD_BILLBOARD};
        }
        snprintf(hud, sizeof hud, "picked %u  under cursor %u", g.picked, g.hovered);
        nq += vkr_text(r, hud, 6.0f, 6.0f, 12.0f, 0xffffffffu, quads + nq, MAX_QUADS - nq);

        lights[0] = gk_sun((vec3){0.2f, -1.0f, 0.1f}, 0.6f);
        lights[1] = gk_point_light((vec3){g.px, py + 2.5f, g.pz}, 9.0f, (vec3){1.0f, 0.85f, 0.6f}, 14.0f);
        vkr_frame(r, &(vkr_frame_desc){.view = cam.view, .proj = cam.proj, .camera_pos = {cam.pos.x, cam.pos.y, cam.pos.z, 1},
                                       .near = 0.1f, .far = 80.0f, .instances = instances, .instance_count = n, .lights = lights, .light_count = 2,
                                       .quads = quads, .quad_count = nq, .frame = fr});
        vkmin_frame_end(gpu);
        g.hovered = vkmin_pick(gpu, vkr_id_target(r), (int)in->mouse_x, (int)in->mouse_y);
        if (in->buttons_pressed & VKMIN_MOUSE_LEFT) g.picked = g.hovered;
    }
    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    free(instances);
    return 0;
}
