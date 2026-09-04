/* vkmin.c -- see vkmin.h. Vulkan 1.3 core only: dynamic rendering and
 * synchronization2 are required, not probed for. There is no VkRenderPass and
 * no VkFramebuffer anywhere in this file, and no `if (extension_supported)`
 * branch either; an inadequate device fails loudly at init.
 */
#include "vkmin.h"
#include "plat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include "stb_bridge.h"

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
    VKMIN_MAX_BUFFERS = 32,
    VKMIN_MAX_IMAGES = 16,
    VKMIN_MAX_SHADERS = 16,
    VKMIN_MAX_PIPES = 8,
    VKMIN_MAX_FRAMES = 2,
    VKMIN_MAX_SWAP = 8,
    VKMIN_HANDLE_INDEX_BITS = 24
};

typedef struct {
    uint8_t gen;
    bool used;
    VkBuffer buf;
    VkDeviceMemory mem;
    VkDeviceSize size;
} buffer_slot;

typedef struct {
    uint8_t gen;
    bool used;
    VkImage img;
    VkDeviceMemory mem;
    VkImageView view;
    VkDescriptorSet set;
} image_slot;

typedef struct {
    uint8_t gen;
    bool used;
    VkShaderModule mod;
} shader_slot;

typedef struct {
    uint8_t gen;
    bool used;
    VkPipeline pipe;
} pipe_slot;

struct vkmin_ctx {
    vkmin_desc desc;
    bool debug;

    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    PFN_vkSetDebugUtilsObjectNameEXT fp_set_name;
    PFN_vkDestroyDebugUtilsMessengerEXT fp_destroy_messenger;

    VkPhysicalDevice phys;
    VkPhysicalDeviceMemoryProperties mem_props;
    uint32_t queue_family;
    VkDevice dev;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd[VKMIN_MAX_FRAMES];
    VkFence fence[VKMIN_MAX_FRAMES];
    VkSemaphore acquired[VKMIN_MAX_FRAMES];
    bool fence_pending[VKMIN_MAX_FRAMES];
    uint32_t frames_in_flight;
    uint32_t slot;         /* in-flight slot being recorded */
    uint32_t last_slot;    /* slot of the most recently submitted frame */
    bool have_submitted;
    bool in_frame;

    VkCommandBuffer imm_cmd;
    VkFence imm_fence;

    VkExtent2D extent;
    VkFormat color_format;
    VkFormat depth_format;

    /* Headless colour target. Unused when a swapchain exists. */
    VkImage color_img;
    VkDeviceMemory color_mem;
    VkImageView color_view;

    VkImage depth_img;
    VkDeviceMemory depth_mem;
    VkImageView depth_view;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    uint32_t swap_count;
    VkImage swap_img[VKMIN_MAX_SWAP];
    VkImageView swap_view[VKMIN_MAX_SWAP];
    VkSemaphore rendered[VKMIN_MAX_SWAP]; /* per image, not per frame: a present
                                           * may still be reading last frame's */
    uint32_t swap_index;
    bool index_bound;
    bool need_recreate;

    /* Persistently mapped, one per frame in flight: every frame copies its
     * colour target in here before presenting, whether or not anyone asks for
     * a PNG. One shared buffer would be a write-after-write hazard between
     * frames -- synchronization validation says so, loudly. */
    VkBuffer readback_buf[VKMIN_MAX_FRAMES];
    VkDeviceMemory readback_mem[VKMIN_MAX_FRAMES];
    void *readback_mapped[VKMIN_MAX_FRAMES];
    VkDeviceSize readback_size;

    VkSampler sampler;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipe_layout;
    VkDescriptorPool desc_pool;
    vkmin_image white;

    buffer_slot buffers[VKMIN_MAX_BUFFERS];
    image_slot images[VKMIN_MAX_IMAGES];
    shader_slot shaders[VKMIN_MAX_SHADERS];
    pipe_slot pipes[VKMIN_MAX_PIPES];
};

/* ------------------------------------------------------------- handles ---- */

/* id = generation<<24 | (index+1). Zero is invalid, and a generation bump on
 * free means a stale handle is caught instead of aliasing a recycled slot. */
static uint32_t handle_make(uint32_t index, uint8_t gen) {
    return ((uint32_t)gen << VKMIN_HANDLE_INDEX_BITS) | (index + 1u);
}

static uint32_t handle_index(uint32_t id) {
    return (id & ((1u << VKMIN_HANDLE_INDEX_BITS) - 1u)) - 1u;
}

static uint8_t handle_gen(uint32_t id) {
    return (uint8_t)(id >> VKMIN_HANDLE_INDEX_BITS);
}

/* There is deliberately no destroy-resource API: everything a demo makes lives
 * until vkmin_shutdown. When one arrives, freeing a slot must bump its
 * generation -- (gen == 255 ? 1 : gen + 1), never back to 0 -- or a handle to
 * the freed slot will silently alias its replacement. */

