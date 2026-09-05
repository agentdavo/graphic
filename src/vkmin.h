/* vkmin.h -- a small Vulkan 1.3/1.4 layer in C11. This header is the documentation.
 *
 * Shape: one context, passed first to everything. Resources are typed 32-bit
 * handles (zero invalid). A shader reaches a texture by its bindless index and
 * a buffer by its device address; both travel in the push block you define in
 * shared.h, so there is no binding model: a draw is a pipeline, a push block
 * and a count. The push block's size belongs to the pipeline and is checked
 * against the SPIR-V at creation. A pointer never travels without its size
 * (vkmin_bytes), and a frame reads the outside world at exactly one point:
 * the vkmin_frame that frame_begin returns. Desc structs configure everything
 * and a zero field is always the sensible default. The library never calls
 * user code, never allocates after init, and reads no clock: frame N is the
 * same pixels every run.
 *
 * Every function carries a state contract in its trailing comment:
 *   pure        reads only its arguments
 *   reads ctx   inspects the context, changes nothing
 *   writes ctx  changes host-side context state
 *   gpu         records GPU work or touches device memory
 *   io          files, the window, stderr
 *
 * Single-threaded: all calls on one thread, in order, no exceptions.
 * Failure is fatal: VKMIN_FAIL prints file, line and reason, then aborts.
 */
#ifndef VKMIN_H
#define VKMIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "shared.h"

/* ---- compile-time configuration; every value has a default ------------- */
#ifndef VKMIN_MAX_BUFFERS
#define VKMIN_MAX_BUFFERS 64
#endif
#ifndef VKMIN_MAX_IMAGES
#define VKMIN_MAX_IMAGES 256
#endif
#ifndef VKMIN_MAX_PIPES
#define VKMIN_MAX_PIPES 32
#endif
#ifndef VKMIN_FAIL /* (fmt, ...): report and abort; redefine before including to redirect */
#define VKMIN_FAIL(...) vkmin_fail(__FILE__, __LINE__, __VA_ARGS__)
#endif
#ifndef VKMIN_ASSERT /* (cond, fmt, ...) */
#define VKMIN_ASSERT(cond, ...) do { if (!(cond)) VKMIN_FAIL(__VA_ARGS__); } while (0)
#endif
/* #define VKMIN_NO_PLATFORM before building vkmin.c to drop the window backend (headless only). */
enum { VKMIN_MAX_TIMESTAMPS = 32, VKMIN_PUSH_BYTES = 128 };

typedef struct vkmin_ctx vkmin_ctx;
typedef struct { uint32_t id; } vkmin_buffer;
typedef struct { uint32_t id; } vkmin_image;
typedef struct { uint32_t id; } vkmin_pipeline;
#define vkmin_valid(h) ((h).id != 0u) /* any handle type */
typedef struct { const void *data; size_t size; } vkmin_bytes; /* a pointer and its size, never apart */
#define VKMIN_BYTES(x) ((vkmin_bytes){(x), sizeof(x)})         /* of an array or object, not a pointer */

typedef enum { VKMIN_PATH_AUTO = 0, VKMIN_PATH_LEGACY, VKMIN_PATH_MODERN } vkmin_path;
typedef enum { VKMIN_FMT_RGBA8_UNORM = 0, VKMIN_FMT_RGBA8_SRGB, VKMIN_FMT_BGRA8_UNORM, VKMIN_FMT_BC1_SRGB,
               VKMIN_FMT_BC1_UNORM, VKMIN_FMT_BC3_SRGB, VKMIN_FMT_BC4_UNORM, VKMIN_FMT_BC5_UNORM,
               VKMIN_FMT_R11G11B10_FLOAT, VKMIN_FMT_RGBA16_FLOAT, VKMIN_FMT_D32_FLOAT, VKMIN_FMT_R32_UINT,
               VKMIN_FMT_RG16_UNORM, VKMIN_FMT_NONE,
               VKMIN_FMT_COUNT } vkmin_format;
