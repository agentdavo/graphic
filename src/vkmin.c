/* vkmin.c -- see vkmin.h. Vulkan 1.3 core; dynamic rendering, synchronization2,
 * buffer device address, descriptor indexing and drawIndirectCount are
 * required at init, never probed for. There is no VkRenderPass, no
 * VkFramebuffer, no VkPipelineVertexInputStateCreateInfo, and no
 * `if (extension_supported)` anywhere in this file.
 */
#include "vkmin.h"
#include "plat.h"
#include "stb_bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define VKMIN_FATAL(...)                                                          \
    do {                                                                          \
        fprintf(stderr, "%s:%d: vkmin: ", __FILE__, __LINE__);                    \
        fprintf(stderr, __VA_ARGS__);                                             \
        fputc('\n', stderr);                                                      \
        fflush(stderr);                                                           \
        abort();                                                                  \
    } while (0)

#define VKMIN_ASSERT(cond, ...)                                                   \
    do {                                                                          \
        if (!(cond)) VKMIN_FATAL(__VA_ARGS__);                                    \
    } while (0)

/* ------------------------------------------------------------- the state -- */

enum {
    VKMIN_MAX_BUFFERS = 64,
    VKMIN_MAX_IMAGES = 256,
    VKMIN_MAX_PIPES = 16,
    VKMIN_MAX_FRAMES = 2,
    VKMIN_MAX_SWAP = 8,
    VKMIN_HANDLE_INDEX_BITS = 20,
    VKMIN_BACKBUFFER_SLOT = 0,      /* image slot reserved for the presentable image */
    VKMIN_ARENA_ALIGN = 256,
    VKMIN_RING_ALIGN = 64
};

_Static_assert(sizeof(DrawCmd) == sizeof(VkDrawIndexedIndirectCommand), "DrawCmd mirrors Vulkan");
_Static_assert(sizeof(Push) <= 128, "Push must fit the guaranteed push constant budget");

typedef struct {
    uint16_t gen;
    bool used;
    VkDeviceSize offset; /* into the arena buffer */
    VkDeviceSize size;
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
} image_slot;

typedef struct {
    uint16_t gen;
    bool used;
    VkPipeline pipe;
    VkPipelineBindPoint bind_point;
} pipe_slot;

typedef struct {
    VkDeviceMemory mem;
    VkDeviceSize cap, used;
    uint32_t type;
} arena;