/* One lookup body for every pool: the slot arrays differ only in element type,
 * so the macro pins index, generation and liveness checks in a single place. */
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
                     handle_gen(id), (pool)[idx__].gen);                          \
        (out) = &(pool)[idx__];                                                   \
    } while (0)

/* --------------------------------------------------------- debug naming --- */

static void set_name(vkmin_ctx *c, VkObjectType type, uint64_t handle, const char *fmt, ...) {
    if (!c->fp_set_name || handle == 0) return;
    char buf[96];
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

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
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
    /* Chained into instance creation so that messages from vkCreateInstance and
     * vkDestroyInstance are caught too, not just the ones in between. */
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
     * core checks -- exactly the cross-submit barrier reasoning the frame loop
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
        VKMIN_ASSERT(create && c->fp_destroy_messenger, "debug utils entry points missing");
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
                 "device '%s' reports Vulkan %u.%u; vkmin requires 1.3 core",
                 props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
                 VK_VERSION_MINOR(props.apiVersion));
    VKMIN_ASSERT(props.limits.maxPushConstantsSize >= VKMIN_MAX_PUSH,
                 "device '%s' offers only %u push constant bytes", props.deviceName,
                 props.limits.maxPushConstantsSize);

    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                    .pNext = &f13};
    vkGetPhysicalDeviceFeatures2(c->phys, &f2);
    VKMIN_ASSERT(f13.dynamicRendering, "device '%s' lacks dynamicRendering", props.deviceName);
    VKMIN_ASSERT(f13.synchronization2, "device '%s' lacks synchronization2", props.deviceName);

    vkGetPhysicalDeviceMemoryProperties(c->phys, &c->mem_props);

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &qn, NULL);
    VkQueueFamilyProperties qprops[16];
    if (qn > 16) qn = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &qn, qprops);
    c->queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < qn; ++i) {
        const VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT;
        if ((qprops[i].queueFlags & need) == need) {
            c->queue_family = i;
            break;
        }
    }
    VKMIN_ASSERT(c->queue_family != UINT32_MAX, "no graphics+transfer queue family");
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
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    const VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                          .pNext = &f13};
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

static uint32_t find_memory_type(const vkmin_ctx *c, uint32_t type_bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < c->mem_props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (c->mem_props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    VKMIN_FATAL("no memory type with properties 0x%x", want);
}

/* One VkDeviceMemory per resource. Vulkan guarantees 4096 allocations and the
 * cube demo needs about six. TODO: a suballocator, as a parallel
 * implementation, if a scene ever needs hundreds of resources. */
static void create_raw_buffer(vkmin_ctx *c, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags mem_flags, VkBuffer *out_buf,
                              VkDeviceMemory *out_mem) {
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(c->dev, &info, NULL, out_buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c->dev, *out_buf, &req);
    const VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(c, req.memoryTypeBits, mem_flags),
    };
    VK_CHECK(vkAllocateMemory(c->dev, &alloc, NULL, out_mem));
    VK_CHECK(vkBindBufferMemory(c->dev, *out_buf, *out_mem, 0));
}

static void create_raw_image(vkmin_ctx *c, uint32_t w, uint32_t h, VkFormat format,
                             VkImageUsageFlags usage, VkImageAspectFlags aspect,
                             VkImage *out_img, VkDeviceMemory *out_mem, VkImageView *out_view) {
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = w, .height = h, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(c->dev, &info, NULL, out_img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c->dev, *out_img, &req);
    const VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(c, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VK_CHECK(vkAllocateMemory(c->dev, &alloc, NULL, out_mem));
    VK_CHECK(vkBindImageMemory(c->dev, *out_img, *out_mem, 0));

    const VkImageViewCreateInfo vinfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *out_img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = aspect, .levelCount = 1, .layerCount = 1},
    };
    VK_CHECK(vkCreateImageView(c->dev, &vinfo, NULL, out_view));
}

/* Every layout transition in this file goes through here, so there is exactly
 * one place where a wrong stage or access mask can hide. */
static void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                          VkImageLayout old_layout, VkImageLayout new_layout,
                          VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                          VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    const VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = aspect, .levelCount = 1, .layerCount = 1},
    };
    const VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

/* Record-submit-wait on a dedicated command buffer. Used only outside the
 * frame loop: uploads, and readback. */
static VkCommandBuffer imm_begin(vkmin_ctx *c) {
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
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = c->imm_cmd,
    };
    const VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmd_info,
    };
    VK_CHECK(vkResetFences(c->dev, 1, &c->imm_fence));
    VK_CHECK(vkQueueSubmit2(c->queue, 1, &submit, c->imm_fence));
    VK_CHECK(vkWaitForFences(c->dev, 1, &c->imm_fence, VK_TRUE, UINT64_MAX));
}

