/* cube.c -- the vkmin demo: a spinning textured cube, headless or windowed.
 *
 * Animation is a pure function of an integer frame index. Nothing here reads
 * the wall clock or calls rand(), so `--frame 60` renders exactly the same
 * pixels every run and a visual bug is bisectable instead of a ghost.
 */
#include "vkmin.h"
#include "mat4.h"
#include "shaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define FRAMES_PER_TURN 240 /* fixed timestep: the whole clock, in one place */

typedef enum { SCENE_CLEAR = 0, SCENE_TRI, SCENE_CUBE, SCENE_TEX } scene_kind;

typedef struct {
    float pos[3];
    float color[3];
    float uv[2];
} vertex;
_Static_assert(sizeof(vertex) == 32, "vertex layout must match the pipeline's attribute offsets");

enum { CUBE_VERTS = 24, CUBE_INDICES = 36, MAX_FRAMES = 32 };

typedef struct {
    scene_kind scene;
    bool headless;
    bool sync_naive;
    int width, height;
    int device_index;
    int frames[MAX_FRAMES];
    int frame_count;
    const char *out;
    const char *out_dir;
    int exit_after; /* windowed: stop after this many frames, so the swapchain
                     * path is testable under a virtual display instead of only
                     * believed to work */
    const char *texture;
} options;

/* ----------------------------------------------------------- geometry ---- */

/* Each face is an origin plus two edge vectors, chosen so that dv x du is the
 * outward normal. Emitting all six from one loop means a face cannot end up
 * wound differently from its neighbours by a copy-paste slip. */
typedef struct {
    vec3 origin, du, dv;
    float color[3];
} face;

static uint32_t build_cube(vertex *verts, uint16_t *indices) {
    static const face faces[6] = {
        {{-1, 1, 1}, {2, 0, 0}, {0, -2, 0}, {1.00f, 1.00f, 1.00f}},   /* +Z */
        {{1, 1, -1}, {-2, 0, 0}, {0, -2, 0}, {0.75f, 0.50f, 1.00f}},  /* -Z */
        {{1, 1, 1}, {0, 0, -2}, {0, -2, 0}, {1.00f, 0.45f, 0.45f}},   /* +X */
        {{-1, 1, -1}, {0, 0, 2}, {0, -2, 0}, {0.45f, 1.00f, 0.55f}},  /* -X */
        {{-1, 1, -1}, {2, 0, 0}, {0, 0, 2}, {0.50f, 0.60f, 1.00f}},   /* +Y */
        {{-1, -1, 1}, {2, 0, 0}, {0, 0, -2}, {1.00f, 0.90f, 0.40f}},  /* -Y */
    };
    /* Counter-clockwise seen from outside the cube: bottom-left, bottom-right,
     * top-right, top-left in the face's own (u, v) frame. */
    static const float quad_uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

    uint32_t v = 0, i = 0;
    for (int f = 0; f < 6; ++f) {
        const uint16_t base = (uint16_t)v;
        for (int corner = 0; corner < 4; ++corner) {
            const float u = quad_uv[corner][0], w = quad_uv[corner][1];
            verts[v].pos[0] = faces[f].origin.x + faces[f].du.x * u + faces[f].dv.x * w;
            verts[v].pos[1] = faces[f].origin.y + faces[f].du.y * u + faces[f].dv.y * w;
            verts[v].pos[2] = faces[f].origin.z + faces[f].du.z * u + faces[f].dv.z * w;
            for (int k = 0; k < 3; ++k) verts[v].color[k] = faces[f].color[k];
            verts[v].uv[0] = u;
            verts[v].uv[1] = w;
            ++v;
        }
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; ++k) indices[i++] = (uint16_t)(base + order[k]);
    }
    return i;
}

/* The entire animation: frame index in, transform out. */
static mat4 cube_mvp(int frame, float aspect) {
    const float angle = (float)frame * (2.0f * PI / (float)FRAMES_PER_TURN);
    const mat4 model = mat4_mul(mat4_rotate_y(angle), mat4_rotate_x(angle * 0.5f));
    const mat4 view =
        mat4_look_at((vec3){0.0f, 0.0f, 4.2f}, (vec3){0.0f, 0.0f, 0.0f}, (vec3){0.0f, 1.0f, 0.0f});
    const mat4 proj = mat4_perspective(50.0f * PI / 180.0f, aspect, 0.1f, 20.0f);
    return mat4_mul(proj, mat4_mul(view, model));
}

/* ------------------------------------------------------------ the app ---- */

typedef struct {
    scene_kind scene;
    vkmin_pipe pipe;
    vkmin_buffer vbuf, ibuf;
    vkmin_image texture;
    uint32_t index_count;
} app;