struct vkmin_ctx {
    vkmin_desc desc;
    bool debug;

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
    uint32_t frame_index;

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
    VkImage offscreen_img;          /* headless backbuffer */
    VkImageView offscreen_view;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    uint32_t swap_count;
    VkImage swap_img[VKMIN_MAX_SWAP];
    VkImageView swap_view[VKMIN_MAX_SWAP];
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

/* id = generation << 20 | (index + 1). Zero is invalid. Nothing is ever freed,
 * so generations stay at 1; the field exists so that a free path, when one
 * arrives, bumps it (never back to 0) and stale handles are caught. */
static uint32_t handle_make(uint32_t index, uint16_t gen) {
    return ((uint32_t)gen << VKMIN_HANDLE_INDEX_BITS) | (index + 1u);
}
static uint32_t handle_index(uint32_t id) {
    return (id & ((1u << VKMIN_HANDLE_INDEX_BITS) - 1u)) - 1u;
}
static uint16_t handle_gen(uint32_t id) { return (uint16_t)(id >> VKMIN_HANDLE_INDEX_BITS); }

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
    fprintf(stderr, "vkmin: device[%u] %s (Vulkan %u.%u)\n", want, props.deviceName,
            VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
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
    const VkPhysicalDeviceFeatures2 f2 = {
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
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &f2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qinfo,
        .enabledExtensionCount = c->desc.headless ? 0u : 1u,
        .ppEnabledExtensionNames = extensions,
    };
    VK_CHECK(vkCreateDevice(c->phys, &info, NULL, &c->dev));
    vkGetDeviceQueue(c->dev, c->queue_family, 0, &c->queue);
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
    VKMIN_FATAL("no memory type with properties 0x%x", want);
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
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
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
    VKMIN_FATAL("bad vkmin_format %d", (int)f);
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
    case VKMIN_USE_TRANSFER_DST:
        return (use_info){VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case VKMIN_USE_TRANSFER_SRC:
        return (use_info){VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
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
    VKMIN_FATAL("bad vkmin_use %d", (int)use);
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
        .size = sizeof(Push),
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
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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

static void create_readback_buffers(vkmin_ctx *c) {
    c->readback_size = (VkDeviceSize)c->extent.width * c->extent.height * 4u;
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

/* ------------------------------------------------------------ swapchain --- */

static void destroy_swapchain(vkmin_ctx *c) {
    for (uint32_t i = 0; i < c->swap_count; ++i) {
        vkDestroyImageView(c->dev, c->swap_view[i], NULL);
        vkDestroySemaphore(c->dev, c->rendered[i], NULL);
    }
    c->swap_count = 0;
    if (c->swapchain) {
        vkDestroySwapchainKHR(c->dev, c->swapchain, NULL);
        c->swapchain = VK_NULL_HANDLE;
    }
}

static void create_swapchain(vkmin_ctx *c) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->phys, c->surface, &caps));
    VKMIN_ASSERT(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                 "surface does not allow TRANSFER_SRC on swapchain images");

    int fb_w = 0, fb_h = 0;
    plat_framebuffer_size(&fb_w, &fb_h);
    c->extent = caps.currentExtent.width != UINT32_MAX
                    ? caps.currentExtent
                    : (VkExtent2D){(uint32_t)fb_w, (uint32_t)fb_h};
    if (c->extent.width < caps.minImageExtent.width) c->extent.width = caps.minImageExtent.width;
    if (c->extent.height < caps.minImageExtent.height) c->extent.height = caps.minImageExtent.height;
    if (c->extent.width > caps.maxImageExtent.width) c->extent.width = caps.maxImageExtent.width;
    if (c->extent.height > caps.maxImageExtent.height) c->extent.height = caps.maxImageExtent.height;

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
    VKMIN_ASSERT(c->backbuffer_format == VK_FORMAT_UNDEFINED || c->backbuffer_format == chosen.format,
                 "swapchain format changed across a recreate (%d -> %d)", (int)c->backbuffer_format,
                 (int)chosen.format);
    c->backbuffer_format = chosen.format;

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
        .imageExtent = c->extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
        create_backbuffer_view(c, c->swap_img[i], chosen.format, &c->swap_view[i], "vkmin.swap");
        const VkSemaphoreCreateInfo sinfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(c->dev, &sinfo, NULL, &c->rendered[i]));
        set_name(c, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)c->rendered[i], "vkmin.rendered[%u]", i);
    }
    fprintf(stderr, "vkmin: swapchain %ux%u, %u images, format %d, present mode %d\n",
            c->extent.width, c->extent.height, c->swap_count, (int)chosen.format, (int)mode);
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
    destroy_readback_buffers(c);
    create_swapchain(c);
    create_readback_buffers(c);
    c->have_submitted = false;
    c->need_recreate = false;
}

/* ----------------------------------------------------------- lifecycle ---- */