static void create_readback_buffers(vkmin_ctx *c) {
    c->readback_size = (VkDeviceSize)c->extent.width * c->extent.height * 4u;
    for (uint32_t i = 0; i < VKMIN_MAX_FRAMES; ++i) {
        create_raw_buffer(c, c->readback_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &c->readback_buf[i], &c->readback_mem[i]);
        VK_CHECK(vkMapMemory(c->dev, c->readback_mem[i], 0, c->readback_size, 0,
                             &c->readback_mapped[i]));
        set_name(c, VK_OBJECT_TYPE_BUFFER, (uint64_t)c->readback_buf[i], "vkmin.readback[%u]", i);
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
    c->readback_size = 0;
}

/* -------------------------------------------------------------- targets --- */

static void create_depth_target(vkmin_ctx *c) {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(c->phys, VK_FORMAT_D32_SFLOAT, &fp);
    VKMIN_ASSERT(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 "device cannot use D32_SFLOAT as a depth attachment");
    c->depth_format = VK_FORMAT_D32_SFLOAT;
    create_raw_image(c, c->extent.width, c->extent.height, c->depth_format,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                     &c->depth_img, &c->depth_mem, &c->depth_view);
    set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)c->depth_img, "vkmin.depth");
    set_name(c, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)c->depth_view, "vkmin.depth.view");
}

static void destroy_depth_target(vkmin_ctx *c) {
    vkDestroyImageView(c->dev, c->depth_view, NULL);
    vkDestroyImage(c->dev, c->depth_img, NULL);
    vkFreeMemory(c->dev, c->depth_mem, NULL);
    c->depth_view = VK_NULL_HANDLE;
    c->depth_img = VK_NULL_HANDLE;
    c->depth_mem = VK_NULL_HANDLE;
}

static void create_offscreen_target(vkmin_ctx *c) {
    c->color_format = VK_FORMAT_R8G8B8A8_UNORM;
    create_raw_image(c, c->extent.width, c->extent.height, c->color_format,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, &c->color_img, &c->color_mem, &c->color_view);
    set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)c->color_img, "vkmin.offscreen.color");
    set_name(c, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)c->color_view, "vkmin.offscreen.color.view");
}

/* ------------------------------------------------------------ swapchain --- */

static void destroy_swapchain(vkmin_ctx *c) {
    for (uint32_t i = 0; i < c->swap_count; ++i) {
        vkDestroyImageView(c->dev, c->swap_view[i], NULL);
        vkDestroySemaphore(c->dev, c->rendered[i], NULL);
        c->swap_view[i] = VK_NULL_HANDLE;
        c->rendered[i] = VK_NULL_HANDLE;
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
                 "surface does not allow TRANSFER_SRC on swapchain images; "
                 "vkmin needs it to save a PNG of the window");

    int fb_w = 0, fb_h = 0;
    plat_framebuffer_size(&fb_w, &fb_h);
    c->extent = caps.currentExtent.width != UINT32_MAX
                    ? caps.currentExtent
                    : (VkExtent2D){.width = (uint32_t)fb_w, .height = (uint32_t)fb_h};
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
    /* Pipelines bake the colour format in at creation time, so a recreate that
     * changed it would leave every existing pipeline quietly wrong. Fail loudly
     * instead; nothing here has ever seen it happen. */
    VKMIN_ASSERT(c->color_format == VK_FORMAT_UNDEFINED || c->color_format == chosen.format,
                 "swapchain format changed from %d to %d across a recreate",
                 (int)c->color_format, (int)chosen.format);
    c->color_format = chosen.format;

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
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, /* always supported, no tearing */
        .clipped = VK_TRUE,
    };
    VK_CHECK(vkCreateSwapchainKHR(c->dev, &info, NULL, &c->swapchain));

    c->swap_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(c->dev, c->swapchain, &c->swap_count, NULL));
    VKMIN_ASSERT(c->swap_count <= VKMIN_MAX_SWAP, "swapchain returned %u images, cap is %u",
                 c->swap_count, (unsigned)VKMIN_MAX_SWAP);
    VK_CHECK(vkGetSwapchainImagesKHR(c->dev, c->swapchain, &c->swap_count, c->swap_img));

    for (uint32_t i = 0; i < c->swap_count; ++i) {
        const VkImageViewCreateInfo vinfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = c->swap_img[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = chosen.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1},
        };
        VK_CHECK(vkCreateImageView(c->dev, &vinfo, NULL, &c->swap_view[i]));
        const VkSemaphoreCreateInfo sinfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(c->dev, &sinfo, NULL, &c->rendered[i]));
        set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)c->swap_img[i], "vkmin.swap[%u]", i);
        set_name(c, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)c->rendered[i], "vkmin.rendered[%u]", i);
    }
    fprintf(stderr, "vkmin: swapchain %ux%u, %u images, format %d\n", c->extent.width,
            c->extent.height, c->swap_count, (int)chosen.format);
}