static app app_make(vkmin_ctx *c, const options *opt) {
    app a = {.scene = opt->scene};
    if (opt->scene == SCENE_CLEAR) return a;

    if (opt->scene == SCENE_TRI) {
        a.pipe = vkmin_make_pipeline(
            c, &(vkmin_pipe_desc){
                   .vs = vkmin_make_shader(c, tri_vert_spv, sizeof tri_vert_spv),
                   .fs = vkmin_make_shader(c, tri_frag_spv, sizeof tri_frag_spv),
                   .cull_off = true, /* a single triangle has no inside to hide */
                   .label = "pipe.triangle"});
        a.index_count = 3;
        return a;
    }

    vertex verts[CUBE_VERTS];
    uint16_t indices[CUBE_INDICES];
    a.index_count = build_cube(verts, indices);
    a.vbuf = vkmin_make_buffer(c, &(vkmin_buffer_desc){.type = VKMIN_BUFFER_VERTEX,
                                                       .data = verts,
                                                       .size = sizeof verts,
                                                       .label = "cube.vertices"});
    a.ibuf = vkmin_make_buffer(c, &(vkmin_buffer_desc){.type = VKMIN_BUFFER_INDEX,
                                                       .data = indices,
                                                       .size = sizeof indices,
                                                       .label = "cube.indices"});
    const bool textured = opt->scene == SCENE_TEX;
    if (textured) a.texture = vkmin_load_png(c, opt->texture);
    a.pipe = vkmin_make_pipeline(
        c, &(vkmin_pipe_desc){
               .vs = vkmin_make_shader(c, cube_vert_spv, sizeof cube_vert_spv),
               .fs = textured ? vkmin_make_shader(c, cube_tex_frag_spv, sizeof cube_tex_frag_spv)
                              : vkmin_make_shader(c, cube_flat_frag_spv, sizeof cube_flat_frag_spv),
               .attrs = {{VKMIN_ATTR_FLOAT3, offsetof(vertex, pos)},
                         {VKMIN_ATTR_FLOAT3, offsetof(vertex, color)},
                         {VKMIN_ATTR_FLOAT2, offsetof(vertex, uv)}},
               .vertex_stride = sizeof(vertex),
               .depth_test = true,
               .label = textured ? "pipe.cube.textured" : "pipe.cube.flat"});
    return a;
}

static void render_frame(vkmin_ctx *c, const app *a, int frame) {
    int width = 0, height = 0;
    vkmin_size(c, &width, &height); /* not the requested size: the window may have been resized */
    const vkmin_clear clear = a->scene == SCENE_CLEAR
                                  ? (vkmin_clear){1.0f, 0.0f, 1.0f, 1.0f}
                                  : (vkmin_clear){0.06f, 0.07f, 0.09f, 1.0f};
    vkmin_frame_begin(c, clear);
    if (a->scene != SCENE_CLEAR) {
        vkmin_bind(c, &(vkmin_bindings){.pipe = a->pipe,
                                        .vbuf = a->vbuf,
                                        .ibuf = a->ibuf,
                                        .texture = a->texture});
        if (a->scene != SCENE_TRI) {
            const mat4 mvp = cube_mvp(frame, (float)width / (float)height);
            vkmin_push(c, mvp.m, sizeof mvp.m);
        }
        vkmin_draw(c, a->index_count, 1);
    }
    vkmin_frame_end(c);
}

/* ------------------------------------------------------- command line ---- */

static scene_kind parse_scene(const char *name) {
    if (strcmp(name, "clear") == 0) return SCENE_CLEAR;
    if (strcmp(name, "tri") == 0) return SCENE_TRI;
    if (strcmp(name, "cube") == 0) return SCENE_CUBE;
    if (strcmp(name, "tex") == 0) return SCENE_TEX;
    fprintf(stderr, "cube: unknown scene '%s' (clear|tri|cube|tex)\n", name);
    exit(2);
}

static void parse_frame_list(options *opt, const char *list) {
    opt->frame_count = 0;
    const char *p = list;
    while (*p) {
        char *end = NULL;
        const long value = strtol(p, &end, 10);
        if (end == p) break;
        if (opt->frame_count == MAX_FRAMES) {
            fprintf(stderr, "cube: at most %d frames per run\n", MAX_FRAMES);
            exit(2);
        }
        opt->frames[opt->frame_count++] = (int)value;
        p = (*end == ',') ? end + 1 : end;
    }
    if (opt->frame_count == 0) {
        fprintf(stderr, "cube: --frames needs at least one frame number\n");
        exit(2);
    }
}