vkmin_ctx *vkmin_init(const vkmin_desc *desc) {
    VKMIN_ASSERT(desc != NULL, "vkmin_init: null desc");
    VKMIN_ASSERT(desc->width > 0 && desc->height > 0, "vkmin_init: width and height must be > 0");

    vkmin_ctx *c = calloc(1, sizeof *c);
    VKMIN_ASSERT(c != NULL, "out of memory");
    c->desc = *desc;
#ifdef NDEBUG
    c->debug = false;
#else
    c->debug = true;
#endif
    c->extent = (VkExtent2D){(uint32_t)desc->width, (uint32_t)desc->height};
    c->frames_in_flight = desc->sync_naive ? 1u : 2u;

    if (!desc->headless) {
        VKMIN_ASSERT(plat_window_open(desc->width, desc->height, desc->title ? desc->title : "vkmin"),
                     "could not open a window");
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
    if (desc->headless) {
        create_offscreen(c);
    } else {
        create_swapchain(c);
    }
    create_readback_buffers(c);

    /* Slot 0 is the backbuffer: an external image whose VkImage changes every
     * frame in windowed mode. Its handle is stable for the life of the context. */
    c->images[VKMIN_BACKBUFFER_SLOT] = (image_slot){
        .gen = 1, .used = true, .external = true, .format = c->backbuffer_format,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .w = c->extent.width, .h = c->extent.height, .mips = 1,
        .img = c->offscreen_img, .view = c->offscreen_view, .use = VKMIN_USE_UNDEFINED};
    return c;
}

void vkmin_shutdown(vkmin_ctx *c) {
    if (!c) return;
    VK_CHECK(vkDeviceWaitIdle(c->dev));
    for (uint32_t i = 0; i < VKMIN_MAX_PIPES; ++i) {
        if (c->pipes[i].used) vkDestroyPipeline(c->dev, c->pipes[i].pipe, NULL);
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

bool vkmin_should_close(const vkmin_ctx *c) {
    VKMIN_ASSERT(c != NULL, "vkmin_should_close: null context");
    if (c->desc.headless) return false;
    plat_poll();
    return plat_should_close();
}

bool vkmin_key_hit(const vkmin_ctx *c, int key) {
    VKMIN_ASSERT(c != NULL, "vkmin_key_hit: null context");
    if (c->desc.headless) return false;
    return plat_key_hit(key);
}

uint32_t vkmin_frame_slot(const vkmin_ctx *c) { return c->slot; }

void vkmin_memory_stats(const vkmin_ctx *c, size_t *device_used, size_t *device_cap,
                        size_t *ring_used, size_t *ring_cap) {
    *device_used = (size_t)(c->buf_arena.used + c->img_arena.used);
    *device_cap = (size_t)(c->buf_arena.cap + c->img_arena.cap);
    *ring_used = (size_t)c->ring_head[c->last_slot];
    *ring_cap = (size_t)c->ring_region;
}

/* ------------------------------------------------------------- buffers ---- */

vkmin_buffer vkmin_make_buffer(vkmin_ctx *c, const vkmin_buffer_desc *desc) {
    VKMIN_ASSERT(c && desc && desc->size > 0, "vkmin_make_buffer: bad argument");
    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->buffers, VKMIN_MAX_BUFFERS, index);
    buffer_slot *s = &c->buffers[index];
    s->size = desc->size;
    s->offset = arena_alloc(&c->buf_arena, desc->size, VKMIN_ARENA_ALIGN,
                            desc->label ? desc->label : "buffer");
    const vkmin_buffer b = {handle_make(index, s->gen)};
    if (desc->data) vkmin_buffer_upload(c, b, 0, desc->data, desc->size);
    return b;
}

uint64_t vkmin_buffer_addr(vkmin_ctx *c, vkmin_buffer b) {
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
    const format_info fi = format_lookup(desc->format);
    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->images, VKMIN_MAX_IMAGES, index);
    image_slot *s = &c->images[index];
    s->format = fi.vk;
    s->aspect = fi.aspect;
    s->w = (uint32_t)desc->width;
    s->h = (uint32_t)desc->height;
    s->mips = desc->mip_levels > 0 ? (uint32_t)desc->mip_levels : 1u;
    s->use = VKMIN_USE_UNDEFINED;
    s->external = false;

    VkImageUsageFlags usage = 0;
    if (desc->usage & VKMIN_IMAGE_SAMPLED) usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc->usage & VKMIN_IMAGE_COLOR) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc->usage & VKMIN_IMAGE_DEPTH) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc->usage & VKMIN_IMAGE_TRANSFER_SRC) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VKMIN_ASSERT(usage != 0, "image '%s' has no usage", desc->label ? desc->label : "?");

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
    set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)s->img, "%s", desc->label ? desc->label : "vkmin.image");
    set_name(c, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)s->view, "%s.view", desc->label ? desc->label : "vkmin.image");
    return (vkmin_image){handle_make(index, s->gen)};
}

