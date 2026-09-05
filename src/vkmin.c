/* vkmin.c -- see vkmin.h. Vulkan 1.3 core; dynamic rendering, synchronization2,
 * buffer device address, descriptor indexing and drawIndirectCount are
 * required at init, never probed for. There is no VkRenderPass, no
 * VkFramebuffer, no VkPipelineVertexInputStateCreateInfo, and no
 * `if (extension_supported)` anywhere in this file.
 */
#include "vkmin.h"
#include "cvar.h"
#include "plat.h"
#include "stb_bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <vulkan/vulkan.h>

/* ---------------------------------------------------------------- errors -- */

static const char *vk_result_str(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    default: return "VK_ERROR_<unmapped>";
    }
}

#define VK_CHECK(expr)                                                            \
    do {                                                                          \
        const VkResult vkmin__r = (expr);                                         \
        if (vkmin__r != VK_SUCCESS) {                                             \
            fprintf(stderr, "%s:%d: %s -> %s\n", __FILE__, __LINE__, #expr,       \
                    vk_result_str(vkmin__r));                                     \
            fflush(stderr);                                                       \
            abort();                                                              \
        }                                                                         \
    } while (0)

_Noreturn void vkmin_fail(const char *file, int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: vkmin: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

#ifdef VKMIN_NO_PLATFORM
/* Headless-only builds: the platform surface collapses to stubs. */
bool plat_window_open(int w, int h, const char *t) { (void)w; (void)h; (void)t; return false; }
void plat_poll(void) {}
bool plat_should_close(void) { return true; }
void plat_close(void) {}
const char **plat_required_instance_extensions(uint32_t *n) { *n = 0; return NULL; }
VkSurfaceKHR plat_create_surface(VkInstance i) { (void)i; return VK_NULL_HANDLE; }
void plat_framebuffer_size(int *w, int *h) { *w = 0; *h = 0; }
void plat_input(vkmin_inputs *o) { *o = (vkmin_inputs){0}; }
#endif

/* ------------------------------------------------------------- the state -- */

enum {
    VKMIN_MAX_FRAMES = 2,
    VKMIN_MAX_FRAME_LIST = 64,
    VKMIN_LABEL = 40,
    VKMIN_MAX_SWAP = 8,
    VKMIN_HANDLE_INDEX_BITS = 20,
    VKMIN_BACKBUFFER_SLOT = 0,      /* image slot reserved for the presentable image */
    VKMIN_ARENA_ALIGN = 256,
    VKMIN_RING_ALIGN = 64
};

_Static_assert(sizeof(DrawCmd) == sizeof(VkDrawIndexedIndirectCommand), "DrawCmd mirrors Vulkan");
_Static_assert(sizeof(Push) <= VKMIN_PUSH_BYTES, "Push must fit the push constant block");

typedef struct {
    uint16_t gen;
    bool used;
    VkDeviceSize offset; /* into the arena buffer */
    VkDeviceSize size;
    char label[VKMIN_LABEL];
} buffer_slot;

typedef struct {
    uint16_t gen;
    bool used;
    bool external;       /* swapchain image: not ours to destroy */
    VkImage img;
    VkImageView view;
    VkFormat format;
    VkImageAspectFlags aspect;
    uint32_t w, h, mips;
    vkmin_use use;       /* what it was last transitioned for */
    uint32_t tex_index;  /* bindless slot from vkmin_index, or UINT32_MAX */
    uint32_t sampler;
    char label[VKMIN_LABEL];
} image_slot;

typedef struct {
    uint16_t gen;
    bool used;
    VkPipeline pipe;
    VkPipelineBindPoint bind_point;
    char label[VKMIN_LABEL];
    /* For hot reload: the desc to rebuild from, the files' last mtimes, and
     * the blobs read from disk (owned here once a reload has happened). */
    vkmin_pipe_desc desc;
    long mtime[3];
    uint32_t *loaded[3];
} pipe_slot;

typedef struct {
    VkDeviceMemory mem;
    VkDeviceSize cap, used;
    uint32_t type;
} arena;

/* The features that decide the path, plus the two that only affect debug
 * builds. Filled once; read at init and at the three seams. */
typedef struct {
    bool host_image_copy, maintenance5, push_descriptor, pipeline_robustness, robust_buffer_access2;
} path_caps;

struct vkmin_ctx {
    vkmin_desc desc;
    bool debug;
    vkmin_path path;
    path_caps caps;
    /* Host image copy entry points (modern path only). */
    PFN_vkCopyMemoryToImageEXT fp_copy_memory_to_image;
    PFN_vkCopyImageToMemoryEXT fp_copy_image_to_memory;
    PFN_vkTransitionImageLayoutEXT fp_transition_image_layout;

    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    PFN_vkSetDebugUtilsObjectNameEXT fp_set_name;
    PFN_vkDestroyDebugUtilsMessengerEXT fp_destroy_messenger;
    PFN_vkCmdBeginDebugUtilsLabelEXT fp_label_begin;
    PFN_vkCmdEndDebugUtilsLabelEXT fp_label_end;

    VkPhysicalDevice phys;
    VkPhysicalDeviceMemoryProperties mem_props;
    float timestamp_period_ns;
    uint32_t queue_family;
    VkDevice dev;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd[VKMIN_MAX_FRAMES];
    VkFence fence[VKMIN_MAX_FRAMES];
    VkSemaphore acquired[VKMIN_MAX_FRAMES];
    bool fence_pending[VKMIN_MAX_FRAMES];
    uint32_t frames_in_flight;
    uint32_t slot;
    uint32_t last_slot;
    bool have_submitted;
    bool in_frame;
    bool in_pass;
    bool in_default_pass;
    uint32_t frame_index;      /* logical frame: what vkmin_frame_index reports */
    uint32_t frames_rendered;
    uint32_t draws, dispatches; /* this frame's, copied into stats at frame end */
    vkmin_stats stats;

    /* The command line, as vkmin_init understood it. */
    int frame_list[VKMIN_MAX_FRAME_LIST];
    int frame_count, frame_cursor;
    int exit_after;
    const char *out, *out_dir;
    bool verbose;

    vkmin_image default_depth;   /* for the default pass a clear in frame_begin opens */

    /* The journal. `depth` suppresses recording of public calls made from
     * inside other public calls, so each record is one call the program made. */
    FILE *rec;
    int rec_depth;
    bool replaying;
    uint64_t rec_arena_base, rec_ring_base; /* bases in the recording being replayed */
    VkDeviceSize ring_issued[256];          /* ring offsets handed out this frame */
    int ring_issued_count;
    const char *record_path, *replay_path;

    /* Input: one snapshot a frame, taken in frame_begin and read nowhere else.
     * A demo file is the snapshots alone, without the GPU calls. */
    vkmin_inputs input, prev_input;
    FILE *demo_out, *demo_in;
    const char *demo_path, *play_path;
    int frame_last;              /* highest --frame asked for, or -1 */

    VkCommandBuffer imm_cmd;
    VkFence imm_fence;

    /* Memory: one device arena backing one buffer, one device arena for
     * images, one persistently mapped host ring. Bump allocated, never freed. */
    arena buf_arena;
    VkBuffer arena_buf;
    VkDeviceAddress arena_addr;
    arena img_arena;
    VkBuffer ring_buf;
    VkDeviceMemory ring_mem;
    VkDeviceAddress ring_addr;
    uint8_t *ring_mapped;
    VkDeviceSize ring_cap;          /* whole ring */
    VkDeviceSize ring_region;       /* per frame slot */
    VkDeviceSize ring_head[VKMIN_MAX_FRAMES];

    /* Bindless: one set, bound once per frame to both bind points. */
    VkSampler samplers[VKMIN_SAMPLER_COUNT];
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool desc_pool;
    VkDescriptorSet set;
    VkPipelineLayout pipe_layout;
    uint32_t texture_count;

    VkQueryPool query_pool;
    int ts_written[VKMIN_MAX_FRAMES];
    double ts_ms[VKMIN_MAX_TIMESTAMPS];
    int ts_count;

    VkExtent2D extent;
    VkFormat backbuffer_format;
    VkImage offscreen_img;          /* the backbuffer on both paths and in both modes; windowed
                                     * frames blit it to the swapchain image at frame end */
    VkImageView offscreen_view;

    VkSurfaceKHR surface;
    VkFormat swap_format;
    VkExtent2D swap_extent;
    VkSwapchainKHR swapchain;
    uint32_t swap_count;
    VkImage swap_img[VKMIN_MAX_SWAP];
    VkSemaphore rendered[VKMIN_MAX_SWAP];
    uint32_t swap_index;
    bool need_recreate;

    VkBuffer readback_buf[VKMIN_MAX_FRAMES];
    VkDeviceMemory readback_mem[VKMIN_MAX_FRAMES];
    void *readback_mapped[VKMIN_MAX_FRAMES];
    VkDeviceSize readback_size;

    buffer_slot buffers[VKMIN_MAX_BUFFERS];
    image_slot images[VKMIN_MAX_IMAGES];
    pipe_slot pipes[VKMIN_MAX_PIPES];
};

/* ------------------------------------------------------------- handles ---- */

/* id = generation << 20 | (index + 1). Zero is invalid. Freeing a slot bumps
 * its generation, so a handle to the freed resource is caught at its next
 * lookup rather than aliasing the slot's next occupant. */
static uint32_t handle_make(uint32_t index, uint16_t gen) {
    return ((uint32_t)gen << VKMIN_HANDLE_INDEX_BITS) | (index + 1u);
}
static uint32_t handle_index(uint32_t id) {
    return (id & ((1u << VKMIN_HANDLE_INDEX_BITS) - 1u)) - 1u;
}
static uint16_t handle_gen(uint32_t id) { return (uint16_t)(id >> VKMIN_HANDLE_INDEX_BITS); }
/* 12 bits of generation; never back to 0, which is what makes 0 invalid. */
static uint16_t gen_next(uint16_t gen) { return (uint16_t)(gen >= 4095 ? 1 : gen + 1); }

#define VKMIN_SLOT_ALLOC(pool, count, out_index)                                  \
    do {                                                                          \
        (out_index) = (uint32_t)(count);                                          \
        for (uint32_t i__ = 0; i__ < (count); ++i__) {                            \
            if (!(pool)[i__].used) {                                              \
                (out_index) = i__;                                                \
                break;                                                            \
            }                                                                     \
        }                                                                         \
        VKMIN_ASSERT((out_index) < (count), #pool " pool exhausted (%u slots)",   \
                     (unsigned)(count));                                          \
        (pool)[(out_index)].used = true;                                          \
        if ((pool)[(out_index)].gen == 0) (pool)[(out_index)].gen = 1;            \
    } while (0)

#define VKMIN_SLOT_LOOKUP(pool, count, id, out)                                   \
    do {                                                                          \
        VKMIN_ASSERT((id) != 0, #pool ": null handle");                           \
        const uint32_t idx__ = handle_index(id);                                  \
        VKMIN_ASSERT(idx__ < (count), #pool ": handle index %u out of range",     \
                     idx__);                                                      \
        VKMIN_ASSERT((pool)[idx__].used, #pool ": handle refers to a freed slot"); \
        VKMIN_ASSERT((pool)[idx__].gen == handle_gen(id),                         \
                     #pool ": stale handle (gen %u, slot gen %u)",                \
                     (unsigned)handle_gen(id), (unsigned)(pool)[idx__].gen);      \
        (out) = &(pool)[idx__];                                                   \
    } while (0)

/* --------------------------------------------------------- debug naming --- */

static void set_name(vkmin_ctx *c, VkObjectType type, uint64_t handle, const char *fmt, ...) {
    if (!c->fp_set_name || handle == 0) return;
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    const VkDebugUtilsObjectNameInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = buf,
    };
    VK_CHECK(c->fp_set_name(c->dev, &info));
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                               VkDebugUtilsMessageTypeFlagsEXT types,
                                               const VkDebugUtilsMessengerCallbackDataEXT *data,
                                               void *user) {
    (void)types;
    (void)user;
    if (severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) return VK_FALSE;
    fprintf(stderr, "\nvalidation: %s\n%s\n", data->pMessageIdName ? data->pMessageIdName : "?",
            data->pMessage ? data->pMessage : "");
    for (uint32_t i = 0; i < data->objectCount; ++i) {
        fprintf(stderr, "  object[%u]: %s\n", i,
                data->pObjects[i].pObjectName ? data->pObjects[i].pObjectName : "<unnamed>");
    }
    for (uint32_t i = 0; i < data->cmdBufLabelCount; ++i) {
        fprintf(stderr, "  in pass: %s\n", data->pCmdBufLabels[i].pLabelName);
    }
    fflush(stderr);
    abort(); /* warnings-as-errors, Vulkan edition */
}

/* -------------------------------------------------------------- journal --- */

enum {
    OP_MAKE_BUFFER = 1, OP_FREE_BUFFER, OP_BUFFER_UPLOAD, OP_MAKE_IMAGE, OP_FREE_IMAGE, OP_IMAGE_UPLOAD,
    OP_INDEX, OP_REGISTER, OP_MAKE_PIPELINE, OP_FRAME_BEGIN, OP_FRAME_END, OP_RING_ALLOC, OP_BARRIER,
    OP_FILL, OP_COPY_TO_RING, OP_PASS_BEGIN, OP_PASS_END, OP_VIEWPORT, OP_DEPTH_BIAS, OP_DRAW,
    OP_DRAW_INDIRECT, OP_DISPATCH, OP_TIMESTAMP
};
enum { RELOC_ARENA = 1, RELOC_RING = 2, VKMIN_MAX_RELOCS = 4096 };

typedef struct { uint32_t magic, version, width, height; uint64_t arena_base, ring_base; } journal_header;
typedef struct { uint32_t op, hdr_bytes, data_bytes, reloc_count; } record_header;
typedef struct { uint32_t offset; uint32_t kind; } reloc;
/* Fixed-size mirrors of the descs, with strings copied and pointers dropped. */
typedef struct { uint64_t size; uint32_t result, has_data; char label[VKMIN_LABEL]; } rec_buffer;
typedef struct { int32_t w, h, mips; uint32_t format, usage, sampler, result, has_pixels; char label[VKMIN_LABEL]; } rec_image;
typedef struct { uint32_t vs_bytes, fs_bytes, cs_bytes, color_format, depth, depth_write, compare, cull, blend, bias, result; char label[VKMIN_LABEL]; } rec_pipe;
typedef struct { uint32_t id, mip; uint64_t offset; } rec_upload;
typedef struct { uint32_t frame_index, has_clear; vkmin_clear clear; vkmin_inputs input; } rec_frame;
typedef struct { uint32_t magic, version, width, height; } demo_header;
typedef struct { uint32_t frame_index; vkmin_inputs input; } demo_record;
typedef struct { uint32_t color, depth, clear_color, clear_depth; float clear[4]; int32_t x, y, w, h; } rec_pass;
typedef struct { uint32_t pipe, push_bytes, a, b, cnt; } rec_draw;
typedef struct { uint32_t pipe, push_bytes, indices, cmds, counts, max_draws, host_count; uint64_t cmd_offset, count_offset, host_cmds; } rec_indirect;
typedef struct { uint32_t flags, image_count; } rec_barrier;

/* Which 8-byte words of `data` hold an address vkmin issued: an arena buffer
 * address or a ring allocation from this frame. Exact match, not a range. */
static int scan_relocs(const vkmin_ctx *c, const void *data, size_t bytes, reloc *out, int cap) {
    int n = 0;
    const uint8_t *p = data;
    for (size_t off = 0; off + 8 <= bytes && n < cap; off += 8) {
        uint64_t v;
        memcpy(&v, p + off, 8);
        if (v == 0) continue;
        if (v >= c->arena_addr && v < c->arena_addr + c->buf_arena.cap) {
            for (uint32_t i = 0; i < VKMIN_MAX_BUFFERS; ++i) {
                if (c->buffers[i].used && v == c->arena_addr + c->buffers[i].offset) { out[n++] = (reloc){(uint32_t)off, RELOC_ARENA}; break; }
            }
        } else if (v >= c->ring_addr && v < c->ring_addr + c->ring_cap) {
            for (int i = 0; i < c->ring_issued_count; ++i) {
                if (v == c->ring_addr + c->ring_issued[i]) { out[n++] = (reloc){(uint32_t)off, RELOC_RING}; break; }
            }
        }
    }
    return n;
}

static void journal_write(vkmin_ctx *c, uint32_t op, const void *hdr, size_t hdr_bytes, const void *data, size_t data_bytes) {
    if (!c->rec || c->rec_depth > 0) return;
    static reloc relocs[VKMIN_MAX_RELOCS];
    const int n = data ? scan_relocs(c, data, data_bytes, relocs, VKMIN_MAX_RELOCS) : 0;
    const record_header rh = {op, (uint32_t)hdr_bytes, (uint32_t)data_bytes, (uint32_t)n};
    const bool ok = fwrite(&rh, sizeof rh, 1, c->rec) == 1 && (hdr_bytes == 0 || fwrite(hdr, hdr_bytes, 1, c->rec) == 1) &&
                    (data_bytes == 0 || fwrite(data, data_bytes, 1, c->rec) == 1) &&
                    (n == 0 || fwrite(relocs, sizeof relocs[0], (size_t)n, c->rec) == (size_t)n);
    VKMIN_ASSERT(ok, "journal write failed");
}
#define RECORD(c, op, hdr, data, bytes) journal_write((c), (op), &(hdr), sizeof(hdr), (data), (bytes))
#define RECORD_ENTER(c) ((c)->rec_depth++)
#define RECORD_LEAVE(c) ((c)->rec_depth--)

/* --------------------------------------------------------------- device --- */

static bool layer_present(const char *name) {
    uint32_t n = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&n, NULL));
    VkLayerProperties props[64];
    if (n > 64) n = 64;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&n, props));
    for (uint32_t i = 0; i < n; ++i) {
        if (strcmp(props[i].layerName, name) == 0) return true;
    }
    return false;
}

