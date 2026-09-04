/* vkmin.h -- the GPU layer for vkmin v0.1. Vulkan 1.3 core only.
 *
 * What this layer is: one context, one device arena, one host ring, one
 * bindless texture set bound once per frame, pipelines created at init from
 * embedded SPIR-V, and a small set of recording calls the renderer sequences
 * into a frame. Everything the shaders need arrives by device address in a
 * push constant (see shared.h); nothing is bound per draw.
 *
 * What it is not: an allocator (arenas are bump-allocated and never freed; out
 * of memory is an init-time failure), a scene graph, or a material system.
 *
 * Resources are generation-tagged 32-bit handles: 20 bits of index, 12 bits of
 * generation, zero invalid. No pointer crosses this API in either direction.
 * Errors abort with file, line, expression and VkResult.
 */
#ifndef VKMIN_H
#define VKMIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared.h"

typedef struct vkmin_ctx vkmin_ctx;
typedef struct { uint32_t id; } vkmin_buffer;
typedef struct { uint32_t id; } vkmin_image;
typedef struct { uint32_t id; } vkmin_pipe;

enum { VKMIN_MAX_TIMESTAMPS = 32 };

typedef enum {
    VKMIN_FMT_NONE = 0,
    VKMIN_FMT_RGBA8_UNORM,
    VKMIN_FMT_RGBA8_SRGB,
    VKMIN_FMT_BGRA8_UNORM,     /* swapchains usually come this way */
    VKMIN_FMT_BC1_SRGB,
    VKMIN_FMT_BC1_UNORM,
    VKMIN_FMT_BC3_SRGB,
    VKMIN_FMT_BC4_UNORM,
    VKMIN_FMT_BC5_UNORM,
    VKMIN_FMT_R11G11B10_FLOAT,
    VKMIN_FMT_RGBA16_FLOAT,
    VKMIN_FMT_D32_FLOAT,
    VKMIN_FMT_COUNT
} vkmin_format;

/* How an image is about to be used. The layer maps this to layout, stage and
 * access, and remembers the previous use, so a transition is one call. */
typedef enum {
    VKMIN_USE_UNDEFINED = 0,
    VKMIN_USE_TRANSFER_DST,
    VKMIN_USE_TRANSFER_SRC,
    VKMIN_USE_SAMPLED,          /* fragment + compute reads */
    VKMIN_USE_COLOR_TARGET,
    VKMIN_USE_DEPTH_TARGET,
    VKMIN_USE_PRESENT
} vkmin_use;

typedef enum {
    VKMIN_IMAGE_SAMPLED = 1,
    VKMIN_IMAGE_COLOR = 2,
    VKMIN_IMAGE_DEPTH = 4,
    VKMIN_IMAGE_TRANSFER_SRC = 8
} vkmin_image_usage;

/* Which implementation of the three version-dependent operations to use.
 * Chosen once at init from the features the device reports -- a 1.3 device
 * exposing the promoted extensions is "modern" -- and never consulted again
 * except at the three seams. */
typedef enum { VKMIN_PATH_AUTO = 0, VKMIN_PATH_LEGACY, VKMIN_PATH_MODERN } vkmin_path;

typedef struct {
    bool headless;
    vkmin_path path;       /* AUTO picks modern when the device can; a forced path that the
                            * device cannot do fails at init with the missing feature named */
    bool sync_naive;       /* reference path: one frame in flight, wait idle per submit */
    bool no_readback;      /* skip the per-frame backbuffer copy; vkmin_save_png then fails */
    bool vsync;
    int width, height;
    int device_index;
    const char *title;
    size_t device_arena_bytes; /* 0 = 256 MB */
    size_t host_ring_bytes;    /* 0 = 64 MB */
} vkmin_desc;

typedef struct {
    size_t size;
    const void *data;      /* optional initial contents */
    const char *label;
} vkmin_buffer_desc;

typedef struct {
    int width, height;
    int mip_levels;        /* 0 = 1 */
    vkmin_format format;
    uint32_t usage;        /* vkmin_image_usage bits */
    const char *label;
} vkmin_image_desc;