void vkmin_image_upload(vkmin_ctx *c, vkmin_image img, int mip, const void *data, size_t bytes) {
    VKMIN_ASSERT(c && data, "vkmin_image_upload: null argument");
    image_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, img.id, s);
    VKMIN_ASSERT(mip >= 0 && (uint32_t)mip < s->mips, "mip %d out of range (%u levels)", mip, s->mips);
    const uint32_t mw = s->w >> mip ? s->w >> mip : 1u;
    const uint32_t mh = s->h >> mip ? s->h >> mip : 1u;
    /* The format table knows the exact byte count; a caller with the wrong
     * one has a mip-chain bug and should hear about it now. */
    format_info fi = {.vk = s->format, .aspect = s->aspect, .block_bytes = 4, .block_dim = 1};
    for (int f = 1; f < VKMIN_FMT_COUNT; ++f) {
        const format_info cand = format_lookup((vkmin_format)f);
        if (cand.vk == s->format) fi = cand;
    }
    VKMIN_ASSERT(bytes == mip_bytes(fi, mw, mh), "mip %d of a %ux%u image needs %zu bytes, got %zu",
                 mip, mw, mh, mip_bytes(fi, mw, mh), bytes);
    VKMIN_ASSERT(bytes <= c->ring_cap, "single mip larger than the host ring");

    upload_prepare(c);
    memcpy(c->ring_mapped, data, bytes);
    VkCommandBuffer cmd = imm_begin(c);
    /* Uniform path: each mip lands, then the whole image goes back to SAMPLED.
     * Redundant per mip, correct in every order, and only ever at init. */
    cmd_transition(cmd, s, VKMIN_USE_TRANSFER_DST, false);
    const VkBufferImageCopy copy = {
        .imageSubresource = {.aspectMask = s->aspect, .mipLevel = (uint32_t)mip, .layerCount = 1},
        .imageExtent = {mw, mh, 1},
    };
    vkCmdCopyBufferToImage(cmd, c->ring_buf, s->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    cmd_transition(cmd, s, VKMIN_USE_SAMPLED, false);
    imm_end(c);
}

vkmin_image vkmin_load_png(vkmin_ctx *c, const char *path, bool srgb) {
    VKMIN_ASSERT(c && path, "vkmin_load_png: null argument");
    int w = 0, h = 0;
    unsigned char *pixels = vkmin_png_load(path, &w, &h);
    VKMIN_ASSERT(pixels != NULL, "could not load PNG '%s'", path);
    const vkmin_image img = vkmin_make_image(
        c, &(vkmin_image_desc){.width = w, .height = h, .mip_levels = 1,
                               .format = srgb ? VKMIN_FMT_RGBA8_SRGB : VKMIN_FMT_RGBA8_UNORM,
                               .usage = VKMIN_IMAGE_SAMPLED, .label = path});
    vkmin_image_upload(c, img, 0, pixels, (size_t)w * (size_t)h * 4u);
    vkmin_png_free(pixels);
    return img;
}

vkmin_image vkmin_backbuffer(const vkmin_ctx *c) {
    return (vkmin_image){handle_make(VKMIN_BACKBUFFER_SLOT, c->images[VKMIN_BACKBUFFER_SLOT].gen)};
}

/* ------------------------------------------------------------ pipelines --- */

static VkShaderModule make_module(vkmin_ctx *c, const uint32_t *spv, size_t bytes, const char *label) {
    VKMIN_ASSERT(spv && bytes >= 4 && bytes % 4 == 0, "'%s': bad SPIR-V size %zu", label, bytes);
    VKMIN_ASSERT(spv[0] == 0x07230203u, "'%s': SPIR-V magic missing", label);
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = spv};
    VkShaderModule mod = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(c->dev, &info, NULL, &mod));
    return mod;
}

static VkCompareOp compare_lookup(vkmin_compare cmp) {
    switch (cmp) {
    case VKMIN_CMP_LESS: return VK_COMPARE_OP_LESS;
    case VKMIN_CMP_LESS_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case VKMIN_CMP_EQUAL: return VK_COMPARE_OP_EQUAL;
    case VKMIN_CMP_ALWAYS: return VK_COMPARE_OP_ALWAYS;
    }
    VKMIN_FATAL("bad compare %d", (int)cmp);
}

vkmin_pipe vkmin_make_pipeline(vkmin_ctx *c, const vkmin_pipe_desc *desc) {
    VKMIN_ASSERT(c && desc && desc->vs, "vkmin_make_pipeline: bad argument");
    const char *label = desc->label ? desc->label : "vkmin.pipeline";
    VkShaderModule vs = make_module(c, desc->vs, desc->vs_bytes, label);
    VkShaderModule fs = desc->fs ? make_module(c, desc->fs, desc->fs_bytes, label) : VK_NULL_HANDLE;

    const VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"},
    };
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
        .depthAttachmentFormat = desc->depth ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
    };
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = fs ? 2u : 1u,
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
    VK_CHECK(vkCreateGraphicsPipelines(c->dev, VK_NULL_HANDLE, 1, &info, NULL, &s->pipe));
    set_name(c, VK_OBJECT_TYPE_PIPELINE, (uint64_t)s->pipe, "%s", label);
    vkDestroyShaderModule(c->dev, vs, NULL);
    if (fs) vkDestroyShaderModule(c->dev, fs, NULL);
    return (vkmin_pipe){handle_make(index, s->gen)};
}