typedef enum { VKMIN_USE_UNDEFINED = 0, VKMIN_USE_TRANSFER_DST, VKMIN_USE_TRANSFER_SRC, VKMIN_USE_SAMPLED,
               VKMIN_USE_COLOR_TARGET, VKMIN_USE_DEPTH_TARGET, VKMIN_USE_PRESENT } vkmin_use;
typedef enum { VKMIN_IMAGE_SAMPLED = 1, VKMIN_IMAGE_COLOR = 2, VKMIN_IMAGE_DEPTH = 4,
               VKMIN_IMAGE_READBACK = 8 /* the host may read it: vkmin_pick */ } vkmin_image_usage;
typedef enum { VKMIN_CMP_LESS = 0, VKMIN_CMP_LESS_EQUAL, VKMIN_CMP_EQUAL, VKMIN_CMP_ALWAYS } vkmin_compare;
typedef enum { VKMIN_CULL_BACK = 0, VKMIN_CULL_NONE, VKMIN_CULL_FRONT } vkmin_cull;
enum { VKMIN_KEY_SPACE = 32, VKMIN_KEY_ESCAPE = 256, VKMIN_KEY_ENTER, VKMIN_KEY_TAB, VKMIN_KEY_RIGHT = 262, VKMIN_KEY_LEFT,
       VKMIN_KEY_DOWN, VKMIN_KEY_UP, VKMIN_KEY_F1 = 290, VKMIN_KEY_F12 = 301, VKMIN_KEY_SHIFT = 340, VKMIN_KEY_CTRL,
       VKMIN_KEY_COUNT = 352 }; /* letters and digits are their ASCII uppercase; the codes are GLFW's */
enum { VKMIN_MOUSE_LEFT = 1, VKMIN_MOUSE_RIGHT = 2, VKMIN_MOUSE_MIDDLE = 4 };

typedef struct {                  /* one snapshot per frame: taken in vkmin_frame_begin, journalled, never polled */
    uint32_t down[VKMIN_KEY_COUNT / 32], pressed[VKMIN_KEY_COUNT / 32];  /* bit per key: held / went down this frame */
    float mouse_x, mouse_y;       /* pixels from the top left */
    float wheel;                  /* scroll steps this frame */
    uint32_t buttons, buttons_pressed;                    /* VKMIN_MOUSE_* bits */
    float axes[6];                /* gamepad 0: left x y, right x y, triggers; -1..1 */
    uint32_t pad_buttons;         /* gamepad 0 buttons, bit per GLFW_GAMEPAD_BUTTON_*; 0 when absent */
} vkmin_inputs;
#define vkmin_key_down(in, key) (((in)->down[(key) / 32] >> ((key) % 32)) & 1u)         /* pure */
#define vkmin_key_pressed(in, key) (((in)->pressed[(key) / 32] >> ((key) % 32)) & 1u)   /* pure */

typedef struct {
    int argc; char **argv;        /* command line: cvars as name=value or +name value, plus
                                   * --headless --frame N --frames a,b --out P --out-dir D --exit-after N
                                   * --size W H --path=legacy|modern --sync-naive --no-readback --device N
                                   * --verbose --cvars --record FILE --replay FILE --demo FILE --play FILE;
                                   * unknown ones are the program's */
    const char *title;            /* window title and PNG prefix; 0 = "vkmin" */
    int width, height;            /* 0 = cvars r_width x r_height */
    bool headless;                /* also set by --headless, --frame, --frames */
    vkmin_path path;              /* 0 = modern when the device can, else legacy */
    bool sync_naive;              /* one frame in flight, wait idle per submit (reference) */
    bool no_readback;             /* legacy path: skip the per-frame capture copy */
    bool vsync;                   /* 0 = off (mailbox/immediate); cvar r_vsync overrides */
    int device_index;             /* physical device; also --device N */
    size_t device_arena_bytes;    /* 0 = 256 MB */
    size_t host_ring_bytes;       /* 0 = 64 MB */
} vkmin_desc;