static bool instance_extension_present(const char *name) {
    uint32_t n = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &n, NULL));
    VkExtensionProperties props[256];
    if (n > 256) n = 256;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &n, props));
    for (uint32_t i = 0; i < n; ++i) {
        if (strcmp(props[i].extensionName, name) == 0) return true;
    }
    return false;
}

static void create_instance(vkmin_ctx *c) {
    const char *layers[1];
    uint32_t layer_count = 0;
    const char *extensions[8];
    uint32_t ext_count = 0;

    if (c->debug) {
        if (layer_present("VK_LAYER_KHRONOS_validation") &&
            instance_extension_present(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers[layer_count++] = "VK_LAYER_KHRONOS_validation";
            extensions[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        } else {
            fprintf(stderr, "vkmin: validation layer unavailable; continuing without it\n");
            c->debug = false;
        }
    }
    if (!c->desc.headless) {
        uint32_t plat_count = 0;
        const char **plat_ext = plat_required_instance_extensions(&plat_count);
        VKMIN_ASSERT(plat_ext != NULL, "platform reports no Vulkan surface extensions");
        for (uint32_t i = 0; i < plat_count; ++i) {
            VKMIN_ASSERT(ext_count < 8, "too many instance extensions");
            extensions[ext_count++] = plat_ext[i];
        }
    }

    const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vkmin",
        .applicationVersion = 1,
        .pEngineName = "vkmin",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkDebugUtilsMessengerCreateInfoEXT dbg = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_cb,
    };
    /* Synchronization validation finds a different class of defect from the
     * core checks -- exactly the cross-pass barrier reasoning the frame
     * depends on -- so it is on whenever validation is. */
    const VkValidationFeatureEnableEXT enabled_features[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };
    const VkValidationFeaturesEXT validation_features = {
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = &dbg,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures = enabled_features,
    };
    const VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = c->debug ? (const void *)&validation_features : NULL,
        .pApplicationInfo = &app,
        .enabledLayerCount = layer_count,
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = extensions,
    };
    VK_CHECK(vkCreateInstance(&info, NULL, &c->instance));

    if (c->debug) {
        PFN_vkCreateDebugUtilsMessengerEXT create =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                c->instance, "vkCreateDebugUtilsMessengerEXT");
        c->fp_destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            c->instance, "vkDestroyDebugUtilsMessengerEXT");
        c->fp_label_begin = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(
            c->instance, "vkCmdBeginDebugUtilsLabelEXT");
        c->fp_label_end = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(
            c->instance, "vkCmdEndDebugUtilsLabelEXT");
        VKMIN_ASSERT(create && c->fp_destroy_messenger && c->fp_label_begin && c->fp_label_end,
                     "debug utils entry points missing");
        VK_CHECK(create(c->instance, &dbg, NULL, &c->messenger));
    }
}

static path_caps query_caps(VkPhysicalDevice phys) {
    uint32_t n = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(phys, NULL, &n, NULL));
    VkExtensionProperties *ext = calloc(n ? n : 1, sizeof *ext);
    VKMIN_ASSERT(ext != NULL, "out of memory");
    VK_CHECK(vkEnumerateDeviceExtensionProperties(phys, NULL, &n, ext));
    bool has_hic = false, has_m5 = false, has_pd = false, has_pr = false, has_r2 = false;
    for (uint32_t i = 0; i < n; ++i) {
        if (!strcmp(ext[i].extensionName, VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME)) has_hic = true;
        if (!strcmp(ext[i].extensionName, VK_KHR_MAINTENANCE_5_EXTENSION_NAME)) has_m5 = true;
        if (!strcmp(ext[i].extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) has_pd = true;
        if (!strcmp(ext[i].extensionName, VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME)) has_pr = true;
        if (!strcmp(ext[i].extensionName, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) has_r2 = true;
    }
    free(ext);
    /* An extension that is present but whose feature bit is off is absent. */
    VkPhysicalDeviceHostImageCopyFeaturesEXT hic = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT};
    VkPhysicalDeviceMaintenance5FeaturesKHR m5 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, .pNext = &hic};
    VkPhysicalDevicePipelineRobustnessFeaturesEXT pr = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT, .pNext = &m5};
    VkPhysicalDeviceRobustness2FeaturesEXT r2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .pNext = &pr};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &r2};
    vkGetPhysicalDeviceFeatures2(phys, &f2);
    return (path_caps){
        .host_image_copy = has_hic && hic.hostImageCopy,
        .maintenance5 = has_m5 && m5.maintenance5,
        .push_descriptor = has_pd,
        .pipeline_robustness = has_pr && pr.pipelineRobustness,
        .robust_buffer_access2 = has_r2 && r2.robustBufferAccess2,
    };
}

/* The one place the path is decided. Version number is not the test. */
static vkmin_path choose_path(path_caps k, vkmin_path want, const char **reason) {
    const bool can_modern = k.host_image_copy && k.maintenance5;
    if (want == VKMIN_PATH_LEGACY) { *reason = "legacy requested"; return VKMIN_PATH_LEGACY; }
    if (want == VKMIN_PATH_MODERN) {
        if (!k.host_image_copy) VKMIN_FAIL("--path=modern requested but the device lacks hostImageCopy");
        if (!k.maintenance5) VKMIN_FAIL("--path=modern requested but the device lacks maintenance5");
        *reason = "modern requested";
        return VKMIN_PATH_MODERN;
    }
    if (can_modern) { *reason = "hostImageCopy and maintenance5 present"; return VKMIN_PATH_MODERN; }
    *reason = !k.host_image_copy ? "no hostImageCopy" : "no maintenance5";
    return VKMIN_PATH_LEGACY;
}

vkmin_report vkmin_probe(int device_index) {
    vkmin_report r = {0};
    const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    const VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&info, NULL, &inst) != VK_SUCCESS) { r.reason = "no Vulkan instance"; return r; }
    uint32_t n = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(inst, &n, NULL));
    VkPhysicalDevice devices[16];
    if (n > 16) n = 16;
    VK_CHECK(vkEnumeratePhysicalDevices(inst, &n, devices));
    if ((uint32_t)device_index >= n) { vkDestroyInstance(inst, NULL); r.reason = "no such device"; return r; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(devices[device_index], &props);
    snprintf(r.device_name, sizeof r.device_name, "%s", props.deviceName);
    r.api_major = VK_VERSION_MAJOR(props.apiVersion);
    r.api_minor = VK_VERSION_MINOR(props.apiVersion);
    r.vulkan_1_3 = props.apiVersion >= VK_API_VERSION_1_3;
    const path_caps k = query_caps(devices[device_index]);
    r.host_image_copy = k.host_image_copy;
    r.maintenance5 = k.maintenance5;
    r.push_descriptor = k.push_descriptor;
    r.pipeline_robustness = k.pipeline_robustness;
    r.robust_buffer_access2 = k.robust_buffer_access2;
    VkPhysicalDeviceVulkan12Features f12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f12};
    vkGetPhysicalDeviceFeatures2(devices[device_index], &f2);
    r.scalar_block_layout = f12.scalarBlockLayout;
    r.buffer_device_address = f12.bufferDeviceAddress;
    r.descriptor_indexing = f12.descriptorIndexing;
    r.draw_indirect_count = f12.drawIndirectCount;
    r.would_choose = choose_path(k, VKMIN_PATH_AUTO, &r.reason);
    vkDestroyInstance(inst, NULL);
    return r;
}

vkmin_path vkmin_path_used(const vkmin_ctx *c) { return c->path; }

static void pick_physical_device(vkmin_ctx *c) {
    uint32_t n = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(c->instance, &n, NULL));
    VKMIN_ASSERT(n > 0, "no Vulkan physical devices");
    VkPhysicalDevice devices[16];
    if (n > 16) n = 16;
    VK_CHECK(vkEnumeratePhysicalDevices(c->instance, &n, devices));

    const uint32_t want = (uint32_t)(c->desc.device_index < 0 ? 0 : c->desc.device_index);
    VKMIN_ASSERT(want < n, "device_index %u out of range (%u devices)", want, n);
    c->phys = devices[want];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(c->phys, &props);
    VKMIN_ASSERT(props.apiVersion >= VK_API_VERSION_1_3,
                 "device '%s' reports Vulkan %u.%u; vkmin requires 1.3 core", props.deviceName,
                 VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
    VKMIN_ASSERT(props.limits.maxPushConstantsSize >= sizeof(Push),
                 "device '%s' offers only %u push constant bytes", props.deviceName,
                 props.limits.maxPushConstantsSize);
    VKMIN_ASSERT(props.limits.timestampComputeAndGraphics, "device '%s' has no timestamps",
                 props.deviceName);
    c->timestamp_period_ns = props.limits.timestampPeriod;

    /* Every feature the design leans on, demanded up front and by name. */
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &f12};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                    .pNext = &f13};
    vkGetPhysicalDeviceFeatures2(c->phys, &f2);
#define NEED(cond, what) VKMIN_ASSERT((cond), "device '%s' lacks %s", props.deviceName, what)
    NEED(f13.dynamicRendering, "dynamicRendering");
    NEED(f13.synchronization2, "synchronization2");
    NEED(f12.bufferDeviceAddress, "bufferDeviceAddress");
    NEED(f12.scalarBlockLayout, "scalarBlockLayout");
    NEED(f12.descriptorIndexing, "descriptorIndexing");
    NEED(f12.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
    NEED(f12.descriptorBindingSampledImageUpdateAfterBind, "sampledImageUpdateAfterBind");
    NEED(f12.shaderSampledImageArrayNonUniformIndexing, "sampledImageArrayNonUniformIndexing");
    NEED(f12.runtimeDescriptorArray, "runtimeDescriptorArray");
    NEED(f12.drawIndirectCount, "drawIndirectCount");
    NEED(f12.hostQueryReset, "hostQueryReset");
    NEED(f2.features.multiDrawIndirect, "multiDrawIndirect");
    NEED(f2.features.drawIndirectFirstInstance, "drawIndirectFirstInstance");
    NEED(f2.features.textureCompressionBC, "textureCompressionBC");
    NEED(f2.features.samplerAnisotropy, "samplerAnisotropy");
    NEED(f2.features.depthClamp, "depthClamp");
    NEED(f2.features.shaderInt64, "shaderInt64");
    NEED(f2.features.fragmentStoresAndAtomics, "fragmentStoresAndAtomics");
#undef NEED

    vkGetPhysicalDeviceMemoryProperties(c->phys, &c->mem_props);

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &qn, NULL);
    VkQueueFamilyProperties qprops[16];
    if (qn > 16) qn = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &qn, qprops);
    c->queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < qn; ++i) {
        const VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        if ((qprops[i].queueFlags & need) == need && qprops[i].timestampValidBits >= 32) {
            c->queue_family = i;
            break;
        }
    }
    VKMIN_ASSERT(c->queue_family != UINT32_MAX, "no graphics+compute+transfer queue with timestamps");

    /* The init report: every feature checked, and the path chosen and why. */
    c->caps = query_caps(c->phys);
    const char *reason = "";
    c->path = choose_path(c->caps, c->desc.path, &reason);
    fprintf(stderr, "vkmin: device[%u] %s (Vulkan %u.%u)\n", want, props.deviceName,
            VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
    fprintf(stderr, "vkmin: hostImageCopy=%d maintenance5=%d pushDescriptor=%d pipelineRobustness=%d "
                    "robustBufferAccess2=%d\n", c->caps.host_image_copy, c->caps.maintenance5,
            c->caps.push_descriptor, c->caps.pipeline_robustness, c->caps.robust_buffer_access2);
    fprintf(stderr, "vkmin: path = %s (%s)%s\n", c->path == VKMIN_PATH_MODERN ? "modern" : "legacy", reason,
            c->debug ? (c->caps.pipeline_robustness && c->caps.robust_buffer_access2
                            ? "; debug pipelines use robustBufferAccess2"
                            : "; robustBufferAccess2 absent, debug pipelines without it")
                     : "");
}

static void create_device(vkmin_ctx *c) {
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo qinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = c->queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .drawIndirectCount = VK_TRUE,
        .descriptorIndexing = VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .scalarBlockLayout = VK_TRUE,
        .hostQueryReset = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &f12,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 f2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &f13,
        .features = {.multiDrawIndirect = VK_TRUE,
                     .drawIndirectFirstInstance = VK_TRUE,
                     .depthClamp = VK_TRUE,
                     .samplerAnisotropy = VK_TRUE,
                     .textureCompressionBC = VK_TRUE,
                     .shaderInt64 = VK_TRUE,
                     .fragmentStoresAndAtomics = VK_TRUE},
    };
    /* Modern-path features chain in only when that path was chosen; the
     * robustness features chain in for debug builds on either path. On a
     * 1.4 driver these are all core and the extension names are accepted
     * aliases. */
    VkPhysicalDeviceHostImageCopyFeaturesEXT hic = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT, .hostImageCopy = VK_TRUE};
    VkPhysicalDeviceMaintenance5FeaturesKHR m5 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, .pNext = &hic, .maintenance5 = VK_TRUE};
    VkPhysicalDevicePipelineRobustnessFeaturesEXT pr = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT, .pipelineRobustness = VK_TRUE};
    VkPhysicalDeviceRobustness2FeaturesEXT r2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .pNext = &pr, .robustBufferAccess2 = VK_TRUE};
    const char *extensions[8];
    uint32_t ext_count = 0;
    if (!c->desc.headless) extensions[ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    const void *chain = &f2;
    if (c->path == VKMIN_PATH_MODERN) {
        extensions[ext_count++] = VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME;
        extensions[ext_count++] = VK_KHR_MAINTENANCE_5_EXTENSION_NAME;
        f12.pNext = &m5;
    }
    const bool robust = c->debug && c->caps.pipeline_robustness && c->caps.robust_buffer_access2;
    if (robust) {
        f2.features.robustBufferAccess = VK_TRUE; /* robustBufferAccess2 requires the base feature too */
        extensions[ext_count++] = VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME;
        extensions[ext_count++] = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
        hic.pNext = c->path == VKMIN_PATH_MODERN ? (void *)&r2 : NULL;
        if (c->path != VKMIN_PATH_MODERN) f12.pNext = &r2;
    }
    const VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = chain,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qinfo,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = extensions,
    };
    VK_CHECK(vkCreateDevice(c->phys, &info, NULL, &c->dev));
    vkGetDeviceQueue(c->dev, c->queue_family, 0, &c->queue);
    if (c->path == VKMIN_PATH_MODERN) {
        c->fp_copy_memory_to_image = (PFN_vkCopyMemoryToImageEXT)vkGetDeviceProcAddr(c->dev, "vkCopyMemoryToImageEXT");
        c->fp_copy_image_to_memory = (PFN_vkCopyImageToMemoryEXT)vkGetDeviceProcAddr(c->dev, "vkCopyImageToMemoryEXT");
        c->fp_transition_image_layout = (PFN_vkTransitionImageLayoutEXT)vkGetDeviceProcAddr(c->dev, "vkTransitionImageLayoutEXT");
        VKMIN_ASSERT(c->fp_copy_memory_to_image && c->fp_copy_image_to_memory && c->fp_transition_image_layout,
                     "host image copy entry points missing");
    }
    if (c->debug) {
        c->fp_set_name = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
            c->dev, "vkSetDebugUtilsObjectNameEXT");
    }
    set_name(c, VK_OBJECT_TYPE_DEVICE, (uint64_t)c->dev, "vkmin.device");
    set_name(c, VK_OBJECT_TYPE_QUEUE, (uint64_t)c->queue, "vkmin.queue");
}

/* --------------------------------------------------------------- memory --- */

static uint32_t find_memory_type(const vkmin_ctx *c, uint32_t type_bits,
                                 VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < c->mem_props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (c->mem_props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    VKMIN_FAIL("no memory type with properties 0x%x", want);
}

static VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) & ~(a - 1); }

/* Bump allocation. There is no free: everything lives until shutdown, so
 * running out is an init-time failure with a number in it, never a runtime
 * surprise. */
static VkDeviceSize arena_alloc(arena *a, VkDeviceSize size, VkDeviceSize alignment,
                                const char *what) {
    const VkDeviceSize offset = align_up(a->used, alignment);
    VKMIN_ASSERT(offset + size <= a->cap,
                 "%s arena exhausted: need %llu bytes at %llu, capacity %llu", what,
                 (unsigned long long)size, (unsigned long long)offset,
                 (unsigned long long)a->cap);
    a->used = offset + size;
    return offset;
}

static void arena_create(vkmin_ctx *c, arena *a, VkDeviceSize cap, uint32_t type_bits,
                         VkMemoryPropertyFlags props, bool device_address, const char *label) {
    a->cap = cap;
    a->used = 0;
    a->type = find_memory_type(c, type_bits, props);
    const VkMemoryAllocateFlagsInfo flags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    };
    const VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = device_address ? &flags : NULL,
        .allocationSize = cap,
        .memoryTypeIndex = a->type,
    };
    VK_CHECK(vkAllocateMemory(c->dev, &info, NULL, &a->mem));
    set_name(c, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)a->mem, "%s", label);
}

/* A whole VkBuffer with its own memory; used for the arena buffer, the ring,
 * and the readback buffers. Everything the API calls a "buffer" is a range
 * inside the arena buffer instead. */