static const char *usage_text =
    "usage: cube [options]\n"
    "  --headless            render offscreen, no window\n"
    "  --frame N             render exactly frame N and exit (implies --headless)\n"
    "  --frames a,b,c        render each of these frames (implies --headless)\n"
    "  --out PATH            PNG path for a single frame\n"
    "  --out-dir DIR         PNG directory for a frame list\n"
    "  --scene NAME          clear | tri | cube | tex   (default tex)\n"
    "  --size W H            render size (default 256x256 headless, 800x600 windowed)\n"
    "  --texture PATH        texture PNG (default tests/assets/grid.png)\n"
    "  --sync-naive          reference sync path: one frame in flight + wait idle\n"
    "  --exit-after N        windowed: quit after N frames (and save --out, if given)\n"
    "  --device N            physical device index\n"
    "  windowed: F12 saves a numbered PNG, Escape quits\n";

int main(int argc, char **argv) {
    options opt = {.scene = SCENE_TEX,
                   .width = 0,
                   .height = 0,
                   .texture = "tests/assets/grid.png",
                   .out = NULL};
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const bool has_next = i + 1 < argc;
        if (strcmp(arg, "--headless") == 0) {
            opt.headless = true;
        } else if (strcmp(arg, "--sync-naive") == 0) {
            opt.sync_naive = true;
        } else if (strcmp(arg, "--frame") == 0 && has_next) {
            opt.headless = true;
            opt.frame_count = 1;
            opt.frames[0] = atoi(argv[++i]);
        } else if (strcmp(arg, "--frames") == 0 && has_next) {
            opt.headless = true;
            parse_frame_list(&opt, argv[++i]);
        } else if (strcmp(arg, "--out") == 0 && has_next) {
            opt.out = argv[++i];
        } else if (strcmp(arg, "--out-dir") == 0 && has_next) {
            opt.out_dir = argv[++i];
        } else if (strcmp(arg, "--scene") == 0 && has_next) {
            opt.scene = parse_scene(argv[++i]);
        } else if (strcmp(arg, "--texture") == 0 && has_next) {
            opt.texture = argv[++i];
        } else if (strcmp(arg, "--exit-after") == 0 && has_next) {
            opt.exit_after = atoi(argv[++i]);
        } else if (strcmp(arg, "--device") == 0 && has_next) {
            opt.device_index = atoi(argv[++i]);
        } else if (strcmp(arg, "--size") == 0 && i + 2 < argc) {
            opt.width = atoi(argv[++i]);
            opt.height = atoi(argv[++i]);
        } else {
            fprintf(stderr, "%s", usage_text);
            return strcmp(arg, "--help") == 0 ? 0 : 2;
        }
    }
    if (opt.width <= 0 || opt.height <= 0) {
        opt.width = opt.headless ? 256 : 800;
        opt.height = opt.headless ? 256 : 600;
    }
    if (opt.headless && opt.frame_count == 0) {
        opt.frame_count = 1;
        opt.frames[0] = 0;
    }

    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.headless = opt.headless,
                                            .sync_naive = opt.sync_naive,
                                            .width = opt.width,
                                            .height = opt.height,
                                            .device_index = opt.device_index,
                                            .title = "vkmin cube"});
    const app a = app_make(c, &opt);
    int status = 0;

    if (opt.headless) {
        static const char *scene_names[4] = {"clear", "tri", "cube", "tex"};
        for (int i = 0; i < opt.frame_count; ++i) {
            const int frame = opt.frames[i];
            render_frame(c, &a, frame);
            char path[512];
            if (opt.out && opt.frame_count == 1) {
                snprintf(path, sizeof path, "%s", opt.out);
            } else {
                snprintf(path, sizeof path, "%s/%s_%04d.png",
                         opt.out_dir ? opt.out_dir : ".", scene_names[opt.scene], frame);
            }
            if (!vkmin_save_png(c, path)) status = 1;
            else printf("wrote %s\n", path);
        }
    } else {
        int frame = 0, shot = 0;
        while (!vkmin_should_close(c)) {
            if (vkmin_key_hit(c, VKMIN_KEY_ESCAPE)) break;
            render_frame(c, &a, frame);
            if (vkmin_key_hit(c, VKMIN_KEY_F12)) {
                char path[512];
                snprintf(path, sizeof path, "shot_%04d.png", shot++);
                if (vkmin_save_png(c, path)) printf("wrote %s\n", path);
            }
            ++frame;
            if (opt.exit_after > 0 && frame >= opt.exit_after) break;
        }
        if (opt.out && frame > 0 && !vkmin_save_png(c, opt.out)) status = 1;
    }

    vkmin_shutdown(c);
    return status;
}