/* The single recreate site. Everything else just raises need_recreate. */
static void recreate_swapchain(vkmin_ctx *c) {
    int w = 0, h = 0;
    plat_framebuffer_size(&w, &h);
    while ((w == 0 || h == 0) && !plat_should_close()) { /* minimised: nothing to draw into */
        plat_poll();
        plat_framebuffer_size(&w, &h);
    }
    VK_CHECK(vkDeviceWaitIdle(c->dev));
    destroy_swapchain(c);
    destroy_depth_target(c);
    destroy_readback_buffers(c);
    create_swapchain(c);
    create_depth_target(c);
    create_readback_buffers(c);
    /* The readback buffers are new and empty, and the old ones described a
     * different size: there is no last completed frame to save any more. */
    c->have_submitted = false;
    c->need_recreate = false;
}

/* ------------------------------------------------ descriptors & sampler --- */

static void create_binding_plumbing(vkmin_ctx *c) {
    const VkSamplerCreateInfo sinfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };
    VK_CHECK(vkCreateSampler(c->dev, &sinfo, NULL, &c->sampler));

    /* Exactly one set layout and one pipeline layout exist. Pipelines that do
     * not sample anything still bind the default white image, so there is no
     * "does this pipeline have a texture" branch anywhere. */
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo linfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(c->dev, &linfo, NULL, &c->set_layout));

    const VkPushConstantRange push = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = VKMIN_MAX_PUSH,
    };
    const VkPipelineLayoutCreateInfo pinfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &c->set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    VK_CHECK(vkCreatePipelineLayout(c->dev, &pinfo, NULL, &c->pipe_layout));

    const VkDescriptorPoolSize size = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .descriptorCount = VKMIN_MAX_IMAGES};
    const VkDescriptorPoolCreateInfo dpinfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = VKMIN_MAX_IMAGES,
        .poolSizeCount = 1,
        .pPoolSizes = &size,
    };
    VK_CHECK(vkCreateDescriptorPool(c->dev, &dpinfo, NULL, &c->desc_pool));

    set_name(c, VK_OBJECT_TYPE_SAMPLER, (uint64_t)c->sampler, "vkmin.sampler");
    set_name(c, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)c->pipe_layout, "vkmin.pipeline_layout");
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
    c->extent = (VkExtent2D){.width = (uint32_t)desc->width, .height = (uint32_t)desc->height};
    /* The reference path: one frame in flight and a device wait after every
     * submit. Kept permanently so a flicker can be bisected in ten seconds. */
    c->frames_in_flight = desc->sync_naive ? 1u : 2u;

    if (!desc->headless) {
        VKMIN_ASSERT(plat_window_open(desc->width, desc->height,
                                      desc->title ? desc->title : "vkmin"),
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
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(c->phys, c->queue_family, c->surface,
                                                      &supported));
        VKMIN_ASSERT(supported, "queue family %u cannot present to this surface",
                     c->queue_family);
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

    if (desc->headless) {
        create_offscreen_target(c);
    } else {
        create_swapchain(c);
    }
    create_depth_target(c);
    create_readback_buffers(c);
    create_binding_plumbing(c);

    /* Bound wherever a pipeline does not want a texture of its own. */
    const uint8_t white_pixels[4] = {255, 255, 255, 255};
    c->white = vkmin_make_image(c, &(vkmin_image_desc){.width = 1,
                                                       .height = 1,
                                                       .pixels = white_pixels,
                                                       .label = "vkmin.white"});
    return c;
}