vkmin_pipe vkmin_make_compute(vkmin_ctx *c, const uint32_t *spv, size_t bytes, const char *label) {
    VKMIN_ASSERT(c != NULL, "vkmin_make_compute: null context");
    VkShaderModule mod = make_module(c, spv, bytes, label ? label : "compute");
    const VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = mod, .pName = "main"},
        .layout = c->pipe_layout,
    };
    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->pipes, VKMIN_MAX_PIPES, index);
    pipe_slot *s = &c->pipes[index];
    s->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    VK_CHECK(vkCreateComputePipelines(c->dev, VK_NULL_HANDLE, 1, &info, NULL, &s->pipe));
    set_name(c, VK_OBJECT_TYPE_PIPELINE, (uint64_t)s->pipe, "%s", label ? label : "vkmin.compute");
    vkDestroyShaderModule(c->dev, mod, NULL);
    return (vkmin_pipe){handle_make(index, s->gen)};
}

/* ---------------------------------------------------------------- frame --- */

static void backbuffer_bind(vkmin_ctx *c, VkImage img, VkImageView view) {
    image_slot *bb = &c->images[VKMIN_BACKBUFFER_SLOT];
    bb->img = img;
    bb->view = view;
    bb->w = c->extent.width;
    bb->h = c->extent.height;
    bb->format = c->backbuffer_format;
    /* The offscreen image is the same one every frame, so its tracked use
     * (TRANSFER_SRC, from last frame's readback) stays valid and orders this
     * frame's first write after that read. A swapchain image arrives from the
     * presentation engine; the acquire semaphore orders it, and PRESENT is
     * the truthful source state. */
    if (!c->desc.headless) bb->use = VKMIN_USE_PRESENT;
}

void vkmin_frame_begin(vkmin_ctx *c) {
    VKMIN_ASSERT(c != NULL, "vkmin_frame_begin: null context");
    VKMIN_ASSERT(!c->in_frame, "vkmin_frame_begin called twice without a frame_end");

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
        backbuffer_bind(c, c->swap_img[c->swap_index], c->swap_view[c->swap_index]);
    } else {
        backbuffer_bind(c, c->offscreen_img, c->offscreen_view);
    }

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
}

void *vkmin_ring_alloc(vkmin_ctx *c, size_t bytes, uint64_t *addr_out) {
    VKMIN_ASSERT(c && c->in_frame, "vkmin_ring_alloc outside a frame");
    const VkDeviceSize off = align_up(c->ring_head[c->slot], VKMIN_RING_ALIGN);
    VKMIN_ASSERT(off + bytes <= c->ring_region, "host ring region exhausted: %zu bytes at %llu of %llu",
                 bytes, (unsigned long long)off, (unsigned long long)c->ring_region);
    c->ring_head[c->slot] = off + bytes;
    const VkDeviceSize base = c->slot * c->ring_region + off;
    if (addr_out) *addr_out = c->ring_addr + base;
    return c->ring_mapped + base;
}

void vkmin_barrier(vkmin_ctx *c, const vkmin_barrier_desc *desc) {
    VKMIN_ASSERT(c && desc && c->in_frame && !c->in_pass, "vkmin_barrier: must be in a frame, outside a pass");
    VkImageMemoryBarrier2 images[16];
    VKMIN_ASSERT(desc->image_count <= 16, "too many image transitions in one barrier");
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
        mem.srcStageMask |= VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
        mem.srcAccessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mem.dstStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.dstAccessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }
    if (desc->graphics_to_compute) {
        /* Last frame's draws read what this frame's compute is about to
         * overwrite: a write-after-read, so no source access mask needed. */
        mem.srcStageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        mem.dstStageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
        mem.dstAccessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
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
    vkCmdFillBuffer(c->cmd[c->slot], c->arena_buf, s->offset + offset, bytes, value);
}