static void create_backing_buffer(vkmin_ctx *c, VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags mem_flags, VkBuffer *out_buf,
                                  VkDeviceMemory *out_mem, VkDeviceAddress *out_addr,
                                  const char *label) {
    const bool bda = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(c->dev, &info, NULL, out_buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c->dev, *out_buf, &req);
    const VkMemoryAllocateFlagsInfo flags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    };
    const VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = bda ? &flags : NULL,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(c, req.memoryTypeBits, mem_flags),
    };
    VK_CHECK(vkAllocateMemory(c->dev, &alloc, NULL, out_mem));
    VK_CHECK(vkBindBufferMemory(c->dev, *out_buf, *out_mem, 0));
    if (out_addr) {
        const VkBufferDeviceAddressInfo ai = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = *out_buf};
        *out_addr = vkGetBufferDeviceAddress(c->dev, &ai);
    }
    set_name(c, VK_OBJECT_TYPE_BUFFER, (uint64_t)*out_buf, "%s", label);
}

static void create_memory(vkmin_ctx *c) {
    const VkDeviceSize buf_cap = c->desc.device_arena_bytes ? c->desc.device_arena_bytes
                                                            : (VkDeviceSize)256u << 20;
    const VkDeviceSize ring_cap = c->desc.host_ring_bytes ? c->desc.host_ring_bytes
                                                          : (VkDeviceSize)64u << 20;

    /* One buffer for every device-side buffer the renderer will ever make. */
    const VkBufferUsageFlags arena_usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkDeviceMemory arena_mem = VK_NULL_HANDLE;
    create_backing_buffer(c, buf_cap, arena_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &c->arena_buf, &arena_mem, &c->arena_addr, "vkmin.arena");
    c->buf_arena = (arena){.mem = arena_mem, .cap = buf_cap, .used = 0};

    /* Images get their own arena; the memory type is whatever a depth image
     * and a colour image both accept, checked again at every bind. */
    const VkImageCreateInfo probe_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {4, 4, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage probe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImage(c->dev, &probe_info, NULL, &probe));
    VkMemoryRequirements probe_req;
    vkGetImageMemoryRequirements(c->dev, probe, &probe_req);
    vkDestroyImage(c->dev, probe, NULL);
    arena_create(c, &c->img_arena, buf_cap, probe_req.memoryTypeBits,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, "vkmin.image_arena");

    /* The host ring: staging at init, per-frame data thereafter. Split into
     * one region per frame in flight so a frame never overwrites data the
     * previous one is still reading. */
    create_backing_buffer(c, ring_cap,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &c->ring_buf, &c->ring_mem, &c->ring_addr, "vkmin.ring");
    void *mapped = NULL;
    VK_CHECK(vkMapMemory(c->dev, c->ring_mem, 0, ring_cap, 0, &mapped));
    c->ring_mapped = mapped;
    c->ring_cap = ring_cap;
    c->ring_region = ring_cap / c->frames_in_flight;
}

/* ----------------------------------------------------- immediate submit --- */

static VkCommandBuffer imm_begin(vkmin_ctx *c) {
    VKMIN_ASSERT(!c->in_frame, "immediate submit inside a frame");
    VK_CHECK(vkResetCommandBuffer(c->imm_cmd, 0));
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(c->imm_cmd, &begin));
    return c->imm_cmd;
}

static void imm_end(vkmin_ctx *c) {
    VK_CHECK(vkEndCommandBuffer(c->imm_cmd));
    const VkCommandBufferSubmitInfo cmd_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = c->imm_cmd};
    const VkSubmitInfo2 submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                                  .commandBufferInfoCount = 1,
                                  .pCommandBufferInfos = &cmd_info};
    VK_CHECK(vkResetFences(c->dev, 1, &c->imm_fence));
    VK_CHECK(vkQueueSubmit2(c->queue, 1, &submit, c->imm_fence));
    VK_CHECK(vkWaitForFences(c->dev, 1, &c->imm_fence, VK_TRUE, UINT64_MAX));
}

/* Uploads go through the ring in chunks. Only legal outside a frame; if frames
 * have been submitted the ring may still be in use, so wait for them first.
 * Uploads are an init-time activity and this path is not in any budget. */
static void upload_prepare(vkmin_ctx *c) {
    VKMIN_ASSERT(!c->in_frame, "upload inside a frame");
    if (c->have_submitted) VK_CHECK(vkDeviceWaitIdle(c->dev));
}

/* -------------------------------------------------------------- formats --- */

typedef struct {
    VkFormat vk;
    uint32_t block_bytes;
    uint32_t block_dim; /* 1 for uncompressed, 4 for BC */
    VkImageAspectFlags aspect;
} format_info;

static format_info format_lookup(vkmin_format f) {
    switch (f) {
    case VKMIN_FMT_RGBA8_UNORM: return (format_info){VK_FORMAT_R8G8B8A8_UNORM, 4, 1, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_RGBA8_SRGB: return (format_info){VK_FORMAT_R8G8B8A8_SRGB, 4, 1, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_BGRA8_UNORM: return (format_info){VK_FORMAT_B8G8R8A8_UNORM, 4, 1, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_BC1_SRGB: return (format_info){VK_FORMAT_BC1_RGB_SRGB_BLOCK, 8, 4, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_BC1_UNORM: return (format_info){VK_FORMAT_BC1_RGB_UNORM_BLOCK, 8, 4, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_BC3_SRGB: return (format_info){VK_FORMAT_BC3_SRGB_BLOCK, 16, 4, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_BC4_UNORM: return (format_info){VK_FORMAT_BC4_UNORM_BLOCK, 8, 4, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_BC5_UNORM: return (format_info){VK_FORMAT_BC5_UNORM_BLOCK, 16, 4, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_R11G11B10_FLOAT: return (format_info){VK_FORMAT_B10G11R11_UFLOAT_PACK32, 4, 1, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_RGBA16_FLOAT: return (format_info){VK_FORMAT_R16G16B16A16_SFLOAT, 8, 1, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_D32_FLOAT: return (format_info){VK_FORMAT_D32_SFLOAT, 4, 1, VK_IMAGE_ASPECT_DEPTH_BIT};
    case VKMIN_FMT_NONE:
    case VKMIN_FMT_COUNT: break;
    }
    VKMIN_FAIL("bad vkmin_format %d", (int)f);
}

static size_t mip_bytes(format_info fi, uint32_t w, uint32_t h) {
    const uint32_t bw = (w + fi.block_dim - 1) / fi.block_dim;
    const uint32_t bh = (h + fi.block_dim - 1) / fi.block_dim;
    return (size_t)bw * bh * fi.block_bytes;
}

/* ----------------------------------------------------- image transitions -- */

typedef struct {
    VkImageLayout layout;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
} use_info;

/* The one table that says what each use means. Every image barrier in the
 * codebase is derived from two rows of it. */
static use_info use_lookup(vkmin_use use, VkImageAspectFlags aspect) {
    switch (use) {
    case VKMIN_USE_UNDEFINED:
        return (use_info){VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0};
    /* ALL_TRANSFER, not COPY: the backbuffer is read by a copy on one path
     * and a blit on the other, and both are transfers. */
    case VKMIN_USE_TRANSFER_DST:
        return (use_info){VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case VKMIN_USE_TRANSFER_SRC:
        return (use_info){VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                          VK_ACCESS_2_TRANSFER_READ_BIT};
    case VKMIN_USE_SAMPLED:
        return (use_info){VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case VKMIN_USE_COLOR_TARGET:
        return (use_info){VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    case VKMIN_USE_DEPTH_TARGET:
        return (use_info){VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case VKMIN_USE_PRESENT:
        return (use_info){VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
    }
    (void)aspect;
    VKMIN_FAIL("bad vkmin_use %d", (int)use);
}

/* Fills one VkImageMemoryBarrier2 for a slot moving to `use`, and records the
 * move. `discard` keeps the source scope but throws the contents away, which
 * is what every cleared attachment wants. */
static VkImageMemoryBarrier2 slot_transition(image_slot *s, vkmin_use use, bool discard) {
    const use_info from = use_lookup(s->use, s->aspect);
    const use_info to = use_lookup(use, s->aspect);
    const VkImageMemoryBarrier2 b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = from.stage,
        .srcAccessMask = from.access,
        .dstStageMask = to.stage,
        .dstAccessMask = to.access,
        .oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : from.layout,
        .newLayout = to.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = s->img,
        .subresourceRange = {.aspectMask = s->aspect,
                             .levelCount = VK_REMAINING_MIP_LEVELS,
                             .layerCount = 1},
    };
    s->use = use;
    return b;
}

static void cmd_transition(VkCommandBuffer cmd, image_slot *s, vkmin_use use, bool discard) {
    const VkImageMemoryBarrier2 b = slot_transition(s, use, discard);
    const VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                  .imageMemoryBarrierCount = 1,
                                  .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cmd, &dep);
}

/* ------------------------------------------------------------- bindless --- */

static void create_bindless(vkmin_ctx *c) {
    /* Five presets. Users pick one by enum and never see a VkSampler. */
    const struct {
        VkFilter filter;
        VkSamplerAddressMode address;
        float aniso;
        bool compare;
        const char *name;
    } presets[VKMIN_SAMPLER_COUNT] = {
        {VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1.0f, false, "linear_repeat"},
        {VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, false, "linear_clamp"},
        {VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, false, "nearest_clamp"},
        {VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, 8.0f, false, "aniso_repeat"},
        {VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1.0f, true, "shadow"},
    };
    for (uint32_t i = 0; i < VKMIN_SAMPLER_COUNT; ++i) {
        const VkSamplerCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = presets[i].filter,
            .minFilter = presets[i].filter,
            .mipmapMode = presets[i].filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                : VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = presets[i].address,
            .addressModeV = presets[i].address,
            .addressModeW = presets[i].address,
            .anisotropyEnable = presets[i].aniso > 1.0f,
            .maxAnisotropy = presets[i].aniso,
            .compareEnable = presets[i].compare,
            .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        };
        VK_CHECK(vkCreateSampler(c->dev, &info, NULL, &c->samplers[i]));
        set_name(c, VK_OBJECT_TYPE_SAMPLER, (uint64_t)c->samplers[i], "vkmin.sampler.%s",
                 presets[i].name);
    }

    /* One binding: a big, partially bound, update-after-bind array of
     * combined image samplers. Shaders declare it as sampler2D and as
     * sampler2DShadow over the same binding and index it with nonuniformEXT. */
    const VkDescriptorBindingFlags binding_flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    const VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 1,
        .pBindingFlags = &binding_flags,
    };
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = VKMIN_MAX_TEXTURES,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT |
                      VK_SHADER_STAGE_VERTEX_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo linfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flags_info,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(c->dev, &linfo, NULL, &c->set_layout));

    const VkDescriptorPoolSize size = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .descriptorCount = VKMIN_MAX_TEXTURES};
    const VkDescriptorPoolCreateInfo pinfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &size,
    };
    VK_CHECK(vkCreateDescriptorPool(c->dev, &pinfo, NULL, &c->desc_pool));
    const VkDescriptorSetAllocateInfo ainfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = c->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &c->set_layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(c->dev, &ainfo, &c->set));
    set_name(c, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)c->set, "vkmin.textures");

    /* One pipeline layout for everything: the texture set plus one push
     * constant block. Every pipeline in the codebase is created against it. */
    const VkPushConstantRange push = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                      VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = VKMIN_PUSH_BYTES,
    };
    const VkPipelineLayoutCreateInfo plinfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &c->set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    VK_CHECK(vkCreatePipelineLayout(c->dev, &plinfo, NULL, &c->pipe_layout));
    set_name(c, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)c->pipe_layout, "vkmin.layout");
}

uint32_t vkmin_register_texture(vkmin_ctx *c, vkmin_image img, uint32_t sampler_preset) {
    VKMIN_ASSERT(c != NULL, "vkmin_register_texture: null context");
    VKMIN_ASSERT(sampler_preset < VKMIN_SAMPLER_COUNT, "bad sampler preset %u", sampler_preset);
    VKMIN_ASSERT(c->texture_count < VKMIN_MAX_TEXTURES, "bindless texture array full (%u)",
                 (unsigned)VKMIN_MAX_TEXTURES);
    const image_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, img.id, s);
    const uint32_t index = c->texture_count++;
    const rec_draw rr = {.pipe = img.id, .a = sampler_preset, .b = index};
    RECORD(c, OP_REGISTER, rr, NULL, 0);
    const VkDescriptorImageInfo info = {
        .sampler = c->samplers[sampler_preset],
        .imageView = s->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = c->set,
        .dstBinding = 0,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &info,
    };
    vkUpdateDescriptorSets(c->dev, 1, &write, 0, NULL);
    return index;
}

/* -------------------------------------------------------------- targets --- */

static void create_backbuffer_view(vkmin_ctx *c, VkImage img, VkFormat fmt, VkImageView *view,
                                   const char *label) {
    const VkImageViewCreateInfo vinfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    VK_CHECK(vkCreateImageView(c->dev, &vinfo, NULL, view));
    set_name(c, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)*view, "%s.view", label);
}

/* The backbuffer is always an image we own, in both modes and on both paths.
 * Windowed frames blit it to the swapchain image, so nothing is ever captured
 * from a swapchain image and there is no BGRA special case anywhere. */
static void create_offscreen(vkmin_ctx *c) {
    c->backbuffer_format = VK_FORMAT_R8G8B8A8_UNORM;
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = c->backbuffer_format,
        .extent = {c->extent.width, c->extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 (c->path == VKMIN_PATH_MODERN ? VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT : 0u),
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(c->dev, &info, NULL, &c->offscreen_img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c->dev, c->offscreen_img, &req);
    VKMIN_ASSERT(req.memoryTypeBits & (1u << c->img_arena.type), "offscreen image rejects the image arena");
    const VkDeviceSize off = arena_alloc(&c->img_arena, req.size, req.alignment, "image");
    VK_CHECK(vkBindImageMemory(c->dev, c->offscreen_img, c->img_arena.mem, off));
    set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)c->offscreen_img, "vkmin.offscreen");
    create_backbuffer_view(c, c->offscreen_img, c->backbuffer_format, &c->offscreen_view,
                           "vkmin.offscreen");
}

/* --- legacy-only: staging readback --------------------------------------- */
static void create_readback_buffers(vkmin_ctx *c) {
    c->readback_size = (VkDeviceSize)c->extent.width * c->extent.height * 4u;
    if (c->path != VKMIN_PATH_LEGACY) return;
    for (uint32_t i = 0; i < VKMIN_MAX_FRAMES; ++i) {
        create_backing_buffer(c, c->readback_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &c->readback_buf[i], &c->readback_mem[i], NULL, "vkmin.readback");
        VK_CHECK(vkMapMemory(c->dev, c->readback_mem[i], 0, c->readback_size, 0,
                             &c->readback_mapped[i]));
    }
}

static void destroy_readback_buffers(vkmin_ctx *c) {
    for (uint32_t i = 0; i < VKMIN_MAX_FRAMES; ++i) {
        if (!c->readback_buf[i]) continue;
        vkUnmapMemory(c->dev, c->readback_mem[i]);
        vkDestroyBuffer(c->dev, c->readback_buf[i], NULL);
        vkFreeMemory(c->dev, c->readback_mem[i], NULL);
        c->readback_buf[i] = VK_NULL_HANDLE;
        c->readback_mem[i] = VK_NULL_HANDLE;
        c->readback_mapped[i] = NULL;
    }
}
/* --- end legacy-only ------------------------------------------------------ */

/* ------------------------------------------------------------ swapchain --- */

static void destroy_swapchain(vkmin_ctx *c) {
    for (uint32_t i = 0; i < c->swap_count; ++i) vkDestroySemaphore(c->dev, c->rendered[i], NULL);
    c->swap_count = 0;
    if (c->swapchain) {
        vkDestroySwapchainKHR(c->dev, c->swapchain, NULL);
        c->swapchain = VK_NULL_HANDLE;
    }
}

static void create_swapchain(vkmin_ctx *c) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->phys, c->surface, &caps));
    VKMIN_ASSERT(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                 "surface does not allow TRANSFER_DST on swapchain images; vkmin blits into them");

    int fb_w = 0, fb_h = 0;
    plat_framebuffer_size(&fb_w, &fb_h);
    VkExtent2D e = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent
                                                          : (VkExtent2D){(uint32_t)fb_w, (uint32_t)fb_h};
    if (e.width < caps.minImageExtent.width) e.width = caps.minImageExtent.width;
    if (e.height < caps.minImageExtent.height) e.height = caps.minImageExtent.height;
    if (e.width > caps.maxImageExtent.width) e.width = caps.maxImageExtent.width;
    if (e.height > caps.maxImageExtent.height) e.height = caps.maxImageExtent.height;
    c->swap_extent = e;

    uint32_t fn = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, c->surface, &fn, NULL));
    VkSurfaceFormatKHR formats[64];
    if (fn > 64) fn = 64;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, c->surface, &fn, formats));
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < fn; ++i) {
        const bool unorm8 = formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
                            formats[i].format == VK_FORMAT_R8G8B8A8_UNORM;
        if (unorm8 && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }
    c->swap_format = chosen.format;

    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!c->desc.vsync) {
        uint32_t pn = 0;
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(c->phys, c->surface, &pn, NULL));
        VkPresentModeKHR modes[8];
        if (pn > 8) pn = 8;
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(c->phys, c->surface, &pn, modes));
        for (uint32_t i = 0; i < pn; ++i) {
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) mode = modes[i];
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR && mode == VK_PRESENT_MODE_FIFO_KHR) mode = modes[i];
        }
    }

    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && images > caps.maxImageCount) images = caps.maxImageCount;
    if (images > VKMIN_MAX_SWAP) images = VKMIN_MAX_SWAP;

    const VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = c->surface,
        .minImageCount = images,
        .imageFormat = chosen.format,
        .imageColorSpace = chosen.colorSpace,
        .imageExtent = c->swap_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = mode,
        .clipped = VK_TRUE,
    };
    VK_CHECK(vkCreateSwapchainKHR(c->dev, &info, NULL, &c->swapchain));

    c->swap_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(c->dev, c->swapchain, &c->swap_count, NULL));
    VKMIN_ASSERT(c->swap_count <= VKMIN_MAX_SWAP, "swapchain returned %u images, cap is %u",
                 c->swap_count, (unsigned)VKMIN_MAX_SWAP);
    VK_CHECK(vkGetSwapchainImagesKHR(c->dev, c->swapchain, &c->swap_count, c->swap_img));
    for (uint32_t i = 0; i < c->swap_count; ++i) {
        set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)c->swap_img[i], "vkmin.swap[%u]", i);
        const VkSemaphoreCreateInfo sinfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(c->dev, &sinfo, NULL, &c->rendered[i]));
        set_name(c, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)c->rendered[i], "vkmin.rendered[%u]", i);
    }
    fprintf(stderr, "vkmin: swapchain %ux%u, %u images, format %d, present mode %d\n",
            c->swap_extent.width, c->swap_extent.height, c->swap_count, (int)chosen.format, (int)mode);
}