void vkmin_shutdown(vkmin_ctx *c) {
    if (!c) return;
    VK_CHECK(vkDeviceWaitIdle(c->dev));

    for (uint32_t i = 0; i < VKMIN_MAX_PIPES; ++i) {
        if (c->pipes[i].used) vkDestroyPipeline(c->dev, c->pipes[i].pipe, NULL);
    }
    for (uint32_t i = 0; i < VKMIN_MAX_SHADERS; ++i) {
        if (c->shaders[i].used) vkDestroyShaderModule(c->dev, c->shaders[i].mod, NULL);
    }
    for (uint32_t i = 0; i < VKMIN_MAX_IMAGES; ++i) {
        if (!c->images[i].used) continue;
        vkDestroyImageView(c->dev, c->images[i].view, NULL);
        vkDestroyImage(c->dev, c->images[i].img, NULL);
        vkFreeMemory(c->dev, c->images[i].mem, NULL);
    }
    for (uint32_t i = 0; i < VKMIN_MAX_BUFFERS; ++i) {
        if (!c->buffers[i].used) continue;
        vkDestroyBuffer(c->dev, c->buffers[i].buf, NULL);
        vkFreeMemory(c->dev, c->buffers[i].mem, NULL);
    }

    vkDestroyDescriptorPool(c->dev, c->desc_pool, NULL);
    vkDestroyPipelineLayout(c->dev, c->pipe_layout, NULL);
    vkDestroyDescriptorSetLayout(c->dev, c->set_layout, NULL);
    vkDestroySampler(c->dev, c->sampler, NULL);

    destroy_readback_buffers(c);
    destroy_depth_target(c);
    if (c->color_img) {
        vkDestroyImageView(c->dev, c->color_view, NULL);
        vkDestroyImage(c->dev, c->color_img, NULL);
        vkFreeMemory(c->dev, c->color_mem, NULL);
    }
    destroy_swapchain(c);

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

/* ------------------------------------------------------------ resources --- */

vkmin_buffer vkmin_make_buffer(vkmin_ctx *c, const vkmin_buffer_desc *desc) {
    VKMIN_ASSERT(c && desc, "vkmin_make_buffer: null argument");
    VKMIN_ASSERT(desc->data && desc->size > 0, "vkmin_make_buffer: buffers need initial data");

    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->buffers, VKMIN_MAX_BUFFERS, index);
    buffer_slot *slot = &c->buffers[index];
    slot->size = desc->size;

    const VkBufferUsageFlags kind = desc->type == VKMIN_BUFFER_INDEX
                                        ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                        : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    create_raw_buffer(c, desc->size, kind | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &slot->buf, &slot->mem);
    set_name(c, VK_OBJECT_TYPE_BUFFER, (uint64_t)slot->buf, "%s",
             desc->label ? desc->label : "vkmin.buffer");

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    create_raw_buffer(c, desc->size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &staging_mem);
    void *mapped = NULL;
    VK_CHECK(vkMapMemory(c->dev, staging_mem, 0, desc->size, 0, &mapped));
    memcpy(mapped, desc->data, desc->size);
    vkUnmapMemory(c->dev, staging_mem);

    VkCommandBuffer cmd = imm_begin(c);
    const VkBufferCopy copy = {.size = desc->size};
    vkCmdCopyBuffer(cmd, staging, slot->buf, 1, &copy);
    imm_end(c);

    vkDestroyBuffer(c->dev, staging, NULL);
    vkFreeMemory(c->dev, staging_mem, NULL);
    return (vkmin_buffer){handle_make(index, slot->gen)};
}

vkmin_image vkmin_make_image(vkmin_ctx *c, const vkmin_image_desc *desc) {
    VKMIN_ASSERT(c && desc, "vkmin_make_image: null argument");
    VKMIN_ASSERT(desc->width > 0 && desc->height > 0 && desc->pixels,
                 "vkmin_make_image: needs a size and pixels");

    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->images, VKMIN_MAX_IMAGES, index);
    image_slot *slot = &c->images[index];
    const uint32_t w = (uint32_t)desc->width, h = (uint32_t)desc->height;
    const VkDeviceSize bytes = (VkDeviceSize)w * h * 4u;

    create_raw_image(c, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, &slot->img, &slot->mem, &slot->view);
    set_name(c, VK_OBJECT_TYPE_IMAGE, (uint64_t)slot->img, "%s",
             desc->label ? desc->label : "vkmin.image");

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    create_raw_buffer(c, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &staging_mem);
    void *mapped = NULL;
    VK_CHECK(vkMapMemory(c->dev, staging_mem, 0, bytes, 0, &mapped));
    memcpy(mapped, desc->pixels, (size_t)bytes);
    vkUnmapMemory(c->dev, staging_mem);

    VkCommandBuffer cmd = imm_begin(c);
    image_barrier(cmd, slot->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkBufferImageCopy copy = {
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {.width = w, .height = h, .depth = 1},
    };
    vkCmdCopyBufferToImage(cmd, staging, slot->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_barrier(cmd, slot->img, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    imm_end(c);

    vkDestroyBuffer(c->dev, staging, NULL);
    vkFreeMemory(c->dev, staging_mem, NULL);

    /* One descriptor set per image, written once. There is no per-frame
     * descriptor churn anywhere in this codebase. */
    const VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = c->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &c->set_layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(c->dev, &alloc, &slot->set));
    const VkDescriptorImageInfo image_info = {
        .sampler = c->sampler,
        .imageView = slot->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = slot->set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };
    vkUpdateDescriptorSets(c->dev, 1, &write, 0, NULL);
    return (vkmin_image){handle_make(index, slot->gen)};
}

vkmin_image vkmin_load_png(vkmin_ctx *c, const char *path) {
    VKMIN_ASSERT(c && path, "vkmin_load_png: null argument");
    int w = 0, h = 0;
    unsigned char *pixels = vkmin_png_load(path, &w, &h);
    VKMIN_ASSERT(pixels != NULL, "could not load PNG '%s'", path);
    const vkmin_image image = vkmin_make_image(
        c, &(vkmin_image_desc){.width = w, .height = h, .pixels = pixels, .label = path});
    vkmin_png_free(pixels);
    return image;
}

vkmin_shader vkmin_make_shader(vkmin_ctx *c, const uint32_t *spv, size_t bytes) {
    VKMIN_ASSERT(c && spv && bytes >= 4, "vkmin_make_shader: null or empty SPIR-V");
    VKMIN_ASSERT(bytes % 4 == 0, "SPIR-V size %zu is not a multiple of 4", bytes);
    VKMIN_ASSERT(spv[0] == 0x07230203u, "SPIR-V magic missing (got 0x%08x)", spv[0]);

    uint32_t index = 0;
    VKMIN_SLOT_ALLOC(c->shaders, VKMIN_MAX_SHADERS, index);
    shader_slot *slot = &c->shaders[index];
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes,
        .pCode = spv,
    };
    VK_CHECK(vkCreateShaderModule(c->dev, &info, NULL, &slot->mod));
    return (vkmin_shader){handle_make(index, slot->gen)};
}