void vkmin_pass_begin(vkmin_ctx *c, const vkmin_pass_desc *desc) {
    VKMIN_ASSERT(c && desc && c->in_frame && !c->in_pass, "vkmin_pass_begin: bad state");
    VKMIN_ASSERT(desc->color.id || desc->depth.id, "pass with no attachments");
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
}

void vkmin_pass_end(vkmin_ctx *c) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_pass_end without a pass");
    vkCmdEndRendering(c->cmd[c->slot]);
    if (c->fp_label_end) c->fp_label_end(c->cmd[c->slot]);
    c->in_pass = false;
}

void vkmin_set_viewport(vkmin_ctx *c, int x, int y, int w, int h) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_set_viewport outside a pass");
    const VkViewport vp = {.x = (float)x, .y = (float)y, .width = (float)w, .height = (float)h,
                           .minDepth = 0.0f, .maxDepth = 1.0f};
    const VkRect2D sc = {.offset = {x, y}, .extent = {(uint32_t)w, (uint32_t)h}};
    vkCmdSetViewport(c->cmd[c->slot], 0, 1, &vp);
    vkCmdSetScissor(c->cmd[c->slot], 0, 1, &sc);
}

void vkmin_set_depth_bias(vkmin_ctx *c, float constant, float slope) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_set_depth_bias outside a pass");
    vkCmdSetDepthBias(c->cmd[c->slot], constant, 0.0f, slope);
}

void vkmin_bind_pipeline(vkmin_ctx *c, vkmin_pipe p) {
    VKMIN_ASSERT(c && c->in_frame, "vkmin_bind_pipeline outside a frame");
    const pipe_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->pipes, VKMIN_MAX_PIPES, p.id, s);
    VKMIN_ASSERT((s->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) == c->in_pass,
                 "graphics pipelines bind inside a pass, compute outside");
    vkCmdBindPipeline(c->cmd[c->slot], s->bind_point, s->pipe);
}

void vkmin_push(vkmin_ctx *c, const Push *push) {
    VKMIN_ASSERT(c && push && c->in_frame, "vkmin_push: bad state");
    vkCmdPushConstants(c->cmd[c->slot], c->pipe_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof *push, push);
}

void vkmin_bind_index_buffer(vkmin_ctx *c, vkmin_buffer b, size_t offset) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_bind_index_buffer outside a pass");
    const buffer_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, b.id, s);
    vkCmdBindIndexBuffer(c->cmd[c->slot], c->arena_buf, s->offset + offset, VK_INDEX_TYPE_UINT32);
}

void vkmin_draw(vkmin_ctx *c, uint32_t vertices, uint32_t instances) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_draw outside a pass");
    vkCmdDraw(c->cmd[c->slot], vertices, instances, 0, 0);
}

void vkmin_draw_indexed_indirect(vkmin_ctx *c, vkmin_buffer cmds, size_t offset, uint32_t count) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_draw_indexed_indirect outside a pass");
    if (count == 0) return;
    const buffer_slot *s = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, cmds.id, s);
    vkCmdDrawIndexedIndirect(c->cmd[c->slot], c->arena_buf, s->offset + offset, count, sizeof(DrawCmd));
}

void vkmin_draw_indexed_indirect_count(vkmin_ctx *c, vkmin_buffer cmds, size_t cmd_offset,
                                       vkmin_buffer counts, size_t count_offset, uint32_t max_draws) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_draw_indexed_indirect_count outside a pass");
    const buffer_slot *cs = NULL, *ns = NULL;
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, cmds.id, cs);
    VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, counts.id, ns);
    vkCmdDrawIndexedIndirectCount(c->cmd[c->slot], c->arena_buf, cs->offset + cmd_offset, c->arena_buf,
                                  ns->offset + count_offset, max_draws, sizeof(DrawCmd));
}

void vkmin_draw_indexed_indirect_host(vkmin_ctx *c, uint64_t ring_addr, uint32_t count) {
    VKMIN_ASSERT(c && c->in_pass, "vkmin_draw_indexed_indirect_host outside a pass");
    if (count == 0) return;
    VKMIN_ASSERT(ring_addr >= c->ring_addr && ring_addr < c->ring_addr + c->ring_cap,
                 "indirect commands are not in the ring buffer");
    vkCmdDrawIndexedIndirect(c->cmd[c->slot], c->ring_buf, ring_addr - c->ring_addr, count, sizeof(DrawCmd));
}