typedef struct {
    vkmin_bytes data;             /* initial contents; .data 0 = uninitialised */
    size_t size;                  /* 0 = data.size */
    const char *label;            /* 0 = "buffer" */
} vkmin_buffer_desc;

typedef struct {
    int width, height;            /* required */
    vkmin_bytes pixels;           /* .data 0 = no upload; tightly packed, mip 0 */
    int mip_levels;               /* 0 = 1 */
    vkmin_format format;          /* 0 = RGBA8_UNORM */
    uint32_t usage;               /* 0 = SAMPLED */
    uint32_t sampler;             /* VKMIN_SAMPLER_* for vkmin_index; 0 = linear repeat */
    const char *label;            /* 0 = "image" */
} vkmin_image_desc;

typedef struct {
    vkmin_bytes vs;                          /* SPIR-V; required for graphics */
    vkmin_bytes fs;                          /* .data 0 = depth-only */
    vkmin_bytes cs;                          /* set instead of vs: a compute pipeline */
    uint32_t push_size;                      /* bytes of the push block every draw passes; must equal the
                                              * push-constant block the SPIR-V declares, or creation aborts */
    vkmin_format color_format;               /* 0 = RGBA8_UNORM (the backbuffer); NONE for depth-only */
    int extra_colors; vkmin_format extra_format[2]; /* further colour attachments (MRT); a blended
                                              * pipeline writes only the first attachment */
    bool depth;                              /* depth test against a D32 attachment. Backbuffer pipelines
                                              * always carry the attachment (the default pass has one). */
    bool depth_write;                        /* 0 = off (set with .depth for the usual case) */
    vkmin_compare depth_compare;             /* 0 = LESS */
    vkmin_cull cull;                         /* 0 = back faces */
    bool blend;                              /* premultiplied-alpha over */
    bool depth_bias;                         /* enable dynamic depth bias */
    const char *label;                       /* 0 = "pipeline" */
    const char *vs_path, *fs_path, *cs_path; /* SPIR-V files to watch when cvar r_hotreload is 1;
                                              * 0 = this pipeline never reloads */
} vkmin_pipeline_desc;

typedef struct { float r, g, b, a; } vkmin_clear;

typedef struct {                  /* everything a frame reads from outside, gathered at one point */
    uint32_t index;               /* the logical frame: the animation's clock, and the journal's */
    uint32_t slot;                /* 0 or 1: which frame in flight */
    int width, height;            /* the render size this frame */
    float aspect;
    vkmin_inputs input;           /* this frame's snapshot; zero when headless without a demo */
} vkmin_frame;

typedef struct {
    vkmin_image color;            /* 0 = depth-only pass */
    vkmin_image extra[2];         /* MRT attachments 1 and 2, cleared to zero with the colour */
    vkmin_image depth;            /* 0 = no depth */
    bool clear_color; float clear[4];
    bool clear_depth;             /* to 1.0 */
    int x, y, w, h;               /* render area; w == 0 = whole image */
    const char *label;            /* debug label; 0 = none */
} vkmin_pass_desc;

typedef struct { vkmin_image image; vkmin_use use; } vkmin_transition;
typedef struct {                  /* one batched barrier at a pass boundary */
    const vkmin_transition *images; int image_count;
    bool compute_to_indirect_draw, compute_to_fragment, transfer_to_compute, frame_start, compute_to_transfer;
} vkmin_barrier_desc;

typedef struct {                  /* an indexed indirect draw; set exactly one of cmds / host_cmds */
    vkmin_buffer indices;         /* required: uint32 index buffer */
    vkmin_buffer cmds; size_t cmd_offset;               /* DrawCmd records in device memory */
    vkmin_buffer counts; size_t count_offset;           /* 0 = fixed count max_draws */
    uint32_t max_draws;
    uint64_t host_cmds; uint32_t host_count;            /* DrawCmd records in ring memory */
} vkmin_indirect_desc;