static VkFormat attr_format(vkmin_attr_type type) {
    switch (type) {
    case VKMIN_ATTR_FLOAT2: return VK_FORMAT_R32G32_SFLOAT;
    case VKMIN_ATTR_FLOAT3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VKMIN_ATTR_FLOAT4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VKMIN_ATTR_NONE: break;
    }
    VKMIN_FATAL("bad vertex attribute type %d", (int)type);
}

vkmin_pipe vkmin_make_pipeline(vkmin_ctx *c, const vkmin_pipe_desc *desc) {
    VKMIN_ASSERT(c && desc, "vkmin_make_pipeline: null argument");
    const shader_slot *vs = NULL, *fs = NULL;
    VKMIN_SLOT_LOOKUP(c->shaders, VKMIN_MAX_SHADERS, desc->vs.id, vs);
    VKMIN_SLOT_LOOKUP(c->shaders, VKMIN_MAX_SHADERS, desc->fs.id, fs);

    VkVertexInputAttributeDescription attrs[VKMIN_MAX_ATTRS];
    uint32_t attr_count = 0;
    for (uint32_t i = 0; i < VKMIN_MAX_ATTRS; ++i) {
        if (desc->attrs[i].type == VKMIN_ATTR_NONE) break;
        attrs[attr_count] = (VkVertexInputAttributeDescription){
            .location = attr_count,
            .binding = 0,
            .format = attr_format(desc->attrs[i].type),
            .offset = desc->attrs[i].offset,
        };
        ++attr_count;
    }
    VKMIN_ASSERT(attr_count == 0 || desc->vertex_stride > 0,
                 "pipeline has vertex attributes but no vertex_stride");

    const VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = desc->vertex_stride,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = attr_count ? 1u : 0u,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = attr_count,
        .pVertexAttributeDescriptions = attrs,
    };
    const VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vs->mod,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fs->mod,
         .pName = "main"},
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    /* Viewport and scissor are dynamic, so a resize never rebuilds a pipeline. */
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = desc->cull_off ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc->depth_test,
        .depthWriteEnable = desc->depth_test,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .maxDepthBounds = 1.0f,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };
    /* Dynamic rendering: no VkRenderPass, no VkFramebuffer, anywhere. */
    const VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &c->color_format,
        .depthAttachmentFormat = c->depth_format,
    };
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
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
    pipe_slot *slot = &c->pipes[index];
    VK_CHECK(vkCreateGraphicsPipelines(c->dev, VK_NULL_HANDLE, 1, &info, NULL, &slot->pipe));
    set_name(c, VK_OBJECT_TYPE_PIPELINE, (uint64_t)slot->pipe, "%s",
             desc->label ? desc->label : "vkmin.pipeline");
    return (vkmin_pipe){handle_make(index, slot->gen)};
}

/* ---------------------------------------------------------------- frame --- */

static VkImage frame_color_image(const vkmin_ctx *c) {
    return c->desc.headless ? c->color_img : c->swap_img[c->swap_index];
}

static VkImageView frame_color_view(const vkmin_ctx *c) {
    return c->desc.headless ? c->color_view : c->swap_view[c->swap_index];
}