/* The single recreate site. Everything else only raises need_recreate. Note
 * that the renderer's own size-dependent images (HDR target, depth) are not
 * this layer's business: it reports the new size and the renderer rebuilds. */
static void recreate_swapchain(vkmin_ctx *c) {
    int w = 0, h = 0;
    plat_framebuffer_size(&w, &h);
    while ((w == 0 || h == 0) && !plat_should_close()) {
        plat_poll();
        plat_framebuffer_size(&w, &h);
    }
    VK_CHECK(vkDeviceWaitIdle(c->dev));
    destroy_swapchain(c);
    create_swapchain(c);
    /* The owned backbuffer keeps its size; the blit scales into the new
     * swapchain extent. c->extent stays the render size. */
    c->extent = (VkExtent2D){(uint32_t)c->desc.width, (uint32_t)c->desc.height};
    c->need_recreate = false;
}

/* ----------------------------------------------------------- lifecycle ---- */

/* The one command-line parser. Every program wants the same flags, so they
 * live here; anything unrecognised is left alone for the program to read. */
static void parse_command_line(vkmin_ctx *c, int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const bool next = i + 1 < argc;
        if (a[0] == '+' && next) {
            char joined[256];
            snprintf(joined, sizeof joined, "%s=%s", a + 1, argv[++i]);
            VKMIN_ASSERT(cvar_parse_assignment(joined), "bad cvar assignment '%s'", joined);
        } else if (a[0] != '-' && strchr(a, '=')) {
            VKMIN_ASSERT(cvar_parse_assignment(a), "bad cvar assignment '%s'", a);
        } else if (!strcmp(a, "--headless")) {
            c->desc.headless = true;
        } else if (!strcmp(a, "--frame") && next) {
            c->desc.headless = true;
            c->frame_count = 1;
            c->frame_list[0] = atoi(argv[++i]);
        } else if (!strcmp(a, "--frames") && next) {
            c->desc.headless = true;
            c->frame_count = 0;
            const char *p = argv[++i];
            while (*p && c->frame_count < VKMIN_MAX_FRAME_LIST) {
                char *end = NULL;
                const long v = strtol(p, &end, 10);
                if (end == p) break;
                c->frame_list[c->frame_count++] = (int)v;
                p = *end == ',' ? end + 1 : end;
            }
        } else if (!strcmp(a, "--out") && next) {
            c->out = argv[++i];
        } else if (!strcmp(a, "--out-dir") && next) {
            c->out_dir = argv[++i];
        } else if (!strcmp(a, "--exit-after") && next) {
            c->exit_after = atoi(argv[++i]);
        } else if (!strcmp(a, "--size") && i + 2 < argc) {
            /* Flags that are spellings of a cvar assignment go through the
             * assignment path so they count as set by the user. */
            char w[64], h[64];
            snprintf(w, sizeof w, "r_width=%d", atoi(argv[++i]));
            snprintf(h, sizeof h, "r_height=%d", atoi(argv[++i]));
            cvar_parse_assignment(w);
            cvar_parse_assignment(h);
        } else if (!strcmp(a, "--path=legacy")) {
            cvar_parse_assignment("r_path=1");
        } else if (!strcmp(a, "--path=modern")) {
            cvar_parse_assignment("r_path=2");
        } else if (!strcmp(a, "--sync-naive")) {
            cvar_parse_assignment("r_sync_naive=1");
        } else if (!strcmp(a, "--no-readback")) {
            cvar_parse_assignment("r_readback=0");
        } else if (!strcmp(a, "--device") && next) {
            c->desc.device_index = atoi(argv[++i]);
        } else if (!strcmp(a, "--record") && next) {
            c->record_path = argv[++i];
        } else if (!strcmp(a, "--replay") && next) {
            c->replay_path = argv[++i];
            c->desc.headless = true;
        } else if (!strcmp(a, "--demo") && next) {
            c->demo_path = argv[++i];
        } else if (!strcmp(a, "--play") && next) {
            c->play_path = argv[++i];
        } else if (!strcmp(a, "--verbose")) {
            c->verbose = true;
        } else if (!strcmp(a, "--cvars")) {
            cvar_print_all();
            exit(0);
        }
        /* anything else belongs to the program */
    }
}

vkmin_ctx *vkmin_init(const vkmin_desc *desc) {
    VKMIN_ASSERT(desc != NULL, "vkmin_init: null desc");
    vkmin_ctx *c = calloc(1, sizeof *c);
    VKMIN_ASSERT(c != NULL, "out of memory");
    c->desc = *desc;
    if (!c->desc.title) c->desc.title = "vkmin";
    if (desc->argv) parse_command_line(c, desc->argc, desc->argv);
    /* Cvars set on the command line win over the desc; the desc wins over the
     * cvar defaults. */
    if (c->desc.width <= 0 || cvar_was_set(CV_r_width)) c->desc.width = cvar_get_int(CV_r_width);
    if (c->desc.height <= 0 || cvar_was_set(CV_r_height)) c->desc.height = cvar_get_int(CV_r_height);
    if (cvar_was_set(CV_r_path)) c->desc.path = (vkmin_path)cvar_get_int(CV_r_path);
    if (cvar_get_bool(CV_r_sync_naive)) c->desc.sync_naive = true;
    if (!cvar_get_bool(CV_r_readback)) c->desc.no_readback = true;
    c->desc.vsync = cvar_get_bool(CV_r_vsync);
    if (c->replay_path) { /* the recording decides the size */
        FILE *f = fopen(c->replay_path, "rb");
        journal_header jh;
        VKMIN_ASSERT(f && fread(&jh, sizeof jh, 1, f) == 1 && jh.magic == 0x4a4d4b56u, "cannot read journal '%s'", c->replay_path);
        fclose(f);
        c->desc.width = (int)jh.width;
        c->desc.height = (int)jh.height;
        c->rec_arena_base = jh.arena_base;
        c->rec_ring_base = jh.ring_base;
        c->replaying = true;
    }
    if (c->play_path) { /* the demo was recorded at a size; mouse positions are in its pixels */
        c->demo_in = fopen(c->play_path, "rb");
        demo_header dh;
        VKMIN_ASSERT(c->demo_in && fread(&dh, sizeof dh, 1, c->demo_in) == 1 && dh.magic == 0x444d4b56u && dh.version == 1,
                     "cannot read demo '%s'", c->play_path);
        c->desc.width = (int)dh.width;
        c->desc.height = (int)dh.height;
    }
    c->frame_last = -1;
    for (int i = 0; i < c->frame_count; ++i) c->frame_last = c->frame_list[i] > c->frame_last ? c->frame_list[i] : c->frame_last;
    if (c->desc.headless && c->frame_count == 0 && !c->replaying && !c->demo_in) { c->frame_count = 1; c->frame_list[0] = 0; }
    VKMIN_ASSERT(c->desc.width > 0 && c->desc.height > 0, "vkmin_init: width and height must be > 0");
    if (c->verbose) cvar_print_all();
#ifdef NDEBUG
    c->debug = false;
#else
    c->debug = true;
#endif
    desc = &c->desc;
    c->extent = (VkExtent2D){(uint32_t)desc->width, (uint32_t)desc->height};
    c->frames_in_flight = desc->sync_naive ? 1u : 2u;

    if (!desc->headless) {
        VKMIN_ASSERT(plat_window_open(desc->width, desc->height, desc->title), "could not open a window");
    }
    create_instance(c);
    if (!desc->headless) {
        c->surface = plat_create_surface(c->instance);
        VKMIN_ASSERT(c->surface != VK_NULL_HANDLE, "could not create a Vulkan surface");
    }
    pick_physical_device(c);
    if (!desc->headless) {
        VkBool32 supported = VK_FALSE;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(c->phys, c->queue_family, c->surface, &supported));
        VKMIN_ASSERT(supported, "queue family %u cannot present to this surface", c->queue_family);
    }
    create_device(c);

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = c->queue_family,
    };
    VK_CHECK(vkCreateCommandPool(c->dev, &pool_info, NULL, &c->cmd_pool));
    VkCommandBuffer buffers[VKMIN_MAX_FRAMES + 1];
    const VkCommandBufferAllocateInfo cbinfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = c->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VKMIN_MAX_FRAMES + 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(c->dev, &cbinfo, buffers));
    for (uint32_t i = 0; i < VKMIN_MAX_FRAMES; ++i) {
        c->cmd[i] = buffers[i];
        const VkFenceCreateInfo finfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(c->dev, &finfo, NULL, &c->fence[i]));
        const VkSemaphoreCreateInfo sinfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(c->dev, &sinfo, NULL, &c->acquired[i]));
        set_name(c, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)c->cmd[i], "vkmin.cmd[%u]", i);
        set_name(c, VK_OBJECT_TYPE_FENCE, (uint64_t)c->fence[i], "vkmin.fence[%u]", i);
        set_name(c, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)c->acquired[i], "vkmin.acquired[%u]", i);
    }
    c->imm_cmd = buffers[VKMIN_MAX_FRAMES];
    const VkFenceCreateInfo finfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(c->dev, &finfo, NULL, &c->imm_fence));
    set_name(c, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)c->imm_cmd, "vkmin.cmd.immediate");

    const VkQueryPoolCreateInfo qinfo = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = VKMIN_MAX_FRAMES * VKMIN_MAX_TIMESTAMPS,
    };
    VK_CHECK(vkCreateQueryPool(c->dev, &qinfo, NULL, &c->query_pool));
    vkResetQueryPool(c->dev, c->query_pool, 0, VKMIN_MAX_FRAMES * VKMIN_MAX_TIMESTAMPS);

    create_memory(c);
    create_bindless(c);
    create_offscreen(c);
    if (!desc->headless) create_swapchain(c);
    create_readback_buffers(c);

    /* Slot 0 is the backbuffer: an external image whose VkImage changes every
     * frame in windowed mode. Its handle is stable for the life of the context. */
    c->images[VKMIN_BACKBUFFER_SLOT] = (image_slot){
        .gen = 1, .used = true, .external = true, .format = c->backbuffer_format,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .w = c->extent.width, .h = c->extent.height, .mips = 1,
        .img = c->offscreen_img, .view = c->offscreen_view, .use = VKMIN_USE_UNDEFINED,
        .tex_index = UINT32_MAX, .label = "backbuffer"};
    c->default_depth = vkmin_make_image(c, &(vkmin_image_desc){.width = desc->width, .height = desc->height,
                                                               .format = VKMIN_FMT_D32_FLOAT, .usage = VKMIN_IMAGE_DEPTH,
                                                               .label = "vkmin.default_depth"});
    if (c->record_path) { /* everything the program does from here is recorded */
        c->rec = fopen(c->record_path, "wb");
        VKMIN_ASSERT(c->rec != NULL, "cannot write journal '%s'", c->record_path);
        const journal_header jh = {0x4a4d4b56u, 1, (uint32_t)desc->width, (uint32_t)desc->height, c->arena_addr, c->ring_addr};
        VKMIN_ASSERT(fwrite(&jh, sizeof jh, 1, c->rec) == 1, "journal write failed");
        fprintf(stderr, "vkmin: recording to %s\n", c->record_path);
    }
    if (c->demo_path) {
        c->demo_out = fopen(c->demo_path, "wb");
        VKMIN_ASSERT(c->demo_out != NULL, "cannot write demo '%s'", c->demo_path);
        const demo_header dh = {0x444d4b56u, 1, (uint32_t)desc->width, (uint32_t)desc->height};
        VKMIN_ASSERT(fwrite(&dh, sizeof dh, 1, c->demo_out) == 1, "demo write failed");
    }
    return c;
}

void vkmin_shutdown(vkmin_ctx *c) {
    if (!c) return;
    if (c->rec) fclose(c->rec);
    if (c->demo_out) fclose(c->demo_out);
    if (c->demo_in) fclose(c->demo_in);
    VK_CHECK(vkDeviceWaitIdle(c->dev));
    for (uint32_t i = 0; i < VKMIN_MAX_PIPES; ++i) {
        if (c->pipes[i].used) vkDestroyPipeline(c->dev, c->pipes[i].pipe, NULL);
        for (int k = 0; k < 3; ++k) free(c->pipes[i].loaded[k]);
    }
    for (uint32_t i = 0; i < VKMIN_MAX_IMAGES; ++i) {
        if (!c->images[i].used || c->images[i].external) continue;
        vkDestroyImageView(c->dev, c->images[i].view, NULL);
        vkDestroyImage(c->dev, c->images[i].img, NULL);
    }
    if (c->offscreen_img) {
        vkDestroyImageView(c->dev, c->offscreen_view, NULL);
        vkDestroyImage(c->dev, c->offscreen_img, NULL);
    }
    destroy_readback_buffers(c);
    destroy_swapchain(c);
    for (uint32_t i = 0; i < VKMIN_SAMPLER_COUNT; ++i) vkDestroySampler(c->dev, c->samplers[i], NULL);
    vkDestroyDescriptorPool(c->dev, c->desc_pool, NULL);
    vkDestroyDescriptorSetLayout(c->dev, c->set_layout, NULL);
    vkDestroyPipelineLayout(c->dev, c->pipe_layout, NULL);
    vkUnmapMemory(c->dev, c->ring_mem);
    vkDestroyBuffer(c->dev, c->ring_buf, NULL);
    vkFreeMemory(c->dev, c->ring_mem, NULL);
    vkDestroyBuffer(c->dev, c->arena_buf, NULL);
    vkFreeMemory(c->dev, c->buf_arena.mem, NULL);
    vkFreeMemory(c->dev, c->img_arena.mem, NULL);
    vkDestroyQueryPool(c->dev, c->query_pool, NULL);
    for (uint32_t i = 0; i < VKMIN_MAX_FRAMES; ++i) {
        vkDestroyFence(c->dev, c->fence[i], NULL);
        vkDestroySemaphore(c->dev, c->acquired[i], NULL);
    }
    vkDestroyFence(c->dev, c->imm_fence, NULL);
    vkDestroyCommandPool(c->dev, c->cmd_pool, NULL);
    vkDestroyDevice(c->dev, NULL);
    if (c->surface) vkDestroySurfaceKHR(c->instance, c->surface, NULL);
    if (c->messenger) c->fp_destroy_messenger(c->instance, c->messenger, NULL);
    vkDestroyInstance(c->instance, NULL);
    if (!c->desc.headless) plat_close();
    free(c);
}

void vkmin_size(const vkmin_ctx *c, int *w, int *h) {
    VKMIN_ASSERT(c && w && h, "vkmin_size: null argument");
    *w = (int)c->extent.width;
    *h = (int)c->extent.height;
}

const vkmin_inputs *vkmin_input(const vkmin_ctx *c) { return &c->input; }

bool vkmin_key_hit(const vkmin_ctx *c, int key) {
    VKMIN_ASSERT(c != NULL && key >= 0 && key < (int)VKMIN_KEY_COUNT, "vkmin_key_hit: bad argument");
    return vkmin_key_pressed(&c->input, key) != 0u;
}

uint32_t vkmin_frame_slot(const vkmin_ctx *c) { return c->slot; }

/* ------------------------------------------------------------- buffers ---- */

vkmin_buffer vkmin_make_buffer(vkmin_ctx *c, const vkmin_buffer_desc *desc) {
    VKMIN_ASSERT(c && desc && desc->size > 0, "vkmin_make_buffer: bad argument");
    RECORD_ENTER(c);
    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->buffers, VKMIN_MAX_BUFFERS, index);
    buffer_slot *s = &c->buffers[index];
    s->size = desc->size;
    snprintf(s->label, sizeof s->label, "%s", desc->label ? desc->label : "buffer");
    s->offset = arena_alloc(&c->buf_arena, desc->size, VKMIN_ARENA_ALIGN, s->label);
    const vkmin_buffer b = {handle_make(index, s->gen)};
    if (desc->data) vkmin_buffer_upload(c, b, 0, desc->data, desc->size);
    RECORD_LEAVE(c);
    rec_buffer rb = {.size = desc->size, .result = b.id, .has_data = desc->data != NULL};
    snprintf(rb.label, sizeof rb.label, "%s", s->label);
    RECORD(c, OP_MAKE_BUFFER, rb, desc->data, desc->data ? desc->size : 0);
    return b;
}