typedef struct {                  /* heights in, one chunked grid mesh out; sizes from _sizes */
    const float *heights; int width, height;   /* samples across and down; heights in world units */
    float cell;                   /* world units between samples; 0 = 1 */
    int chunk;                    /* cells per chunk side, each chunk one Mesh; 0 = 32 */
    float uv_per_unit;            /* texture repeats per world unit; 0 = 1 */
} vkmin_heightfield_desc;
typedef struct { uint32_t vertices, indices, meshes; } vkmin_heightfield_size;

typedef struct {                  /* plain data, for humans and models to read */
    double gpu_ms[VKMIN_MAX_TIMESTAMPS]; int timestamps;  /* since timestamp 0, last completed frame */
    uint32_t frame_index, draws, dispatches, buffers, images, pipelines, textures;
    size_t device_used, device_cap, ring_used, ring_cap;
    vkmin_path path;
} vkmin_stats;

typedef struct {                  /* what a device offers and what vkmin would do with it */
    char device_name[256]; uint32_t api_major, api_minor; bool vulkan_1_3;
    bool host_image_copy, maintenance5, push_descriptor, pipeline_robustness, robust_buffer_access2;
    bool scalar_block_layout, buffer_device_address, descriptor_indexing, draw_indirect_count;
    vkmin_path would_choose; const char *reason;
} vkmin_report;

/* ---- lifecycle ------------------------------------------------------------- */
vkmin_report vkmin_probe(int device_index);                                  // io (a throwaway instance)
vkmin_ctx *vkmin_init(const vkmin_desc *);                                   // writes ctx, gpu, io
void vkmin_shutdown(vkmin_ctx *);                                            // writes ctx, gpu, io
_Noreturn void vkmin_fail(const char *file, int line, const char *fmt, ...); // io, aborts

/* ---- frame ------------------------------------------------------------------ */
/* The loop:  while (vkmin_running(ctx)) { f = vkmin_frame_begin(ctx, clear); ... vkmin_frame_end(ctx); }
 * running polls the window and the demo file and decides the next frame;
 * false ends the loop: window closed, --exit-after reached, every --frame
 * rendered, demo finished. frame_begin then opens the frame (with a clear,
 * also the default pass: backbuffer + depth) and returns everything the
 * frame may read from outside. frame_end submits and saves --out if asked. */
bool vkmin_running(vkmin_ctx *);                                             // writes ctx, io
vkmin_frame vkmin_frame_begin(vkmin_ctx *, const vkmin_clear *clear);        // writes ctx, gpu, io
void vkmin_frame_end(vkmin_ctx *);                                           // writes ctx, gpu, io
void vkmin_size(const vkmin_ctx *, int *w, int *h);  /* before the loop, to size targets */ // reads ctx
void *vkmin_ring_alloc(vkmin_ctx *, size_t bytes, uint64_t *addr);  /* per-frame host memory */ // writes ctx

/* ---- resources -------------------------------------------------------------- */
vkmin_buffer vkmin_make_buffer(vkmin_ctx *, const vkmin_buffer_desc *);      // writes ctx, gpu
void vkmin_free_buffer(vkmin_ctx *, vkmin_buffer);                           // writes ctx, gpu
uint64_t vkmin_address(vkmin_ctx *, vkmin_buffer);  /* device address, for the push block */ // reads ctx
void vkmin_buffer_upload(vkmin_ctx *, vkmin_buffer, size_t offset, vkmin_bytes);        // gpu
vkmin_image vkmin_make_image(vkmin_ctx *, const vkmin_image_desc *);         // writes ctx, gpu
void vkmin_free_image(vkmin_ctx *, vkmin_image);                             // writes ctx, gpu
void vkmin_image_upload(vkmin_ctx *, vkmin_image, int mip, vkmin_bytes);          // gpu
uint32_t vkmin_index(vkmin_ctx *, vkmin_image);     /* bindless index with the desc's sampler */ // writes ctx, gpu
uint32_t vkmin_register_texture(vkmin_ctx *, vkmin_image, uint32_t sampler); /* a second sampler */ // writes ctx, gpu
vkmin_image vkmin_load_png(vkmin_ctx *, const char *path, bool srgb);        // writes ctx, gpu, io
vkmin_image vkmin_backbuffer(const vkmin_ctx *);   /* the owned image presented each frame */ // reads ctx
vkmin_image vkmin_default_depth(const vkmin_ctx *); /* its depth; passes on the backbuffer attach it */ // reads ctx
vkmin_format vkmin_backbuffer_format(const vkmin_ctx *);                     // pure (always RGBA8_UNORM)
vkmin_pipeline vkmin_make_pipeline(vkmin_ctx *, const vkmin_pipeline_desc *); // writes ctx, gpu
uint32_t vkmin_pick(vkmin_ctx *, vkmin_image r32_uint, int x, int y); /* one texel of the last
                                        completed frame's ID target, between frames; 0 off-image */ // gpu