void vkmin_frame_begin(vkmin_ctx *c, vkmin_clear clear) {
    VKMIN_ASSERT(c != NULL, "vkmin_frame_begin: null context");
    VKMIN_ASSERT(!c->in_frame, "vkmin_frame_begin called twice without a frame_end");

    if (c->fence_pending[c->slot]) {
        VK_CHECK(vkWaitForFences(c->dev, 1, &c->fence[c->slot], VK_TRUE, UINT64_MAX));
        c->fence_pending[c->slot] = false;
    }
    VK_CHECK(vkResetFences(c->dev, 1, &c->fence[c->slot]));

    if (!c->desc.headless) {
        /* The one and only recreate site: a flag checked at a fixed point,
         * then an unconditional rebuild. Nothing scatters VK_ERROR_OUT_OF_DATE
         * handling across the call sites. */
        if (c->need_recreate) recreate_swapchain(c);
        VkResult r = vkAcquireNextImageKHR(c->dev, c->swapchain, UINT64_MAX,
                                           c->acquired[c->slot], VK_NULL_HANDLE, &c->swap_index);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain(c);
            r = vkAcquireNextImageKHR(c->dev, c->swapchain, UINT64_MAX, c->acquired[c->slot],
                                      VK_NULL_HANDLE, &c->swap_index);
        }
        if (r != VK_SUBOPTIMAL_KHR) VK_CHECK(r);
    }

    VkCommandBuffer cmd = c->cmd[c->slot];
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    /* Both targets start UNDEFINED every frame: the contents are always fully
     * replaced by the clear, and discarding beats preserving. The src masks
     * name the previous frame's last use of each image, which is what orders
     * this frame against the one before it on the same queue. */
    image_barrier(cmd, frame_color_image(c), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, c->depth_img, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    const VkRenderingAttachmentInfo color_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = frame_color_view(c),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {clear.r, clear.g, clear.b, clear.a}}},
    };
    const VkRenderingAttachmentInfo depth_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = c->depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {.depth = 1.0f, .stencil = 0}},
    };
    const VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = c->extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthAttachment = &depth_attachment,
    };
    vkCmdBeginRendering(cmd, &rendering);

    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)c->extent.width,
        .height = (float)c->extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor = {.extent = c->extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    c->in_frame = true;
}

void vkmin_bind(vkmin_ctx *c, const vkmin_bindings *bind) {
    VKMIN_ASSERT(c && bind, "vkmin_bind: null argument");
    VKMIN_ASSERT(c->in_frame, "vkmin_bind outside a frame");
    VkCommandBuffer cmd = c->cmd[c->slot];

    const pipe_slot *pipe = NULL;
    VKMIN_SLOT_LOOKUP(c->pipes, VKMIN_MAX_PIPES, bind->pipe.id, pipe);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipe);

    /* Always bind a texture: a pipeline that does not sample gets the 1x1
     * white image, so the descriptor set is bound on every path. */
    const uint32_t texture_id = bind->texture.id ? bind->texture.id : c->white.id;
    const image_slot *texture = NULL;
    VKMIN_SLOT_LOOKUP(c->images, VKMIN_MAX_IMAGES, texture_id, texture);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipe_layout, 0, 1,
                            &texture->set, 0, NULL);

    if (bind->vbuf.id) {
        const buffer_slot *vbuf = NULL;
        VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, bind->vbuf.id, vbuf);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf->buf, &offset);
    }
    /* vkmin_draw dispatches on this rather than taking a flag: an index buffer
     * being bound is the same fact stated twice otherwise. */
    c->index_bound = bind->ibuf.id != 0;
    if (c->index_bound) {
        const buffer_slot *ibuf = NULL;
        VKMIN_SLOT_LOOKUP(c->buffers, VKMIN_MAX_BUFFERS, bind->ibuf.id, ibuf);
        vkCmdBindIndexBuffer(cmd, ibuf->buf, 0, VK_INDEX_TYPE_UINT16);
    }
}

void vkmin_push(vkmin_ctx *c, const void *data, uint32_t bytes) {
    VKMIN_ASSERT(c && data, "vkmin_push: null argument");
    VKMIN_ASSERT(c->in_frame, "vkmin_push outside a frame");
    VKMIN_ASSERT(bytes > 0 && bytes <= VKMIN_MAX_PUSH, "vkmin_push: %u bytes exceeds %u", bytes,
                 (unsigned)VKMIN_MAX_PUSH);
    vkCmdPushConstants(c->cmd[c->slot], c->pipe_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, bytes, data);
}

void vkmin_draw(vkmin_ctx *c, uint32_t count, uint32_t instances) {
    VKMIN_ASSERT(c != NULL, "vkmin_draw: null context");
    VKMIN_ASSERT(c->in_frame, "vkmin_draw outside a frame");
    if (c->index_bound) {
        vkCmdDrawIndexed(c->cmd[c->slot], count, instances, 0, 0, 0);
    } else {
        vkCmdDraw(c->cmd[c->slot], count, instances, 0, 0);
    }
}