void vkmin_free_buffer(vkmin_ctx *c, vkmin_buffer b) {
    VKMIN_ASSERT(c && !c->in_frame, "vkmin_free_buffer: call it between frames");
    RECORD(c, OP_FREE_BUFFER, b, NULL, 0);
    buffer_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, b.id, s);
    VK_CHECK(vkDeviceWaitIdle(c->dev));
    const uint16_t gen = gen_next(s->gen);
    *s = (buffer_slot){.gen = gen}; /* the range in the arena stays allocated; see vkmin.h */
}

void vkmin_free_image(vkmin_ctx *c, vkmin_image img) {
    VKMIN_ASSERT(c && !c->in_frame, "vkmin_free_image: call it between frames");
    RECORD(c, OP_FREE_IMAGE, img, NULL, 0);
    image_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, img.id, s);
    VKMIN_ASSERT(!s->external, "the backbuffer cannot be freed");
    VK_CHECK(vkDeviceWaitIdle(c->dev));
    vkDestroyImageView(c->dev, s->view, NULL);
    vkDestroyImage(c->dev, s->img, NULL);
    const uint16_t gen = gen_next(s->gen);
    *s = (image_slot){.gen = gen};
}

uint64_t vkmin_address(vkmin_ctx *c, vkmin_buffer b) {
    const buffer_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, b.id, s);
    return c->arena_addr + s->offset;
}

void vkmin_buffer_upload(vkmin_ctx *c, vkmin_buffer b, size_t offset, const void *data, size_t bytes) {
    VKMIN_ASSERT(c && data, "vkmin_buffer_upload: null argument");
    const buffer_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, b.id, s);
    VKMIN_ASSERT(offset + bytes <= s->size, "upload of %zu bytes at %zu overruns a %llu byte buffer",
                 bytes, offset, (unsigned long long)s->size);
    const rec_upload ru = {.id = b.id, .offset = offset};
    RECORD(c, OP_BUFFER_UPLOAD, ru, data, bytes);
    upload_prepare(c);
    const uint8_t *src = data;
    size_t done = 0;
    while (done < bytes) {
        const size_t chunk = bytes - done < c->ring_cap ? bytes - done : (size_t)c->ring_cap;
        memcpy(c->ring_mapped, src + done, chunk);
        VkCommandBuffer cmd = imm_begin(c);
        const VkBufferCopy copy = {.srcOffset = 0, .dstOffset = s->offset + offset + done, .size = chunk};
        vkCmdCopyBuffer(cmd, c->ring_buf, c->arena_buf, 1, &copy);
        imm_end(c);
        done += chunk;
    }
}

/* -------------------------------------------------------------- images ---- */

vkmin_image vkmin_make_image(vkmin_ctx *c, const vkmin_image_desc *desc) {
    VKMIN_ASSERT(c && desc && desc->width > 0 && desc->height > 0, "vkmin_make_image: bad argument");
    VKMIN_ASSERT(desc->sampler < VKMIN_SAMPLER_COUNT, "bad sampler preset %u", desc->sampler);
    RECORD_ENTER(c);
    const format_info fi = format_lookup(desc->format);
    const uint32_t usage_bits = desc->usage ? desc->usage : (uint32_t)VKMIN_IMAGE_SAMPLED;
    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->images, VKMIN_MAX_IMAGES, index);
    image_slot *s = &c->images[index];
    snprintf(s->label, sizeof s->label, "%s", desc->label ? desc->label : "image");
    s->tex_index = UINT32_MAX;
    s->sampler = desc->sampler;
    s->format = fi.vk;
    s->aspect = fi.aspect;
    s->w = (uint32_t)desc->width;
    s->h = (uint32_t)desc->height;
    s->mips = desc->mip_levels > 0 ? (uint32_t)desc->mip_levels : 1u;
    s->use = VKMIN_USE_UNDEFINED;
    s->external = false;

    VkImageUsageFlags usage = 0;
    if (usage_bits & VKMIN_IMAGE_SAMPLED) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    /* Only images that get uploaded need a transfer destination: sampled ones
     * that are not attachments. On the modern path that is a host transfer. */
    const bool uploadable = (usage_bits & VKMIN_IMAGE_SAMPLED) && !(usage_bits & (VKMIN_IMAGE_COLOR | VKMIN_IMAGE_DEPTH));
    if (uploadable) usage |= c->path == VKMIN_PATH_MODERN ? VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT : VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage_bits & VKMIN_IMAGE_COLOR) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage_bits & VKMIN_IMAGE_DEPTH) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fi.vk,
        .extent = {s->w, s->h, 1},
        .mipLevels = s->mips,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(c->dev, &info, NULL, &s->img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c->dev, s->img, &req);
    VKMIN_ASSERT(req.memoryTypeBits & (1u << c->img_arena.type), "image '%s' rejects the image arena",
                 desc->label ? desc->label : "?");
    const VkDeviceSize off = arena_alloc(&c->img_arena, req.size, req.alignment,
                                         desc->label ? desc->label : "image");
    VK_CHECK(vkBindImageMemory(c->dev, s->img, c->img_arena.mem, off));

    const VkImageViewCreateInfo vinfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = s->img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fi.vk,
        .subresourceRange = {.aspectMask = fi.aspect, .levelCount = s->mips, .layerCount = 1},
    };
    VK_CHECK(vkCreateImageView(c->dev, &vinfo, NULL, &s->view));
    set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)s->img, "%s", s->label);
    set_name(c, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)s->view, "%s.view", s->label);
    const vkmin_image img = {handle_make(index, s->gen)};
    if (desc->pixels) vkmin_image_upload(c, img, 0, desc->pixels, mip_bytes(fi, s->w, s->h));
    RECORD_LEAVE(c);
    rec_image ri = {.w = desc->width, .h = desc->height, .mips = desc->mip_levels, .format = desc->format, .usage = desc->usage,
                    .sampler = desc->sampler, .result = img.id, .has_pixels = desc->pixels != NULL};
    snprintf(ri.label, sizeof ri.label, "%s", s->label);
    RECORD(c, OP_MAKE_IMAGE, ri, desc->pixels, desc->pixels ? mip_bytes(fi, s->w, s->h) : 0);
    return img;
}

uint32_t vkmin_index(vkmin_ctx *c, vkmin_image img) {
    image_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, img.id, s);
    if (s->tex_index == UINT32_MAX) {
        RECORD_ENTER(c);
        s->tex_index = vkmin_register_texture(c, img, s->sampler);
        RECORD_LEAVE(c);
        const rec_draw ri = {.pipe = img.id, .a = s->tex_index};
        RECORD(c, OP_INDEX, ri, NULL, 0);
    }
    return s->tex_index;
}

/* --- legacy-only: upload through the ring and a command buffer ----------- */
static void legacy_image_upload(vkmin_ctx *c, image_slot *s, uint32_t mip, uint32_t mw, uint32_t mh,
                                const void *data, size_t bytes) {
    VKMIN_ASSERT(bytes <= c->ring_cap, "single mip larger than the host ring");
    upload_prepare(c);
    memcpy(c->ring_mapped, data, bytes);
    VkCommandBuffer cmd = imm_begin(c);
    /* Uniform path: each mip lands, then the whole image goes back to SAMPLED.
     * Redundant per mip, correct in every order, and only ever at init. */
    cmd_transition(cmd, s, VKMIN_USE_TRANSFER_DST, false);
    const VkBufferImageCopy copy = {
        .imageSubresource = {.aspectMask = s->aspect, .mipLevel = mip, .layerCount = 1},
        .imageExtent = {mw, mh, 1},
    };
    vkCmdCopyBufferToImage(cmd, c->ring_buf, s->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    cmd_transition(cmd, s, VKMIN_USE_SAMPLED, false);
    imm_end(c);
}
/* --- end legacy-only ------------------------------------------------------ */

/* --- modern-only: host image copy straight into the image ---------------- */
static void modern_image_upload(vkmin_ctx *c, image_slot *s, uint32_t mip, uint32_t mw, uint32_t mh,
                                const void *data, size_t bytes) {
    (void)bytes;
    upload_prepare(c);
    /* Layout transitions happen on the host too: no command buffer anywhere. */
    const VkHostImageLayoutTransitionInfoEXT to_general = {
        .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT,
        .image = s->img,
        .oldLayout = use_lookup(s->use, s->aspect).layout,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .subresourceRange = {.aspectMask = s->aspect, .levelCount = VK_REMAINING_MIP_LEVELS, .layerCount = 1},
    };
    VK_CHECK(c->fp_transition_image_layout(c->dev, 1, &to_general));
    const VkMemoryToImageCopyEXT region = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,
        .pHostPointer = data,
        .imageSubresource = {.aspectMask = s->aspect, .mipLevel = mip, .layerCount = 1},
        .imageExtent = {mw, mh, 1},
    };
    const VkCopyMemoryToImageInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,
        .dstImage = s->img,
        .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1,
        .pRegions = &region,
    };
    VK_CHECK(c->fp_copy_memory_to_image(c->dev, &info));
    const VkHostImageLayoutTransitionInfoEXT to_sampled = {
        .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT,
        .image = s->img,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .subresourceRange = {.aspectMask = s->aspect, .levelCount = VK_REMAINING_MIP_LEVELS, .layerCount = 1},
    };
    VK_CHECK(c->fp_transition_image_layout(c->dev, 1, &to_sampled));
    s->use = VKMIN_USE_SAMPLED;
}
/* --- end modern-only ------------------------------------------------------ */

void vkmin_image_upload(vkmin_ctx *c, vkmin_image img, int mip, const void *data, size_t bytes) {
    VKMIN_ASSERT(c && data, "vkmin_image_upload: null argument");
    image_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, img.id, s);
    VKMIN_ASSERT(mip >= 0 && (uint32_t)mip < s->mips, "mip %d out of range (%u levels)", mip, s->mips);
    const uint32_t mw = (s->w >> mip) ? (s->w >> mip) : 1u;
    const uint32_t mh = (s->h >> mip) ? (s->h >> mip) : 1u;
    /* The format table knows the exact byte count; a caller with the wrong
     * one has a mip-chain bug and should hear about it now. */
    format_info fi = {.vk = s->format, .aspect = s->aspect, .block_bytes = 4, .block_dim = 1};
    for (int f = 0; f < (int)VKMIN_FMT_NONE; ++f) {
        const format_info cand = format_lookup((vkmin_format)f);
        if (cand.vk == s->format) fi = cand;
    }
    VKMIN_ASSERT(bytes == mip_bytes(fi, mw, mh), "mip %d of a %ux%u image needs %zu bytes, got %zu",
                 mip, mw, mh, mip_bytes(fi, mw, mh), bytes);
    const rec_upload ru = {.id = img.id, .mip = (uint32_t)mip};
    RECORD(c, OP_IMAGE_UPLOAD, ru, data, bytes);
    /* Seam 1 of 3, write side. */
    if (c->path == VKMIN_PATH_LEGACY) legacy_image_upload(c, s, (uint32_t)mip, mw, mh, data, bytes);
    else modern_image_upload(c, s, (uint32_t)mip, mw, mh, data, bytes);
}

vkmin_image vkmin_load_png(vkmin_ctx *c, const char *path, bool srgb) {
    VKMIN_ASSERT(c && path, "vkmin_load_png: null argument");
    int w = 0, h = 0;
    unsigned char *pixels = vkmin_png_load(path, &w, &h);
    VKMIN_ASSERT(pixels != NULL, "could not load PNG '%s'", path);
    const vkmin_image img = vkmin_make_image(
        c, &(vkmin_image_desc){.width = w, .height = h, .pixels = pixels,
                               .format = srgb ? VKMIN_FMT_RGBA8_SRGB : VKMIN_FMT_RGBA8_UNORM, .label = path});
    vkmin_png_free(pixels);
    return img;
}

vkmin_format vkmin_backbuffer_format(const vkmin_ctx *c) {
    (void)c;
    return VKMIN_FMT_RGBA8_UNORM;
}

vkmin_image vkmin_default_depth(const vkmin_ctx *c) { return c->default_depth; }

vkmin_image vkmin_backbuffer(const vkmin_ctx *c) {
    return (vkmin_image){handle_make(VKMIN_BACKBUFFER_SLOT, c->images[VKMIN_BACKBUFFER_SLOT].gen)};
}

/* ------------------------------------------------------------ pipelines --- */
static long file_mtime(const char *path);

/* Seam 3 of 3: a shader stage. The legacy path creates a transient module
 * and destroys it once the pipeline exists; the modern path (maintenance5)
 * chains the SPIR-V into the stage and never has a module object. Both are
 * complete implementations of "make a stage"; the caller destroys whatever
 * `module` comes back non-null. */
typedef struct {
    VkPipelineShaderStageCreateInfo stage;
    VkShaderModuleCreateInfo inline_code; /* referenced by stage.pNext on the modern path */
    VkShaderModule module;                /* legacy only */
} shader_stage;

static void check_spirv(const uint32_t *spv, size_t bytes, const char *label) {
    VKMIN_ASSERT(spv && bytes >= 4 && bytes % 4 == 0, "'%s': bad SPIR-V size %zu", label, bytes);
    VKMIN_ASSERT(spv[0] == 0x07230203u, "'%s': SPIR-V magic missing", label);
}

/* --- legacy-only: transient shader modules ------------------------------- */
static void legacy_make_stage(vkmin_ctx *c, shader_stage *s, VkShaderStageFlagBits kind, const uint32_t *spv,
                              size_t bytes, const char *label) {
    check_spirv(spv, bytes, label);
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = spv};
    VK_CHECK(vkCreateShaderModule(c->dev, &info, NULL, &s->module));
    s->stage = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = kind, .module = s->module, .pName = "main"};
}
/* --- end legacy-only ------------------------------------------------------ */

/* --- modern-only: SPIR-V inline in the stage ----------------------------- */
static void modern_make_stage(vkmin_ctx *c, shader_stage *s, VkShaderStageFlagBits kind, const uint32_t *spv,
                              size_t bytes, const char *label) {
    (void)c;
    check_spirv(spv, bytes, label);
    s->module = VK_NULL_HANDLE;
    s->inline_code = (VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = spv};
    s->stage = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .pNext = &s->inline_code, .stage = kind, .pName = "main"};
}
/* --- end modern-only ------------------------------------------------------ */

static void make_stage(vkmin_ctx *c, shader_stage *s, VkShaderStageFlagBits kind, const uint32_t *spv,
                       size_t bytes, const char *label) {
    if (c->path == VKMIN_PATH_LEGACY) legacy_make_stage(c, s, kind, spv, bytes, label);
    else modern_make_stage(c, s, kind, spv, bytes, label);
}

/* Debug builds ask for robustBufferAccess2 per pipeline where the device has
 * it: the GPU-side counterpart of the bounds-checked handle lookup, fatal in
 * debug and free in release. A property of the build, not a path. */
static const void *robustness_chain(const vkmin_ctx *c, VkPipelineRobustnessCreateInfoEXT *info, const void *next) {
    if (!(c->debug && c->caps.pipeline_robustness && c->caps.robust_buffer_access2)) return next;
    *info = (VkPipelineRobustnessCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO_EXT,
        .pNext = next,
        .storageBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT,
        .uniformBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT,
        .vertexInputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT,
        .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DEVICE_DEFAULT_EXT,
    };
    return info;
}

static VkCompareOp compare_lookup(vkmin_compare cmp) {
    switch (cmp) {
    case VKMIN_CMP_LESS: return VK_COMPARE_OP_LESS;
    case VKMIN_CMP_LESS_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case VKMIN_CMP_EQUAL: return VK_COMPARE_OP_EQUAL;
    case VKMIN_CMP_ALWAYS: return VK_COMPARE_OP_ALWAYS;
    }
    VKMIN_FAIL("bad compare %d", (int)cmp);
}

static vkmin_pipe make_compute(vkmin_ctx *c, const vkmin_pipe_desc *desc, const char *label);

static vkmin_pipe make_graphics(vkmin_ctx *c, const vkmin_pipe_desc *desc, const char *label);

vkmin_pipe vkmin_make_pipeline(vkmin_ctx *c, const vkmin_pipe_desc *desc) {
    VKMIN_ASSERT(c && desc && (desc->vs || desc->cs), "vkmin_make_pipeline: needs .vs or .cs");
    const char *label = desc->label ? desc->label : "pipeline";
    const vkmin_pipe p = desc->cs ? make_compute(c, desc, label) : make_graphics(c, desc, label);
    if (c->rec && c->rec_depth == 0) {
        /* Blobs concatenated: vs, fs, cs. */
        const size_t total = desc->vs_bytes + desc->fs_bytes + desc->cs_bytes;
        uint8_t *blob = malloc(total ? total : 1);
        VKMIN_ASSERT(blob != NULL, "out of memory");
        size_t at = 0;
        if (desc->vs) { memcpy(blob + at, desc->vs, desc->vs_bytes); at += desc->vs_bytes; }
        if (desc->fs) { memcpy(blob + at, desc->fs, desc->fs_bytes); at += desc->fs_bytes; }
        if (desc->cs) { memcpy(blob + at, desc->cs, desc->cs_bytes); at += desc->cs_bytes; }
        rec_pipe rp = {.vs_bytes = (uint32_t)(desc->vs ? desc->vs_bytes : 0), .fs_bytes = (uint32_t)(desc->fs ? desc->fs_bytes : 0),
                       .cs_bytes = (uint32_t)(desc->cs ? desc->cs_bytes : 0), .color_format = desc->color_format, .depth = desc->depth,
                       .depth_write = desc->depth_write, .compare = desc->depth_compare, .cull = desc->cull,
                       .blend = desc->blend, .bias = desc->depth_bias, .result = p.id};
        snprintf(rp.label, sizeof rp.label, "%s", label);
        RECORD(c, OP_MAKE_PIPELINE, rp, blob, at);
        free(blob);
    }
    return p;
}