typedef enum { VKMIN_CMP_LESS = 0, VKMIN_CMP_LESS_EQUAL, VKMIN_CMP_EQUAL, VKMIN_CMP_ALWAYS } vkmin_compare;
typedef enum { VKMIN_CULL_BACK = 0, VKMIN_CULL_NONE, VKMIN_CULL_FRONT } vkmin_cull;

typedef struct {
    const uint32_t *vs; size_t vs_bytes;
    const uint32_t *fs; size_t fs_bytes;  /* NULL = depth-only */
    vkmin_format color_format;            /* NONE = no colour attachment */
    bool depth;                           /* attach D32 */
    bool depth_write;
    vkmin_compare depth_compare;
    vkmin_cull cull;
    bool blend;                           /* premultiplied-alpha over */
    bool depth_bias;                      /* enables dynamic depth bias */
    const char *label;
} vkmin_pipe_desc;

typedef struct {
    vkmin_image color;     /* {0} = depth-only pass */
    vkmin_image depth;     /* {0} = no depth */
    bool clear_color;
    float clear[4];
    bool clear_depth;      /* to 1.0 */
    int x, y, w, h;        /* render area; w == 0 means whole image */
    const char *label;
} vkmin_pass_desc;

/* One batched barrier: any number of image transitions plus one global
 * memory barrier between compute and everything, issued as a single
 * vkCmdPipelineBarrier2 at a pass boundary. */
typedef struct {
    vkmin_image image;
    vkmin_use use;
} vkmin_transition;

typedef struct {
    const vkmin_transition *images;
    int image_count;
    bool compute_to_indirect_draw; /* cull output -> indirect commands, index/vertex reads */
    bool compute_to_fragment;      /* cluster lists -> fragment shader reads */
    bool transfer_to_compute;      /* fill/copy -> compute reads/writes */
    bool frame_start;              /* last frame's draws and readback copies read what this
                                    * frame's compute and fills are about to overwrite */
    bool compute_to_transfer;      /* compute output -> copy to the ring for CPU readback */
} vkmin_barrier_desc;

/* What a device offers, and what vkmin would do with it. vkmin_probe creates a
 * throwaway instance, asks, and creates nothing else, so a user or an agent can
 * answer "will this run here" before anything fails. */
typedef struct {
    char device_name[256];
    uint32_t api_major, api_minor;
    bool vulkan_1_3;
    bool host_image_copy, maintenance5, push_descriptor, pipeline_robustness, robust_buffer_access2;
    bool scalar_block_layout, buffer_device_address, descriptor_indexing, draw_indirect_count;
    vkmin_path would_choose;   /* LEGACY or MODERN */
    const char *reason;        /* one line: why */
} vkmin_report;
vkmin_report vkmin_probe(int device_index);
vkmin_path vkmin_path_used(const vkmin_ctx *c);

/* Lifecycle */
vkmin_ctx *vkmin_init(const vkmin_desc *desc);
void vkmin_shutdown(vkmin_ctx *c);
void vkmin_size(const vkmin_ctx *c, int *w, int *h);
bool vkmin_should_close(const vkmin_ctx *c);
bool vkmin_key_hit(const vkmin_ctx *c, int key);
enum { VKMIN_KEY_ESCAPE = 256, VKMIN_KEY_F1 = 290, VKMIN_KEY_F12 = 301, VKMIN_KEY_SPACE = 32 };

/* Resources. All device memory comes from the arena; all are alive until shutdown. */
vkmin_buffer vkmin_make_buffer(vkmin_ctx *c, const vkmin_buffer_desc *desc);
/* Freeing waits for the device to go idle, releases the slot and bumps its
 * generation, so a handle to the freed resource aborts on its next use instead
 * of aliasing whatever lands in the slot. Arena memory is not reclaimed: the
 * arenas are bump allocators, and a free-list suballocator is a later parallel
 * implementation. TODO(v0.5): deferred destruction. */
void vkmin_free_buffer(vkmin_ctx *c, vkmin_buffer b);
void vkmin_free_image(vkmin_ctx *c, vkmin_image img);
uint64_t vkmin_buffer_addr(vkmin_ctx *c, vkmin_buffer b);
void vkmin_buffer_upload(vkmin_ctx *c, vkmin_buffer b, size_t offset, const void *data, size_t bytes);