void vkmin_dispatch(vkmin_ctx *c, uint32_t x, uint32_t y, uint32_t z) {
    VKMIN_ASSERT(c && c->in_frame && !c->in_pass, "vkmin_dispatch must be outside a pass");
    vkCmdDispatch(c->cmd[c->slot], x, y, z);
}

void vkmin_timestamp(vkmin_ctx *c, int index) {
    VKMIN_ASSERT(c && c->in_frame && index >= 0 && index < VKMIN_MAX_TIMESTAMPS, "vkmin_timestamp: bad index");
    vkCmdWriteTimestamp2(c->cmd[c->slot], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, c->query_pool,
                         c->slot * VKMIN_MAX_TIMESTAMPS + (uint32_t)index);
    if (index + 1 > c->ts_written[c->slot]) c->ts_written[c->slot] = index + 1;
}

int vkmin_timestamps_read(const vkmin_ctx *c, double *ms_since_first, int cap) {
    const int n = c->ts_count < cap ? c->ts_count : cap;
    for (int i = 0; i < n; ++i) ms_since_first[i] = c->ts_ms[i];
    return n;
}

void vkmin_frame_end(vkmin_ctx *c) {
    VKMIN_ASSERT(c && c->in_frame && !c->in_pass, "vkmin_frame_end: bad state");
    VkCommandBuffer cmd = c->cmd[c->slot];
    image_slot *bb = &c->images[VKMIN_BACKBUFFER_SLOT];

    /* Execute then inhibit: every frame copies the backbuffer into the mapped
     * readback buffer whether or not anyone asks for a PNG. It costs one copy
     * and buys one execution path, and a capture that never reads a swapchain
     * image after it has been handed to the presentation engine. */
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
    if (!c->desc.headless) cmd_transition(cmd, bb, VKMIN_USE_PRESENT, false);
    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkCommandBufferSubmitInfo cmd_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                                                .commandBuffer = cmd};
    const VkSemaphoreSubmitInfo wait = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                        .semaphore = c->acquired[c->slot],
                                        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
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
        if (w > 0 && h > 0 && ((uint32_t)w != c->extent.width || (uint32_t)h != c->extent.height)) {
            c->need_recreate = true;
        }
    }
    if (c->desc.sync_naive) VK_CHECK(vkDeviceWaitIdle(c->dev));
    c->slot = (c->slot + 1u) % c->frames_in_flight;
    c->frame_index++;
    c->in_frame = false;
}

/* -------------------------------------------------------------- readback -- */

bool vkmin_save_png(vkmin_ctx *c, const char *path) {
    VKMIN_ASSERT(c && path && !c->in_frame, "vkmin_save_png: call it between frames");
    if (!c->have_submitted) {
        fprintf(stderr, "vkmin: nothing rendered yet, not writing '%s'\n", path);
        return false;
    }
    VK_CHECK(vkWaitForFences(c->dev, 1, &c->fence[c->last_slot], VK_TRUE, UINT64_MAX));
    c->fence_pending[c->last_slot] = false;

    const uint32_t w = c->extent.width, h = c->extent.height;
    const size_t bytes = (size_t)c->readback_size;
    const int row_pitch = (int)(w * 4u); /* bufferRowLength 0 above: tightly packed */
    unsigned char *rgba = malloc(bytes);
    VKMIN_ASSERT(rgba != NULL, "out of memory writing '%s'", path);
    memcpy(rgba, c->readback_mapped[c->last_slot], bytes);
    const bool bgra = c->backbuffer_format == VK_FORMAT_B8G8R8A8_UNORM ||
                      c->backbuffer_format == VK_FORMAT_B8G8R8A8_SRGB;
    if (bgra) {
        for (size_t i = 0; i < bytes; i += 4) {
            const unsigned char b = rgba[i];
            rgba[i] = rgba[i + 2];
            rgba[i + 2] = b;
        }
    }
    for (size_t i = 3; i < bytes; i += 4) rgba[i] = 255; /* the PNG is opaque by definition */
    const bool ok = vkmin_png_write(path, (int)w, (int)h, rgba, row_pitch);
    free(rgba);
    if (!ok) fprintf(stderr, "vkmin: failed to write '%s'\n", path);
    return ok;
}