static vkmin_pipe make_graphics(vkmin_ctx *c, const vkmin_pipe_desc *desc, const char *label) {
    shader_stage vs = {0}, fs = {0};
    make_stage(c, &vs, VK_SHADER_STAGE_VERTEX_BIT, desc->vs, desc->vs_bytes, label);
    if (desc->fs) make_stage(c, &fs, VK_SHADER_STAGE_FRAGMENT_BIT, desc->fs, desc->fs_bytes, label);
    const VkPipelineShaderStageCreateInfo stages[2] = {vs.stage, fs.stage};
    /* No vertex input state: every vertex shader pulls from a device address. */
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = desc->depth_bias, /* shadow casters behind the near plane still cast */
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = desc->cull == VKMIN_CULL_NONE ? VK_CULL_MODE_NONE
                    : desc->cull == VKMIN_CULL_FRONT ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = desc->depth_bias,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    /* Backbuffer passes always have the default depth attached, so pipelines
     * that render there must declare it whether or not they test against it. */
    const bool depth_attachment = desc->depth || desc->color_format == VKMIN_FMT_RGBA8_UNORM;
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc->depth,
        .depthWriteEnable = desc->depth && desc->depth_write,
        .depthCompareOp = compare_lookup(desc->depth_compare),
        .maxDepthBounds = 1.0f,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = desc->blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, /* premultiplied alpha */
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const bool has_color = desc->color_format != VKMIN_FMT_NONE;
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = has_color ? 1u : 0u,
        .pAttachments = &blend_attachment};
    const VkDynamicState dynamic_states[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                              VK_DYNAMIC_STATE_DEPTH_BIAS};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = desc->depth_bias ? 3u : 2u,
        .pDynamicStates = dynamic_states};
    const VkFormat color_vk = has_color ? format_lookup(desc->color_format).vk : VK_FORMAT_UNDEFINED;
    const VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = has_color ? 1u : 0u,
        .pColorAttachmentFormats = &color_vk,
        .depthAttachmentFormat = depth_attachment ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
    };
    VkPipelineRobustnessCreateInfoEXT robust;
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = robustness_chain(c, &robust, &rendering),
        .stageCount = desc->fs ? 2u : 1u,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = c->pipe_layout,
    };
    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->pipes, VKMIN_MAX_PIPES, index);
    pipe_slot *s = &c->pipes[index];
    s->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    snprintf(s->label, sizeof s->label, "%s", label);
    s->desc = *desc;
    s->mtime[0] = file_mtime(desc->vs_path);
    s->mtime[1] = file_mtime(desc->fs_path);
    s->mtime[2] = file_mtime(desc->cs_path);
    VK_CHECK(vkCreateGraphicsPipelines(c->dev, VK_NULL_HANDLE, 1, &info, NULL, &s->pipe));
    set_name(c, VK_OBJECT_TYPE_PIPELINE, (uint64_t)s->pipe, "%s", label);
    if (vs.module) vkDestroyShaderModule(c->dev, vs.module, NULL);
    if (fs.module) vkDestroyShaderModule(c->dev, fs.module, NULL);
    return (vkmin_pipe){handle_make(index, s->gen)};
}

static vkmin_pipe make_compute(vkmin_ctx *c, const vkmin_pipe_desc *desc, const char *label) {
    shader_stage cs = {0};
    make_stage(c, &cs, VK_SHADER_STAGE_COMPUTE_BIT, desc->cs, desc->cs_bytes, label);
    VkPipelineRobustnessCreateInfoEXT robust;
    const VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = robustness_chain(c, &robust, NULL),
        .stage = cs.stage,
        .layout = c->pipe_layout,
    };
    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->pipes, VKMIN_MAX_PIPES, index);
    pipe_slot *s = &c->pipes[index];
    s->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    snprintf(s->label, sizeof s->label, "%s", label);
    s->desc = *desc;
    s->mtime[2] = file_mtime(desc->cs_path);
    VK_CHECK(vkCreateComputePipelines(c->dev, VK_NULL_HANDLE, 1, &info, NULL, &s->pipe));
    set_name(c, VK_OBJECT_TYPE_PIPELINE, (uint64_t)s->pipe, "%s", label);
    if (cs.module) vkDestroyShaderModule(c->dev, cs.module, NULL);
    return (vkmin_pipe){handle_make(index, s->gen)};
}

/* ------------------------------------------------------------ hot reload -- */

static long file_mtime(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 ? (long)st.st_mtime : -1;
}

static uint32_t *read_spirv_file(const char *path, size_t *bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *blob = size > 0 ? malloc((size_t)size) : NULL;
    if (!blob || fread(blob, 1, (size_t)size, f) != (size_t)size) { free(blob); fclose(f); return NULL; }
    fclose(f);
    *bytes = (size_t)size;
    return blob;
}

/* One unconditional check per frame, at one fixed point: usually it stats a
 * few files and does nothing. When a SPIR-V file has changed, the pipeline is
 * rebuilt into the same slot, so every handle to it keeps working. */
static void hot_reload_check(vkmin_ctx *c) {
    for (uint32_t i = 0; i < VKMIN_MAX_PIPES; ++i) {
        pipe_slot *s = &c->pipes[i];
        const char *paths[3] = {s->desc.vs_path, s->desc.fs_path, s->desc.cs_path};
        if (!s->used || (!paths[0] && !paths[1] && !paths[2])) continue;
        bool changed = false;
        for (int k = 0; k < 3; ++k) {
            const long m = file_mtime(paths[k]);
            if (paths[k] && m != s->mtime[k]) { changed = s->mtime[k] != 0; s->mtime[k] = m; }
        }
        if (!changed) continue;
        vkmin_pipe_desc d = s->desc;
        const uint32_t **blobs[3] = {&d.vs, &d.fs, &d.cs};
        size_t *sizes[3] = {&d.vs_bytes, &d.fs_bytes, &d.cs_bytes};
        bool ok = true;
        uint32_t *fresh[3] = {0};
        for (int k = 0; k < 3 && ok; ++k) {
            if (!paths[k]) continue;
            fresh[k] = read_spirv_file(paths[k], sizes[k]);
            if (!fresh[k]) ok = false; else *blobs[k] = fresh[k];
        }
        if (!ok) { /* half-written file: try again next frame */
            for (int k = 0; k < 3; ++k) free(fresh[k]);
            for (int k = 0; k < 3; ++k) s->mtime[k] = 0;
            continue;
        }
        VK_CHECK(vkDeviceWaitIdle(c->dev));
        vkDestroyPipeline(c->dev, s->pipe, NULL);
        const uint16_t gen = s->gen;
        const char label[VKMIN_LABEL];
        memcpy((void *)label, s->label, sizeof label);
        s->used = false;                       /* let make_pipeline take this very slot */
        const vkmin_pipe p = vkmin_make_pipeline(c, &d);
        VKMIN_ASSERT(handle_index(p.id) == i, "hot reload landed in a different slot");
        s->gen = gen;                          /* same handle as before */
        for (int k = 0; k < 3; ++k) { free(s->loaded[k]); s->loaded[k] = fresh[k]; }
        fprintf(stderr, "vkmin: reloaded pipeline '%s'\n", label);
    }
}

/* ---------------------------------------------------------------- frame --- */

static void backbuffer_bind(vkmin_ctx *c, VkImage img, VkImageView view) {
    image_slot *bb = &c->images[VKMIN_BACKBUFFER_SLOT];
    bb->img = img;
    bb->view = view;
    bb->w = c->extent.width;
    bb->h = c->extent.height;
    bb->format = c->backbuffer_format;
    /* The same owned image every frame, so its tracked use (TRANSFER_SRC from
     * last frame's readback or blit) stays valid and orders this frame's first
     * write after that read. */
}

bool vkmin_frame_begin(vkmin_ctx *c, const vkmin_clear *clear) {
    VKMIN_ASSERT(c != NULL, "vkmin_frame_begin: null context");
    VKMIN_ASSERT(!c->in_frame, "vkmin_frame_begin called twice without a frame_end");

    /* Is there a frame to render? Headless walks the --frame list; windowed
     * runs until the window closes or --exit-after is reached. A replay has
     * had its frame index set by the record being replayed. */
    if (c->replaying) {
        /* nothing to decide: the journal decides, input included */
    } else if (c->demo_in) {
        demo_record dr;
        if (fread(&dr, sizeof dr, 1, c->demo_in) != 1) return false;
        if (c->frame_last >= 0 && (int)dr.frame_index > c->frame_last) return false;
        if (!c->desc.headless) {
            plat_poll();
            if (plat_should_close()) return false;
        }
        c->frame_index = dr.frame_index;
        c->input = dr.input;
    } else if (c->desc.headless) {
        if (c->frame_cursor >= c->frame_count) return false;
        c->frame_index = (uint32_t)c->frame_list[c->frame_cursor];
    } else {
        plat_poll();
        if (plat_should_close()) return false;
        if (c->exit_after > 0 && (int)c->frames_rendered >= c->exit_after) return false;
    }
    if (!c->replaying && !c->demo_in) {
        /* The one place input is read. Edges are computed here so the
         * snapshot, not the backend, is the whole truth of the frame. */
        vkmin_inputs raw = {0};
        if (!c->desc.headless) plat_input(&raw);
        for (size_t k = 0; k < sizeof raw.down / sizeof raw.down[0]; ++k) raw.pressed[k] = raw.down[k] & ~c->prev_input.down[k];
        raw.buttons_pressed = raw.buttons & ~c->prev_input.buttons;
        c->prev_input = raw;
        c->input = raw;
    }
    if (c->demo_out) {
        const demo_record dr = {.frame_index = c->frame_index, .input = c->input};
        VKMIN_ASSERT(fwrite(&dr, sizeof dr, 1, c->demo_out) == 1, "demo write failed");
    }
    c->draws = 0;
    c->dispatches = 0;
    c->ring_issued_count = 0;
    if (cvar_get_bool(CV_r_hotreload)) hot_reload_check(c);
    {
        rec_frame rf = {.frame_index = c->frame_index, .has_clear = clear != NULL, .input = c->input};
        if (clear) rf.clear = *clear;
        RECORD(c, OP_FRAME_BEGIN, rf, NULL, 0);
    }
    RECORD_ENTER(c);

    if (c->fence_pending[c->slot]) {
        VK_CHECK(vkWaitForFences(c->dev, 1, &c->fence[c->slot], VK_TRUE, UINT64_MAX));
        c->fence_pending[c->slot] = false;
    }
    VK_CHECK(vkResetFences(c->dev, 1, &c->fence[c->slot]));

    /* Timestamps from the frame that last used this slot are complete now. */
    c->ts_count = 0;
    if (c->ts_written[c->slot] > 0) {
        uint64_t raw[VKMIN_MAX_TIMESTAMPS];
        const uint32_t first = c->slot * VKMIN_MAX_TIMESTAMPS;
        VK_CHECK(vkGetQueryPoolResults(c->dev, c->query_pool, first, (uint32_t)c->ts_written[c->slot],
                                       sizeof raw, raw, sizeof raw[0], VK_QUERY_RESULT_64_BIT));
        for (int i = 0; i < c->ts_written[c->slot]; ++i) {
            c->ts_ms[i] = (double)(raw[i] - raw[0]) * (double)c->timestamp_period_ns * 1e-6;
        }
        c->ts_count = c->ts_written[c->slot];
    }
    c->ts_written[c->slot] = 0;

    if (!c->desc.headless) {
        if (c->need_recreate) recreate_swapchain(c);
        VkResult r = vkAcquireNextImageKHR(c->dev, c->swapchain, UINT64_MAX, c->acquired[c->slot],
                                           VK_NULL_HANDLE, &c->swap_index);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain(c);
            r = vkAcquireNextImageKHR(c->dev, c->swapchain, UINT64_MAX, c->acquired[c->slot],
                                      VK_NULL_HANDLE, &c->swap_index);
        }
        if (r != VK_SUBOPTIMAL_KHR) VK_CHECK(r);
    }
    backbuffer_bind(c, c->offscreen_img, c->offscreen_view);

    c->ring_head[c->slot] = 0;
    VkCommandBuffer cmd = c->cmd[c->slot];
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    vkCmdResetQueryPool(cmd, c->query_pool, c->slot * VKMIN_MAX_TIMESTAMPS, VKMIN_MAX_TIMESTAMPS);

    /* The one and only descriptor bind of the frame, to both bind points. */
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipe_layout, 0, 1, &c->set, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c->pipe_layout, 0, 1, &c->set, 0, NULL);
    c->in_frame = true;
    if (clear) {
        /* The default pass: backbuffer plus depth, for programs that do not
         * want to manage passes. vkmin_frame_end closes it. */
        vkmin_pass_begin(c, &(vkmin_pass_desc){.color = vkmin_backbuffer(c), .depth = c->default_depth,
                                               .clear_color = true, .clear = {clear->r, clear->g, clear->b, clear->a},
                                               .clear_depth = true, .label = "default"});
        c->in_default_pass = true;
    }
    RECORD_LEAVE(c);
    return true;
}

uint32_t vkmin_frame_index(const vkmin_ctx *c) { return c->frame_index; }
float vkmin_aspect(const vkmin_ctx *c) { return (float)c->extent.width / (float)c->extent.height; }

void *vkmin_ring_alloc(vkmin_ctx *c, size_t bytes, uint64_t *addr_out) {
    VKMIN_ASSERT(c && c->in_frame, "vkmin_ring_alloc outside a frame");
    const VkDeviceSize off = align_up(c->ring_head[c->slot], VKMIN_RING_ALIGN);
    VKMIN_ASSERT(off + bytes <= c->ring_region, "host ring region exhausted: %zu bytes at %llu of %llu",
                 bytes, (unsigned long long)off, (unsigned long long)c->ring_region);
    c->ring_head[c->slot] = off + bytes;
    const VkDeviceSize base = c->slot * c->ring_region + off;
    if (addr_out) *addr_out = c->ring_addr + base;
    if (c->ring_issued_count < 256) c->ring_issued[c->ring_issued_count++] = base;
    const rec_upload ra = {.offset = bytes};
    RECORD(c, OP_RING_ALLOC, ra, NULL, 0);
    return c->ring_mapped + base;
}

void vkmin_barrier(vkmin_ctx *c, const vkmin_barrier_desc *desc) {
    VKMIN_ASSERT(c && desc && c->in_frame && !c->in_pass, "vkmin_barrier: must be in a frame, outside a pass");
    VkImageMemoryBarrier2 images[16];
    VKMIN_ASSERT(desc->image_count <= 16, "too many image transitions in one barrier");
    {
        const rec_barrier rb = {.flags = (desc->compute_to_indirect_draw ? 1u : 0u) | (desc->compute_to_fragment ? 2u : 0u) |
                                         (desc->transfer_to_compute ? 4u : 0u) | (desc->frame_start ? 8u : 0u) |
                                         (desc->compute_to_transfer ? 16u : 0u),
                                .image_count = (uint32_t)desc->image_count};
        RECORD(c, OP_BARRIER, rb, desc->images, sizeof(vkmin_transition) * (size_t)desc->image_count);
    }
    for (int i = 0; i < desc->image_count; ++i) {
        image_slot *s = NULL;
        VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, desc->images[i].image.id, s);
        images[i] = slot_transition(s, desc->images[i].use, false);
    }
    VkMemoryBarrier2 mem = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    if (desc->compute_to_indirect_draw) {
        mem.srcStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.srcAccessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        mem.dstStageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                            VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        mem.dstAccessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                             VK_ACCESS_2_INDEX_READ_BIT;
    }
    if (desc->compute_to_fragment) {
        mem.srcStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.srcAccessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        mem.dstStageMask |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        mem.dstAccessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    }
    if (desc->transfer_to_compute) {
        /* ALL_TRANSFER rather than CLEAR: the spec files vkCmdFillBuffer under
         * CLEAR, synchronization validation files it under COPY, and the
         * union of the transfer stages is still a narrow mask. */
        mem.srcStageMask |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        mem.srcAccessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mem.dstStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.dstAccessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }
    if (desc->frame_start) {
        /* Last frame's draws and its count-readback copy read what this
         * frame's compute and fill are about to overwrite: write-after-read,
         * so an execution dependency with no source access mask. */
        mem.srcStageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        mem.dstStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        mem.dstAccessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if (desc->compute_to_transfer) {
        mem.srcStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.srcAccessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        mem.dstStageMask |= VK_PIPELINE_STAGE_2_COPY_BIT;
        mem.dstAccessMask |= VK_ACCESS_2_TRANSFER_READ_BIT;
    }
    const bool has_mem = mem.srcStageMask != 0;
    if (desc->image_count == 0 && !has_mem) return;
    const VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = has_mem ? 1u : 0u,
        .pMemoryBarriers = &mem,
        .imageMemoryBarrierCount = (uint32_t)desc->image_count,
        .pImageMemoryBarriers = images,
    };
    vkCmdPipelineBarrier2(c->cmd[c->slot], &dep);
}