vkmin_image vkmin_make_image(vkmin_ctx *c, const vkmin_image_desc *desc);
void vkmin_image_upload(vkmin_ctx *c, vkmin_image img, int mip, const void *data, size_t bytes);
uint32_t vkmin_register_texture(vkmin_ctx *c, vkmin_image img, uint32_t sampler_preset);
vkmin_image vkmin_load_png(vkmin_ctx *c, const char *path, bool srgb);
vkmin_image vkmin_backbuffer(const vkmin_ctx *c); /* this frame's presentable image */
vkmin_format vkmin_backbuffer_format(const vkmin_ctx *c); /* always RGBA8_UNORM: the backbuffer is an owned image on both paths */

vkmin_pipe vkmin_make_pipeline(vkmin_ctx *c, const vkmin_pipe_desc *desc);
vkmin_pipe vkmin_make_compute(vkmin_ctx *c, const uint32_t *spv, size_t bytes, const char *label);

/* Frame recording. Between begin and end, calls record into one command buffer
 * in the order written; there is no reordering and no hidden state beyond the
 * currently bound pipeline. */
void vkmin_frame_begin(vkmin_ctx *c);
void vkmin_frame_end(vkmin_ctx *c);
uint32_t vkmin_frame_slot(const vkmin_ctx *c);

/* Per-frame host-visible memory. Valid until this slot's frame is reused. */
void *vkmin_ring_alloc(vkmin_ctx *c, size_t bytes, uint64_t *addr_out);

void vkmin_barrier(vkmin_ctx *c, const vkmin_barrier_desc *desc);
void vkmin_fill_buffer(vkmin_ctx *c, vkmin_buffer b, size_t offset, size_t bytes, uint32_t value);
/* Records a copy from a device buffer into ring memory obtained this frame;
 * the bytes are valid on the host once this frame's fence has been waited,
 * i.e. the next time this frame slot comes round. */
void vkmin_copy_to_ring(vkmin_ctx *c, vkmin_buffer src, size_t offset, size_t bytes, uint64_t ring_addr);

void vkmin_pass_begin(vkmin_ctx *c, const vkmin_pass_desc *desc);
void vkmin_pass_end(vkmin_ctx *c);
void vkmin_set_viewport(vkmin_ctx *c, int x, int y, int w, int h);
void vkmin_set_depth_bias(vkmin_ctx *c, float constant, float slope);

void vkmin_bind_pipeline(vkmin_ctx *c, vkmin_pipe p);
void vkmin_push(vkmin_ctx *c, const Push *push);
void vkmin_bind_index_buffer(vkmin_ctx *c, vkmin_buffer b, size_t offset);
void vkmin_draw(vkmin_ctx *c, uint32_t vertices, uint32_t instances);
void vkmin_draw_indexed_indirect(vkmin_ctx *c, vkmin_buffer cmds, size_t offset, uint32_t count);
void vkmin_draw_indexed_indirect_count(vkmin_ctx *c, vkmin_buffer cmds, size_t cmd_offset,
                                       vkmin_buffer counts, size_t count_offset,
                                       uint32_t max_draws);
void vkmin_draw_indexed_indirect_host(vkmin_ctx *c, uint64_t ring_addr, uint32_t count);
void vkmin_dispatch(vkmin_ctx *c, uint32_t x, uint32_t y, uint32_t z);

/* GPU timestamps. Write up to VKMIN_MAX_TIMESTAMPS per frame; results for the
 * frame that last used this slot are available at frame_begin. */
void vkmin_timestamp(vkmin_ctx *c, int index);
int vkmin_timestamps_read(const vkmin_ctx *c, double *ms_since_first, int cap);

/* Output: last completed frame's backbuffer. */
bool vkmin_save_png(vkmin_ctx *c, const char *path);

/* Memory accounting, for the overlay. */
void vkmin_memory_stats(const vkmin_ctx *c, size_t *device_used, size_t *device_cap,
                        size_t *ring_used, size_t *ring_cap);

#endif /* VKMIN_H */