vkmin_heightfield_size vkmin_heightfield_sizes(const vkmin_heightfield_desc *);            // pure
void vkmin_heightfield(const vkmin_heightfield_desc *, Vertex *, uint32_t *, Mesh *);      // pure

/* ---- recording: in order, into one command buffer, no reordering --------- */
void vkmin_barrier(vkmin_ctx *, const vkmin_barrier_desc *);                 // gpu
void vkmin_fill_buffer(vkmin_ctx *, vkmin_buffer, size_t offset, size_t bytes, uint32_t value); // gpu
void vkmin_copy_to_ring(vkmin_ctx *, vkmin_buffer src, size_t offset, size_t bytes, uint64_t ring_addr); // gpu
void vkmin_pass_begin(vkmin_ctx *, const vkmin_pass_desc *);                 // writes ctx, gpu
void vkmin_pass_end(vkmin_ctx *);                                            // writes ctx, gpu
void vkmin_set_viewport(vkmin_ctx *, int x, int y, int w, int h);            // gpu
void vkmin_set_depth_bias(vkmin_ctx *, float constant, float slope);         // gpu
/* The push block is push_size bytes, as the pipeline declared; 0 = none. */
void vkmin_draw(vkmin_ctx *, vkmin_pipeline, const void *push, uint32_t vertices, uint32_t instances); // gpu
void vkmin_draw_indirect(vkmin_ctx *, vkmin_pipeline, const void *push, const vkmin_indirect_desc *); // gpu
void vkmin_dispatch(vkmin_ctx *, vkmin_pipeline, const void *push, uint32_t x, uint32_t y, uint32_t z); // gpu
void vkmin_timestamp(vkmin_ctx *, int index);   /* 0..VKMIN_MAX_TIMESTAMPS-1, read via stats */ // gpu

/* ---- the journal ------------------------------------------------------------ */
/* --record FILE writes every call after init -- function, arguments, data --
 * to an append-only file. vkmin_replay reads it back and issues the same
 * calls; the render path reads no clock, so the frames are identical. A bug
 * report is a file; a regression is a journal and a frame number. Device
 * addresses inside pushed data are relocated by exact match against the
 * addresses vkmin issued, so a journal replays across runs and paths. */
bool vkmin_replay(vkmin_ctx *, const char *path);                            // writes ctx, gpu, io
/* A demo is the input half of a journal: --demo FILE writes each frame's
 * index and vkmin_inputs; --play FILE feeds them back, one frame per record,
 * so a deterministic game replays itself with no journal of GPU calls. */
/* ---- output and introspection ----------------------------------------------- */
bool vkmin_save_png(vkmin_ctx *, const char *path);  /* last completed frame */   // io
vkmin_stats vkmin_stats_get(const vkmin_ctx *);                              // reads ctx
void vkmin_dump(const vkmin_ctx *, FILE *);  /* every live resource, as text */  // io
vkmin_path vkmin_path_used(const vkmin_ctx *);                               // reads ctx

#endif /* VKMIN_H */