void vkmin_fill_buffer(vkmin_ctx *c, vkmin_buffer b, size_t offset, size_t bytes, uint32_t value) {
    VKMIN_ASSERT(c && c->in_frame && !c->in_pass, "vkmin_fill_buffer: must be in a frame, outside a pass");
    const buffer_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, b.id, s);
    VKMIN_ASSERT(offset + bytes <= s->size, "fill overruns buffer");
    const rec_indirect rf = {.cmds = b.id, .cmd_offset = offset, .count_offset = bytes, .max_draws = value};
    RECORD(c, OP_FILL, rf, NULL, 0);
    vkCmdFillBuffer(c->cmd[c->slot], c->arena_buf, s->offset + offset, bytes, value);
}

void vkmin_copy_to_ring(vkmin_ctx *c, vkmin_buffer src, size_t offset, size_t bytes, uint64_t ring_addr) {
    VKMIN_ASSERT(c && c->in_frame && !c->in_pass, "vkmin_copy_to_ring: must be in a frame, outside a pass");
    const buffer_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, src.id, s);
    VKMIN_ASSERT(offset + bytes <= s->size, "copy overruns source buffer");
    VKMIN_ASSERT(ring_addr >= c->ring_addr && ring_addr + bytes <= c->ring_addr + c->ring_cap,
                 "destination is not in the ring buffer");
    {
        const rec_indirect rc = {.cmds = src.id, .cmd_offset = offset, .count_offset = bytes};
        const uint64_t dst = ring_addr;
        RECORD(c, OP_COPY_TO_RING, rc, &dst, sizeof dst);
    }
    /* This slot's ring bytes were written by the copy two frames ago and read
     * by the host since. The fence orders that on the CPU; the device needs
     * it said too, or the overwrite is an unordered write-after-write. */
    const VkBufferMemoryBarrier2 to_device = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = c->ring_buf,
        .offset = ring_addr - c->ring_addr,
        .size = bytes,
    };
    const VkDependencyInfo dep_dev = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                      .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &to_device};
    vkCmdPipelineBarrier2(c->cmd[c->slot], &dep_dev);
    const VkBufferCopy copy = {.srcOffset = s->offset + offset, .dstOffset = ring_addr - c->ring_addr, .size = bytes};
    vkCmdCopyBuffer(c->cmd[c->slot], c->arena_buf, c->ring_buf, 1, &copy);
    /* Make the copy visible to the host read that happens after the fence. */
    const VkBufferMemoryBarrier2 to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = c->ring_buf,
        .offset = ring_addr - c->ring_addr,
        .size = bytes,
    };
    const VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                  .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &to_host};
    vkCmdPipelineBarrier2(c->cmd[c->slot], &dep);
}

void vkmin_pass_begin(vkmin_ctx *c, const vkmin_pass_desc *desc) {
    VKMIN_ASSERT(c && desc && c->in_frame && !c->in_pass, "vkmin_pass_begin: bad state");
    VKMIN_ASSERT(desc->color.id || desc->depth.id, "pass with no attachments");
    {
        const rec_pass rp = {.color = desc->color.id, .depth = desc->depth.id, .clear_color = desc->clear_color,
                             .clear_depth = desc->clear_depth, .clear = {desc->clear[0], desc->clear[1], desc->clear[2], desc->clear[3]},
                             .x = desc->x, .y = desc->y, .w = desc->w, .h = desc->h};
        RECORD(c, OP_PASS_BEGIN, rp, NULL, 0);
    }
    RECORD_ENTER(c);
    VkCommandBuffer cmd = c->cmd[c->slot];

    VkImageMemoryBarrier2 barriers[2];
    uint32_t barrier_count = 0;
    image_slot *color = NULL, *depth = NULL;
    if (desc->color.id) {
        VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, desc->color.id, color);
        barriers[barrier_count++] = slot_transition(color, VKMIN_USE_COLOR_TARGET, desc->clear_color);
    }
    if (desc->depth.id) {
        VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, desc->depth.id, depth);
        barriers[barrier_count++] = slot_transition(depth, VKMIN_USE_DEPTH_TARGET, desc->clear_depth);
    }
    const VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                  .imageMemoryBarrierCount = barrier_count,
                                  .pImageMemoryBarriers = barriers};
    vkCmdPipelineBarrier2(cmd, &dep);

    if (c->fp_label_begin && desc->label) {
        const VkDebugUtilsLabelEXT label = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                                            .pLabelName = desc->label};
        c->fp_label_begin(cmd, &label);
    }

    const image_slot *any = color ? color : depth;
    VKMIN_ASSERT(any != NULL, "pass with no attachments");
    const int w = desc->w > 0 ? desc->w : (int)any->w;
    const int h = desc->w > 0 ? desc->h : (int)any->h;
    const VkRenderingAttachmentInfo color_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = color ? color->view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = desc->clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {desc->clear[0], desc->clear[1], desc->clear[2], desc->clear[3]}}},
    };
    const VkRenderingAttachmentInfo depth_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth ? depth->view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = desc->clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.depthStencil = {.depth = 1.0f}},
    };
    const VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {desc->x, desc->y}, .extent = {(uint32_t)w, (uint32_t)h}},
        .layerCount = 1,
        .colorAttachmentCount = color ? 1u : 0u,
        .pColorAttachments = color ? &color_att : NULL,
        .pDepthAttachment = depth ? &depth_att : NULL,
    };
    vkCmdBeginRendering(cmd, &rendering);
    c->in_pass = true;
    vkmin_set_viewport(c, desc->x, desc->y, w, h);
    RECORD_LEAVE(c);
}

void vkmin_pass_end(vkmin_ctx *c) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_pass_end without a pass");
    const uint32_t none = 0;
    RECORD(c, OP_PASS_END, none, NULL, 0);
    vkCmdEndRendering(c->cmd[c->slot]);
    if (c->fp_label_end) c->fp_label_end(c->cmd[c->slot]);
    c->in_pass = false;
}

void vkmin_set_viewport(vkmin_ctx *c, int x, int y, int w, int h) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_set_viewport outside a pass");
    const rec_pass rv = {.x = x, .y = y, .w = w, .h = h};
    RECORD(c, OP_VIEWPORT, rv, NULL, 0);
    const VkViewport vp = {.x = (float)x, .y = (float)y, .width = (float)w, .height = (float)h,
                           .minDepth = 0.0f, .maxDepth = 1.0f};
    const VkRect2D sc = {.offset = {x, y}, .extent = {(uint32_t)w, (uint32_t)h}};
    vkCmdSetViewport(c->cmd[c->slot], 0, 1, &vp);
    vkCmdSetScissor(c->cmd[c->slot], 0, 1, &sc);
}

void vkmin_set_depth_bias(vkmin_ctx *c, float constant, float slope) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_set_depth_bias outside a pass");
    const vkmin_clear rb = {.r = constant, .g = slope};
    RECORD(c, OP_DEPTH_BIAS, rb, NULL, 0);
    vkCmdSetDepthBias(c->cmd[c->slot], constant, 0.0f, slope);
}

/* Every draw and dispatch goes through here: the pipeline and the push block
 * are parameters of the call, never state left behind for the next one. */
static void bind_and_push(vkmin_ctx *c, vkmin_pipe p, VkPipelineBindPoint want, const void *push, uint32_t bytes) {
    VKMIN_ASSERT(c && c->in_frame, "draw or dispatch outside a frame");
    VKMIN_ASSERT(bytes <= VKMIN_PUSH_BYTES && (bytes == 0 || push), "push block of %u bytes (max %u)", bytes,
                 (unsigned)VKMIN_PUSH_BYTES);
    const pipe_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->pipes, VKMIN_MAX_PIPES, p.id, s);
    VKMIN_ASSERT(s->bind_point == want, "'%s' is not a %s pipeline", s->label,
                 want == VK_PIPELINE_BIND_POINT_GRAPHICS ? "graphics" : "compute");
    VKMIN_ASSERT((want == VK_PIPELINE_BIND_POINT_GRAPHICS) == c->in_pass, "draws go inside a pass, dispatches outside");
    vkCmdBindPipeline(c->cmd[c->slot], want, s->pipe);
    if (bytes) {
        vkCmdPushConstants(c->cmd[c->slot], c->pipe_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                           0, bytes, push);
    }
}

void vkmin_draw(vkmin_ctx *c, vkmin_pipe p, const void *push, uint32_t push_bytes, uint32_t vertices, uint32_t instances) {
    const rec_draw rd = {.pipe = p.id, .push_bytes = push_bytes, .a = vertices, .b = instances};
    RECORD(c, OP_DRAW, rd, push, push_bytes);
    bind_and_push(c, p, VK_PIPELINE_BIND_POINT_GRAPHICS, push, push_bytes);
    vkCmdDraw(c->cmd[c->slot], vertices, instances, 0, 0);
    c->draws++;
}

void vkmin_draw_indirect(vkmin_ctx *c, vkmin_pipe p, const void *push, uint32_t push_bytes, const vkmin_indirect_desc *d) {
    VKMIN_ASSERT(d && vkmin_valid(d->indices), "vkmin_draw_indirect: needs an index buffer");
    {
        const rec_indirect ri = {.pipe = p.id, .push_bytes = push_bytes, .indices = d->indices.id, .cmds = d->cmds.id,
                                 .counts = d->counts.id, .max_draws = d->max_draws, .host_count = d->host_count,
                                 .cmd_offset = d->cmd_offset, .count_offset = d->count_offset, .host_cmds = d->host_cmds};
        /* the host command address rides after the push so it is relocated too */
        uint8_t data[VKMIN_PUSH_BYTES + 8] = {0};
        if (push_bytes) memcpy(data, push, push_bytes);
        memcpy(data + push_bytes, &d->host_cmds, 8);
        RECORD(c, OP_DRAW_INDIRECT, ri, data, push_bytes + 8);
    }
    bind_and_push(c, p, VK_PIPELINE_BIND_POINT_GRAPHICS, push, push_bytes);
    const buffer_slot *ib = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, d->indices.id, ib);
    vkCmdBindIndexBuffer(c->cmd[c->slot], c->arena_buf, ib->offset, VK_INDEX_TYPE_UINT32);
    if (vkmin_valid(d->cmds)) {
        const buffer_slot *cs = NULL;
        VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, d->cmds.id, cs);
        if (vkmin_valid(d->counts)) {
            const buffer_slot *ns = NULL;
            VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, d->counts.id, ns);
            vkCmdDrawIndexedIndirectCount(c->cmd[c->slot], c->arena_buf, cs->offset + d->cmd_offset, c->arena_buf,
                                          ns->offset + d->count_offset, d->max_draws, sizeof(DrawCmd));
        } else if (d->max_draws) {
            vkCmdDrawIndexedIndirect(c->cmd[c->slot], c->arena_buf, cs->offset + d->cmd_offset, d->max_draws, sizeof(DrawCmd));
        }
    } else if (d->host_count) {
        VKMIN_ASSERT(d->host_cmds >= c->ring_addr && d->host_cmds < c->ring_addr + c->ring_cap,
                     "indirect commands are not in the ring buffer");
        vkCmdDrawIndexedIndirect(c->cmd[c->slot], c->ring_buf, d->host_cmds - c->ring_addr, d->host_count, sizeof(DrawCmd));
    }
    c->draws++;
}

void vkmin_dispatch(vkmin_ctx *c, vkmin_pipe p, const void *push, uint32_t push_bytes, uint32_t x, uint32_t y, uint32_t z) {
    const rec_draw rd = {.pipe = p.id, .push_bytes = push_bytes, .a = x, .b = y, .cnt = z};
    RECORD(c, OP_DISPATCH, rd, push, push_bytes);
    bind_and_push(c, p, VK_PIPELINE_BIND_POINT_COMPUTE, push, push_bytes);
    vkCmdDispatch(c->cmd[c->slot], x, y, z);
    c->dispatches++;
}

void vkmin_timestamp(vkmin_ctx *c, int index) {
    VKMIN_ASSERT(c && c->in_frame && index >= 0 && index < VKMIN_MAX_TIMESTAMPS, "vkmin_timestamp: bad index");
    const uint32_t ri = (uint32_t)index;
    RECORD(c, OP_TIMESTAMP, ri, NULL, 0);
    vkCmdWriteTimestamp2(c->cmd[c->slot], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, c->query_pool,
                         c->slot * VKMIN_MAX_TIMESTAMPS + (uint32_t)index);
    if (index + 1 > c->ts_written[c->slot]) c->ts_written[c->slot] = index + 1;
}

vkmin_stats vkmin_stats_get(const vkmin_ctx *c) {
    vkmin_stats s = c->stats;
    s.timestamps = c->ts_count;
    for (int i = 0; i < c->ts_count; ++i) s.gpu_ms[i] = c->ts_ms[i];
    s.path = c->path;
    s.textures = c->texture_count;
    for (uint32_t i = 0; i < VKMIN_MAX_BUFFERS; ++i) s.buffers += c->buffers[i].used;
    for (uint32_t i = 0; i < VKMIN_MAX_IMAGES; ++i) s.images += c->images[i].used;
    for (uint32_t i = 0; i < VKMIN_MAX_PIPES; ++i) s.pipelines += c->pipes[i].used;
    s.device_used = (size_t)(c->buf_arena.used + c->img_arena.used);
    s.device_cap = (size_t)(c->buf_arena.cap + c->img_arena.cap);
    s.ring_used = (size_t)c->ring_head[c->last_slot];
    s.ring_cap = (size_t)c->ring_region;
    return s;
}

void vkmin_dump(const vkmin_ctx *c, FILE *out) {
    static const char *const uses[] = {"undefined", "transfer_dst", "transfer_src", "sampled", "color", "depth", "present"};
    fprintf(out, "vkmin: path=%s frame=%u slot=%u arena %llu/%llu ring %llu/%llu\n",
            c->path == VKMIN_PATH_MODERN ? "modern" : "legacy", c->frame_index, c->slot,
            (unsigned long long)(c->buf_arena.used + c->img_arena.used),
            (unsigned long long)(c->buf_arena.cap + c->img_arena.cap),
            (unsigned long long)c->ring_head[c->last_slot], (unsigned long long)c->ring_region);
    for (uint32_t i = 0; i < VKMIN_MAX_BUFFERS; ++i) {
        const buffer_slot *s = &c->buffers[i];
        if (s->used) fprintf(out, "  buffer[%u] gen %u  %-32s %llu bytes at %llu\n", i, s->gen, s->label,
                             (unsigned long long)s->size, (unsigned long long)s->offset);
    }
    for (uint32_t i = 0; i < VKMIN_MAX_IMAGES; ++i) {
        const image_slot *s = &c->images[i];
        if (s->used) fprintf(out, "  image[%u]  gen %u  %-32s %ux%u mips %u  %s  tex %d\n", i, s->gen, s->label, s->w,
                             s->h, s->mips, uses[s->use], s->tex_index == UINT32_MAX ? -1 : (int)s->tex_index);
    }
    for (uint32_t i = 0; i < VKMIN_MAX_PIPES; ++i) {
        const pipe_slot *s = &c->pipes[i];
        if (s->used) fprintf(out, "  pipe[%u]   gen %u  %-32s %s\n", i, s->gen, s->label,
                             s->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS ? "graphics" : "compute");
    }
}

/* --- legacy-only: the per-frame staging copy ----------------------------- */
/* Execute then inhibit: every frame copies the backbuffer into the mapped
 * readback buffer whether or not anyone asks for a PNG, so the capture never
 * reasons about what happened last. At 1080p the copy is real bandwidth, so
 * the alternative is kept alive: no_readback skips it and vkmin_save_png says
 * why it cannot. The modern path has no equivalent: it reads the image from
 * the host after the fence, with no command at all. */
static void legacy_record_readback(vkmin_ctx *c, VkCommandBuffer cmd, image_slot *bb) {
    cmd_transition(cmd, bb, VKMIN_USE_TRANSFER_SRC, false);
    const VkBufferMemoryBarrier2 to_device = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = c->readback_buf[c->slot],
        .size = VK_WHOLE_SIZE,
    };
    const VkDependencyInfo dep_dev = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                      .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &to_device};
    vkCmdPipelineBarrier2(cmd, &dep_dev);
    const VkBufferImageCopy readback = {
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {c->extent.width, c->extent.height, 1},
    };
    vkCmdCopyImageToBuffer(cmd, bb->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->readback_buf[c->slot], 1, &readback);
    const VkBufferMemoryBarrier2 to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = c->readback_buf[c->slot],
        .size = VK_WHOLE_SIZE,
    };
    const VkDependencyInfo dep_host = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                       .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &to_host};
    vkCmdPipelineBarrier2(cmd, &dep_host);
}
/* --- end legacy-only ------------------------------------------------------ */

/* Windowed only: blit the owned backbuffer into the acquired swapchain image.
 * The blit converts formats (RGBA to BGRA) and scales to the window. */