void vkmin_frame_end(vkmin_ctx *c) {
    VKMIN_ASSERT(c != NULL, "vkmin_frame_end: null context");
    VKMIN_ASSERT(c->in_frame, "vkmin_frame_end without a frame_begin");
    VkCommandBuffer cmd = c->cmd[c->slot];
    vkCmdEndRendering(cmd);

    /* Execute then inhibit: every frame copies its colour target into the
     * mapped readback buffer whether or not anyone will ask for a PNG. It
     * costs a full-target copy per frame and it buys three things -- one
     * execution path for headless and windowed alike, a readback that never
     * reasons about what happened last, and, decisively, no read of a
     * swapchain image after it has been handed to the presentation engine.
     * vkDeviceWaitIdle does not cover the presentation engine; doing the copy
     * here, while we still own the image, is what makes the capture safe.
     * (Synchronization validation reported WRITE_AFTER_PRESENT for the
     * previous, save-time-copy version of this code.) */
    image_barrier(cmd, frame_color_image(c), VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT);
    /* The readback buffer for this slot was written by the frame two ago and
     * read by the host in between. The fence wait in vkmin_frame_begin already
     * orders that on the CPU, but the dependency has to be stated to the
     * device as well or the overwrite is an unordered write-after-write. */
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
    const VkDependencyInfo device_dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &to_device,
    };
    vkCmdPipelineBarrier2(cmd, &device_dep);
    const VkBufferImageCopy readback = {
        .bufferRowLength = 0, /* tightly packed: pitch is exactly width * 4 bytes */
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {.width = c->extent.width, .height = c->extent.height, .depth = 1},
    };
    vkCmdCopyImageToBuffer(cmd, frame_color_image(c), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           c->readback_buf[c->slot], 1, &readback);
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
    const VkDependencyInfo host_dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &to_host,
    };
    vkCmdPipelineBarrier2(cmd, &host_dep);
    if (!c->desc.headless) {
        image_barrier(cmd, frame_color_image(c), VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
    }
    VK_CHECK(vkEndCommandBuffer(cmd));

    const VkCommandBufferSubmitInfo cmd_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    const VkSemaphoreSubmitInfo wait = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = c->acquired[c->slot],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signal = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = c->desc.headless ? VK_NULL_HANDLE : c->rendered[c->swap_index],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const uint32_t sem_count = c->desc.headless ? 0u : 1u;
    const VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = sem_count,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmd_info,
        .signalSemaphoreInfoCount = sem_count,
        .pSignalSemaphoreInfos = &signal,
    };
    VK_CHECK(vkQueueSubmit2(c->queue, 1, &submit, c->fence[c->slot]));
    c->fence_pending[c->slot] = true;
    c->last_slot = c->slot;
    c->have_submitted = true;

    if (!c->desc.headless) {
        const VkPresentInfoKHR present = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &c->rendered[c->swap_index],
            .swapchainCount = 1,
            .pSwapchains = &c->swapchain,
            .pImageIndices = &c->swap_index,
        };
        const VkResult r = vkQueuePresentKHR(c->queue, &present);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            c->need_recreate = true;
        } else {
            VK_CHECK(r);
        }
        int w = 0, h = 0;
        plat_framebuffer_size(&w, &h);
        if (w > 0 && h > 0 &&
            ((uint32_t)w != c->extent.width || (uint32_t)h != c->extent.height)) {
            c->need_recreate = true;
        }
    }

    if (c->desc.sync_naive) VK_CHECK(vkDeviceWaitIdle(c->dev));

    c->slot = (c->slot + 1u) % c->frames_in_flight;
    c->in_frame = false;
}

/* -------------------------------------------------------------- readback -- */

static bool format_is_bgra(VkFormat f) {
    return f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_B8G8R8A8_SRGB;
}

bool vkmin_save_png(vkmin_ctx *c, const char *path) {
    VKMIN_ASSERT(c && path, "vkmin_save_png: null argument");
    VKMIN_ASSERT(!c->in_frame, "vkmin_save_png inside a frame; call it after vkmin_frame_end");
    if (!c->have_submitted) {
        fprintf(stderr, "vkmin: nothing rendered yet, not writing '%s'\n", path);
        return false;
    }
    /* The copy already happened inside the last frame's command buffer, so all
     * that is left is to wait for that submission and read the mapping. */
    VK_CHECK(vkWaitForFences(c->dev, 1, &c->fence[c->last_slot], VK_TRUE, UINT64_MAX));
    c->fence_pending[c->last_slot] = false;

    const uint32_t w = c->extent.width, h = c->extent.height;
    const size_t bytes = (size_t)c->readback_size;
    /* bufferRowLength 0 in the copy region means "tightly packed at the image
     * width", so the pitch is exactly this. The writer is told explicitly
     * rather than left to assume: a wrong pitch here is the classic silent
     * shear that produces an image which looks almost right. */
    const int row_pitch = (int)(w * 4u);

    unsigned char *rgba = malloc(bytes);
    VKMIN_ASSERT(rgba != NULL, "out of memory writing '%s'", path);
    memcpy(rgba, c->readback_mapped[c->last_slot], bytes);

    /* Swapchain images are usually BGRA. Swizzling on the CPU here is cheaper
     * in code than a blit pipeline, and this path is never in a frame budget. */
    if (format_is_bgra(c->color_format)) {
        for (size_t i = 0; i < bytes; i += 4) {
            const unsigned char b = rgba[i];
            rgba[i] = rgba[i + 2];
            rgba[i + 2] = b;
        }
    }

    const bool ok = vkmin_png_write(path, (int)w, (int)h, rgba, row_pitch);
    free(rgba);
    if (!ok) fprintf(stderr, "vkmin: failed to write '%s'\n", path);
    return ok;
}

/* --------------------------------------------------------------- window --- */

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
