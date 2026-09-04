/* vkmin.h -- an ultra-minimal Vulkan 1.3 layer in C11.
 *
 * Scope: get pixels on screen and pixels into a PNG with as little hidden
 * state as possible. Not a renderer, not an engine. Every entry point takes
 * the context explicitly; there are no globals and no callbacks into user
 * code. Resources are 32-bit generation-tagged handles, never pointers.
 *
 * Errors are fatal. Nothing here returns a VkResult; a demo layer that
 * cannot create a device has nothing useful to do with one.
 */
#ifndef VKMIN_H
#define VKMIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vkmin_ctx vkmin_ctx;

/* Handle zero is always invalid. Upper 8 bits are a generation counter, so a
 * stale handle is rejected rather than aliasing a recycled slot. */
typedef struct { uint32_t id; } vkmin_buffer;
typedef struct { uint32_t id; } vkmin_image;
typedef struct { uint32_t id; } vkmin_shader;
typedef struct { uint32_t id; } vkmin_pipe;

enum { VKMIN_MAX_ATTRS = 4, VKMIN_MAX_PUSH = 128 };

typedef enum {
    VKMIN_ATTR_NONE = 0, /* terminates the attribute list */
    VKMIN_ATTR_FLOAT2,
    VKMIN_ATTR_FLOAT3,
    VKMIN_ATTR_FLOAT4
} vkmin_attr_type;

typedef enum { VKMIN_BUFFER_VERTEX = 0, VKMIN_BUFFER_INDEX } vkmin_buffer_type;

typedef struct {
    bool headless;       /* no window, no swapchain: render to an offscreen target */
    bool sync_naive;     /* reference path: one frame in flight + vkDeviceWaitIdle per submit */
    int width, height;   /* offscreen size, or requested window size */
    int device_index;    /* physical device to use */
    const char *title;   /* window title, windowed mode only */
} vkmin_desc;

typedef struct {
    vkmin_buffer_type type;
    const void *data;    /* required: buffers are immutable after creation */
    size_t size;
    const char *label;
} vkmin_buffer_desc;

typedef struct {
    int width, height;
    const void *pixels;  /* required, tightly packed R8G8B8A8 */
    const char *label;
} vkmin_image_desc;

typedef struct {
    vkmin_attr_type type;
    uint32_t offset;
} vkmin_attr;

typedef struct {
    vkmin_shader vs, fs;
    vkmin_attr attrs[VKMIN_MAX_ATTRS]; /* may be empty: vertices from gl_VertexIndex */
    uint32_t vertex_stride;
    bool depth_test;     /* depth test + write, compare LESS */
    bool cull_off;       /* default is back-face culling, counter-clockwise front */
    const char *label;
} vkmin_pipe_desc;

typedef struct { float r, g, b, a; } vkmin_clear;

typedef struct {
    vkmin_pipe pipe;
    vkmin_buffer vbuf;   /* optional */
    vkmin_buffer ibuf;   /* optional; uint16 indices */
    vkmin_image texture; /* optional; a 1x1 opaque white image is bound if absent */
} vkmin_bindings;

/* Lifecycle */
vkmin_ctx *vkmin_init(const vkmin_desc *desc);
void vkmin_shutdown(vkmin_ctx *c);

/* Resources */
vkmin_buffer vkmin_make_buffer(vkmin_ctx *c, const vkmin_buffer_desc *desc);
vkmin_image vkmin_make_image(vkmin_ctx *c, const vkmin_image_desc *desc);
vkmin_image vkmin_load_png(vkmin_ctx *c, const char *path);
vkmin_shader vkmin_make_shader(vkmin_ctx *c, const uint32_t *spv, size_t bytes);
vkmin_pipe vkmin_make_pipeline(vkmin_ctx *c, const vkmin_pipe_desc *desc);

/* Frame */
void vkmin_frame_begin(vkmin_ctx *c, vkmin_clear clear);
void vkmin_bind(vkmin_ctx *c, const vkmin_bindings *bind);
void vkmin_push(vkmin_ctx *c, const void *data, uint32_t bytes);
void vkmin_draw(vkmin_ctx *c, uint32_t count, uint32_t instances);
void vkmin_frame_end(vkmin_ctx *c);

/* Output: writes the last completed frame's colour target. */
bool vkmin_save_png(vkmin_ctx *c, const char *path);

/* Current render-target size. Changes when a window is resized, so the demo
 * asks every frame rather than trusting what it requested at init. */
void vkmin_size(const vkmin_ctx *c, int *w, int *h);

/* Windowed-mode queries; both are false/no-ops when headless. */
bool vkmin_should_close(const vkmin_ctx *c);
bool vkmin_key_hit(const vkmin_ctx *c, int key); /* edge-triggered; see VKMIN_KEY_* */
enum { VKMIN_KEY_ESCAPE = 256, VKMIN_KEY_F12 = 301 };

#endif /* VKMIN_H */