static void record_present_blit(vkmin_ctx *c, VkCommandBuffer cmd, image_slot *bb) {
    cmd_transition(cmd, bb, VKMIN_USE_TRANSFER_SRC, false);
    VkImageMemoryBarrier2 to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = c->swap_img[c->swap_index],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_dst};
    vkCmdPipelineBarrier2(cmd, &dep);
    const VkImageBlit2 region = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .srcOffsets = {{0, 0, 0}, {(int32_t)c->extent.width, (int32_t)c->extent.height, 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .dstOffsets = {{0, 0, 0}, {(int32_t)c->swap_extent.width, (int32_t)c->swap_extent.height, 1}},
    };
    const VkBlitImageInfo2 blit = {
        .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
        .srcImage = bb->img, .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .dstImage = c->swap_img[c->swap_index], .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount = 1, .pRegions = &region, .filter = VK_FILTER_LINEAR,
    };
    vkCmdBlitImage2(cmd, &blit);
    to_dst.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    to_dst.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_dst.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    to_dst.dstAccessMask = 0;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void vkmin_frame_end(vkmin_ctx *c) {
    VKMIN_ASSERT(c && c->in_frame, "vkmin_frame_end: no frame is open");
    /* Everything the program wrote into the ring this frame travels with the
     * record, so a replay puts the same bytes at the same offsets. */
    {
        const rec_upload re = {.offset = c->ring_head[c->slot]};
        RECORD(c, OP_FRAME_END, re, c->ring_mapped + c->slot * c->ring_region, (size_t)c->ring_head[c->slot]);
    }
    RECORD_ENTER(c);
    if (c->in_default_pass) {
        vkmin_pass_end(c);
        c->in_default_pass = false;
    }
    VKMIN_ASSERT(!c->in_pass, "vkmin_frame_end: a pass is still open");
    VkCommandBuffer cmd = c->cmd[c->slot];
    image_slot *bb = &c->images[VKMIN_BACKBUFFER_SLOT];

    /* Seam 1 of 3: the legacy path records its per-frame readback copy here;
     * the modern path records nothing and reads the image from the host. */
    if (c->path == VKMIN_PATH_LEGACY && !c->desc.no_readback) legacy_record_readback(c, cmd, bb);
    if (!c->desc.headless) record_present_blit(c, cmd, bb);
    /* Leave the backbuffer in TRANSFER_SRC either way, so the modern path's
     * host copy reads it from a layout hostImageCopy accepts. */
    cmd_transition(cmd, bb, VKMIN_USE_TRANSFER_SRC, false);
    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkCommandBufferSubmitInfo cmd_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                                .commandBuffer = cmd};
    const VkSemaphoreSubmitInfo wait = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                        .semaphore = c->acquired[c->slot],
                                        .stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT};
    const VkSemaphoreSubmitInfo signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                          .semaphore = c->desc.headless ? VK_NULL_HANDLE : c->rendered[c->swap_index],
                                          .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    const uint32_t sem_count = c->desc.headless ? 0u : 1u;
    const VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = sem_count, .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1, .pCommandBufferInfos = &cmd_info,
        .signalSemaphoreInfoCount = sem_count, .pSignalSemaphoreInfos = &signal,
    };
    VK_CHECK(vkQueueSubmit2(c->queue, 1, &submit, c->fence[c->slot]));
    c->fence_pending[c->slot] = true;
    c->last_slot = c->slot;
    c->have_submitted = true;

    if (!c->desc.headless) {
        const VkPresentInfoKHR present = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1, .pWaitSemaphores = &c->rendered[c->swap_index],
            .swapchainCount = 1, .pSwapchains = &c->swapchain, .pImageIndices = &c->swap_index,
        };
        const VkResult r = vkQueuePresentKHR(c->queue, &present);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            c->need_recreate = true;
        } else {
            VK_CHECK(r);
        }
        int w = 0, h = 0;
        plat_framebuffer_size(&w, &h);
        if (w > 0 && h > 0 && ((uint32_t)w != c->swap_extent.width || (uint32_t)h != c->swap_extent.height)) {
            c->need_recreate = true;
        }
    }
    if (c->desc.sync_naive) VK_CHECK(vkDeviceWaitIdle(c->dev));
    c->slot = (c->slot + 1u) % c->frames_in_flight;
    c->in_frame = false;
    c->frames_rendered++;
    c->stats.draws = c->draws;
    c->stats.dispatches = c->dispatches;
    c->stats.frame_index = c->frame_index;

    /* --out / --out-dir: save what was asked for, then move on. */
    const uint32_t rendered_index = c->frame_index;
    bool last = false;
    if (c->replaying || c->demo_in) {
        /* Save only frames named on the command line, or every frame if none were. */
        bool wanted = c->frame_count == 0;
        for (int i = 0; i < c->frame_count; ++i) wanted |= c->frame_list[i] == (int)rendered_index;
        if (wanted && (c->out || c->out_dir)) {
            char path[1024];
            if (c->out && c->frame_count == 1) snprintf(path, sizeof path, "%s", c->out);
            else snprintf(path, sizeof path, "%s/%s_%04u.png", c->out_dir ? c->out_dir : ".", c->desc.title, rendered_index);
            if (vkmin_save_png(c, path)) printf("wrote %s\n", path);
        }
        RECORD_LEAVE(c);
        return;
    }
    if (c->desc.headless) {
        c->frame_cursor++;
        last = c->frame_cursor >= c->frame_count;
    } else {
        c->frame_index += (uint32_t)cvar_get_int(CV_d_frame_step);
        last = c->exit_after > 0 && (int)c->frames_rendered >= c->exit_after;
    }
    if (c->out || c->out_dir) {
        const bool single = c->out && (c->frame_count <= 1 || !c->desc.headless);
        if (!single || last || c->desc.headless) {
            char path[1024];
            if (single) snprintf(path, sizeof path, "%s", c->out);
            else snprintf(path, sizeof path, "%s/%s_%04u.png", c->out_dir ? c->out_dir : ".", c->desc.title, rendered_index);
            if (c->desc.headless || last) {
                if (vkmin_save_png(c, path)) printf("wrote %s\n", path);
            }
        }
    }
    RECORD_LEAVE(c);
}

/* --------------------------------------------------------------- replay --- */

static void relocate(const vkmin_ctx *c, uint8_t *data, const reloc *relocs, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t v;
        memcpy(&v, data + relocs[i].offset, 8);
        if (relocs[i].kind == RELOC_ARENA) v = c->arena_addr + (v - c->rec_arena_base);
        else v = c->ring_addr + (v - c->rec_ring_base);
        memcpy(data + relocs[i].offset, &v, 8);
    }
}

bool vkmin_replay(vkmin_ctx *c, const char *path) {
    VKMIN_ASSERT(c && path && c->replaying, "vkmin_replay: init the context with --replay FILE first");
    FILE *f = fopen(path, "rb");
    VKMIN_ASSERT(f != NULL, "cannot open journal '%s'", path);
    journal_header jh;
    VKMIN_ASSERT(fread(&jh, sizeof jh, 1, f) == 1 && jh.magic == 0x4a4d4b56u && jh.version == 1, "bad journal header");
    VKMIN_ASSERT((int)jh.width == c->desc.width && (int)jh.height == c->desc.height, "journal size does not match the context");
    uint8_t hdr[256] = {0};
    static reloc relocs[VKMIN_MAX_RELOCS];
    size_t cap = 1u << 20;
    uint8_t *data = calloc(cap, 1); /* zeroed: a record with no payload reads as zeros, never as garbage */
    VKMIN_ASSERT(data != NULL, "out of memory");
    record_header rh;
    uint32_t records = 0;
    while (fread(&rh, sizeof rh, 1, f) == 1) {
        VKMIN_ASSERT(rh.hdr_bytes <= sizeof hdr && rh.reloc_count <= VKMIN_MAX_RELOCS, "corrupt journal record");
        if (rh.data_bytes > cap) {
            free(data);
            cap = rh.data_bytes;
            data = calloc(cap, 1);
            VKMIN_ASSERT(data != NULL, "out of memory");
        }
        VKMIN_ASSERT((rh.hdr_bytes == 0 || fread(hdr, rh.hdr_bytes, 1, f) == 1) &&
                     (rh.data_bytes == 0 || fread(data, rh.data_bytes, 1, f) == 1) &&
                     (rh.reloc_count == 0 || fread(relocs, sizeof relocs[0], rh.reloc_count, f) == rh.reloc_count),
                     "truncated journal");
        relocate(c, data, relocs, rh.reloc_count);
        ++records;
        /* Every record's header is exactly its struct; anything else is corruption. */
#define HDR(T) T rec = {0}; VKMIN_ASSERT(rh.hdr_bytes == sizeof rec, "record %u: header size %u, expected %zu", records, rh.hdr_bytes, sizeof rec); memcpy(&rec, hdr, sizeof rec)
#define SAME(got, want) VKMIN_ASSERT((got) == (want), "replay diverged at record %u: handle %u, recorded %u", records, (unsigned)(got), (unsigned)(want))
        switch (rh.op) {
        case OP_MAKE_BUFFER: { HDR(rec_buffer);
            const vkmin_buffer b = vkmin_make_buffer(c, &(vkmin_buffer_desc){.size = rec.size, .data = rec.has_data ? data : NULL, .label = rec.label});
            SAME(b.id, rec.result); break; }
        case OP_FREE_BUFFER: { HDR(vkmin_buffer); vkmin_free_buffer(c, rec); break; }
        case OP_BUFFER_UPLOAD: { HDR(rec_upload); vkmin_buffer_upload(c, (vkmin_buffer){rec.id}, rec.offset, data, rh.data_bytes); break; }
        case OP_MAKE_IMAGE: { HDR(rec_image);
            const vkmin_image i = vkmin_make_image(c, &(vkmin_image_desc){.width = rec.w, .height = rec.h, .mip_levels = rec.mips,
                .format = (vkmin_format)rec.format, .usage = rec.usage, .sampler = rec.sampler, .pixels = rec.has_pixels ? data : NULL, .label = rec.label});
            SAME(i.id, rec.result); break; }
        case OP_FREE_IMAGE: { HDR(vkmin_image); vkmin_free_image(c, rec); break; }
        case OP_IMAGE_UPLOAD: { HDR(rec_upload); vkmin_image_upload(c, (vkmin_image){rec.id}, (int)rec.mip, data, rh.data_bytes); break; }
        case OP_INDEX: { HDR(rec_draw); SAME(vkmin_index(c, (vkmin_image){rec.pipe}), rec.a); break; }
        case OP_REGISTER: { HDR(rec_draw); SAME(vkmin_register_texture(c, (vkmin_image){rec.pipe}, rec.a), rec.b); break; }
        case OP_MAKE_PIPELINE: { HDR(rec_pipe);
            const uint32_t *vs = rec.vs_bytes ? (const uint32_t *)(void *)data : NULL;
            const uint32_t *fs = rec.fs_bytes ? (const uint32_t *)(void *)(data + rec.vs_bytes) : NULL;
            const uint32_t *cs = rec.cs_bytes ? (const uint32_t *)(void *)(data + rec.vs_bytes + rec.fs_bytes) : NULL;
            const vkmin_pipe p = vkmin_make_pipeline(c, &(vkmin_pipe_desc){.vs = vs, .vs_bytes = rec.vs_bytes, .fs = fs, .fs_bytes = rec.fs_bytes,
                .cs = cs, .cs_bytes = rec.cs_bytes, .color_format = (vkmin_format)rec.color_format, .depth = rec.depth, .depth_write = rec.depth_write,
                .depth_compare = (vkmin_compare)rec.compare, .cull = (vkmin_cull)rec.cull, .blend = rec.blend, .depth_bias = rec.bias, .label = rec.label});
            SAME(p.id, rec.result); break; }
        case OP_FRAME_BEGIN: { HDR(rec_frame); c->frame_index = rec.frame_index; c->input = rec.input;
            VKMIN_ASSERT(vkmin_frame_begin(c, rec.has_clear ? &rec.clear : NULL), "replay: frame_begin refused"); break; }
        case OP_RING_ALLOC: { HDR(rec_upload); uint64_t addr = 0; vkmin_ring_alloc(c, (size_t)rec.offset, &addr); break; }
        case OP_FRAME_END: { HDR(rec_upload);
            VKMIN_ASSERT(rec.offset == c->ring_head[c->slot], "replay: ring usage differs (%llu vs %llu)", (unsigned long long)rec.offset, (unsigned long long)c->ring_head[c->slot]);
            memcpy(c->ring_mapped + c->slot * c->ring_region, data, rh.data_bytes);
            vkmin_frame_end(c); break; }
        case OP_BARRIER: { HDR(rec_barrier);
            vkmin_barrier(c, &(vkmin_barrier_desc){.images = (const vkmin_transition *)(void *)data, .image_count = (int)rec.image_count,
                .compute_to_indirect_draw = rec.flags & 1u, .compute_to_fragment = rec.flags & 2u, .transfer_to_compute = rec.flags & 4u,
                .frame_start = rec.flags & 8u, .compute_to_transfer = rec.flags & 16u}); break; }
        case OP_FILL: { HDR(rec_indirect); vkmin_fill_buffer(c, (vkmin_buffer){rec.cmds}, rec.cmd_offset, rec.count_offset, rec.max_draws); break; }
        case OP_COPY_TO_RING: { HDR(rec_indirect); uint64_t dst; memcpy(&dst, data, 8);
            vkmin_copy_to_ring(c, (vkmin_buffer){rec.cmds}, rec.cmd_offset, rec.count_offset, dst); break; }
        case OP_PASS_BEGIN: { HDR(rec_pass);
            vkmin_pass_begin(c, &(vkmin_pass_desc){.color = {rec.color}, .depth = {rec.depth}, .clear_color = rec.clear_color, .clear_depth = rec.clear_depth,
                .clear = {rec.clear[0], rec.clear[1], rec.clear[2], rec.clear[3]}, .x = rec.x, .y = rec.y, .w = rec.w, .h = rec.h, .label = "replay"}); break; }
        case OP_PASS_END: vkmin_pass_end(c); break;
        case OP_VIEWPORT: { HDR(rec_pass); vkmin_set_viewport(c, rec.x, rec.y, rec.w, rec.h); break; }
        case OP_DEPTH_BIAS: { HDR(vkmin_clear); vkmin_set_depth_bias(c, rec.r, rec.g); break; }
        case OP_DRAW: { HDR(rec_draw); vkmin_draw(c, (vkmin_pipe){rec.pipe}, rec.push_bytes ? data : NULL, rec.push_bytes, rec.a, rec.b); break; }
        case OP_DRAW_INDIRECT: { HDR(rec_indirect); uint64_t host; memcpy(&host, data + rec.push_bytes, 8);
            vkmin_draw_indirect(c, (vkmin_pipe){rec.pipe}, rec.push_bytes ? data : NULL, rec.push_bytes,
                &(vkmin_indirect_desc){.indices = {rec.indices}, .cmds = {rec.cmds}, .cmd_offset = rec.cmd_offset, .counts = {rec.counts},
                .count_offset = rec.count_offset, .max_draws = rec.max_draws, .host_cmds = host, .host_count = rec.host_count}); break; }
        case OP_DISPATCH: { HDR(rec_draw); vkmin_dispatch(c, (vkmin_pipe){rec.pipe}, rec.push_bytes ? data : NULL, rec.push_bytes, rec.a, rec.b, rec.cnt); break; }
        case OP_TIMESTAMP: { HDR(uint32_t); vkmin_timestamp(c, (int)rec); break; }
        default: VKMIN_FAIL("unknown journal op %u", rh.op);
        }
#undef HDR
#undef SAME
    }
    free(data);
    fclose(f);
    fprintf(stderr, "vkmin: replayed %u records from %s\n", records, path);
    return true;
}

/* -------------------------------------------------------------- readback -- */

/* --- legacy-only: read the mapped staging buffer ------------------------- */
static bool legacy_read_backbuffer(vkmin_ctx *c, unsigned char *rgba, size_t bytes) {
    VK_CHECK(vkWaitForFences(c->dev, 1, &c->fence[c->last_slot], VK_TRUE, UINT64_MAX));
    c->fence_pending[c->last_slot] = false;
    memcpy(rgba, c->readback_mapped[c->last_slot], bytes);
    return true;
}
/* --- end legacy-only ------------------------------------------------------ */

/* --- modern-only: host image copy, no command, no staging ---------------- */
static bool modern_read_backbuffer(vkmin_ctx *c, unsigned char *rgba, size_t bytes) {
    (void)bytes;
    VK_CHECK(vkWaitForFences(c->dev, 1, &c->fence[c->last_slot], VK_TRUE, UINT64_MAX));
    c->fence_pending[c->last_slot] = false;
    const VkImageToMemoryCopyEXT region = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY_EXT,
        .pHostPointer = rgba,
        .memoryRowLength = 0, /* tightly packed at the image width */
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {c->extent.width, c->extent.height, 1},
    };
    const VkCopyImageToMemoryInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_EXT,
        .srcImage = c->offscreen_img,
        .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .regionCount = 1,
        .pRegions = &region,
    };
    VK_CHECK(c->fp_copy_image_to_memory(c->dev, &info));
    return true;
}
/* --- end modern-only ------------------------------------------------------ */

bool vkmin_save_png(vkmin_ctx *c, const char *path) {
    VKMIN_ASSERT(c && path && !c->in_frame, "vkmin_save_png: call it between frames");
    if (!c->have_submitted) {
        fprintf(stderr, "vkmin: nothing rendered yet, not writing '%s'\n", path);
        return false;
    }
    if (c->path == VKMIN_PATH_LEGACY && c->desc.no_readback) {
        fprintf(stderr, "vkmin: readback is disabled (no_readback), not writing '%s'\n", path);
        return false;
    }
    const uint32_t w = c->extent.width, h = c->extent.height;
    const size_t bytes = (size_t)w * h * 4u;
    const int row_pitch = (int)(w * 4u); /* both paths copy tightly packed */
    unsigned char *rgba = malloc(bytes);
    VKMIN_ASSERT(rgba != NULL, "out of memory writing '%s'", path);
    /* Seam 1 of 3, read side. */
    if (c->path == VKMIN_PATH_LEGACY) legacy_read_backbuffer(c, rgba, bytes);
    else modern_read_backbuffer(c, rgba, bytes);
    for (size_t i = 3; i < bytes; i += 4) rgba[i] = 255; /* the PNG is opaque by definition */
    const bool ok = vkmin_png_write(path, (int)w, (int)h, rgba, row_pitch);
    free(rgba);
    if (!ok) fprintf(stderr, "vkmin: failed to write '%s'\n", path);
    return ok;
}
