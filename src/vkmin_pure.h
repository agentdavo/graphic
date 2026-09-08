/* The decisions and tables vkmin can make before it has a device.
 *
 * Everything here is a pure function of its arguments: no context, no Vulkan
 * call, no state. That is why it is in a header rather than buried as static
 * code in vkmin.c -- a test can include it and drive it directly, which is
 * section 5's "split out the finicky bit and test it" applied to decisions
 * and arithmetic whose errors otherwise surface far from their source.
 *
 * The path and format decisions include:
 *
 *  - choose_path decides the 1.4-or-1.3 question described at vkmin_path in
 *    vkmin.h. Getting it wrong does not crash; it silently runs the wrong one
 *    of two paths that are supposed to be interchangeable, and the only
 *    symptom is that a device which could have used host image copy quietly
 *    does not, or a --path=modern that should have been refused is not.
 *
 *  - format_lookup and mip_bytes size every upload. A wrong block size or a
 *    missed block-dimension rounding does not fault: it copies the wrong
 *    number of bytes into an image, and the picture is merely wrong. The
 *    compressed formats are the trap, because block_dim is 4 there and 1
 *    everywhere else, so an arithmetic slip only shows on BC textures.
 *
 * Included by vkmin.c after path_caps is declared. A caller wanting it alone
 * needs vulkan_core.h, vkmin.h and a path_caps in scope first.
 */
#ifndef VKMIN_PURE_H
#define VKMIN_PURE_H

/* A Vulkan buffer covers the entire arena, so only vkmin can enforce the
 * smaller logical resource/slot bounds. Subtract after bounding the offset;
 * offset+bytes can wrap and accidentally pass an addition-based test. */
static bool vkm_range_fits(uint64_t capacity, uint64_t offset, uint64_t bytes) {
    return offset <= capacity && bytes <= capacity-offset;
}

/* Target preference negotiation; independent of what the current GPU supports. */
static uint32_t vkm_choose_samples(uint32_t supported, uint32_t requested) {
    if (!requested || requested > 64 || (requested & (requested-1))) return 0;
    for (uint32_t samples=requested;samples;samples>>=1) if (supported & samples) return samples;
    return 0;
}

/* Decimal CLI input without atoi's silent zero, truncation or overflow.
 * end names the first unconsumed character; callers decide which delimiters fit. */
typedef struct { uint32_t value; const char *end; bool valid; } vkm_decimal;
static vkm_decimal vkm_parse_decimal(const char *text, uint32_t limit) {
    vkm_decimal result = {.end = text};
    if (!text || *text < '0' || *text > '9') return result;
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') {
        const uint32_t digit = (uint32_t)(*text-'0');
        if (value > limit/10 || (value == limit/10 && digit > limit%10)) return result;
        value = value*10+digit; ++text;
    }
    return (vkm_decimal){value, text, true};
}

/* --------------------------------------------------------------- path -- */

/* MODERN needs both features; asking for it without them is a hard error
 * rather than a silent downgrade, because a caller that says --path=modern is
 * usually trying to reproduce a specific journal and would rather be told. */
static vkmin_path choose_path(path_caps k, vkmin_path want, const char **reason) { // pure
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

/* ------------------------------------------------------------ formats -- */

typedef struct {
    VkFormat vk;
    uint32_t block_bytes;
    uint32_t block_dim; /* 1 for uncompressed, 4 for BC */
    VkImageAspectFlags aspect;
} format_info;

static format_info format_lookup(vkmin_format f) { // pure
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
    case VKMIN_FMT_R32_UINT: return (format_info){VK_FORMAT_R32_UINT, 4, 1, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_RG16_UNORM: return (format_info){VK_FORMAT_R16G16_UNORM, 4, 1, VK_IMAGE_ASPECT_COLOR_BIT};
    case VKMIN_FMT_NONE:
    case VKMIN_FMT_COUNT: break;
    }
    VKMIN_FAIL("bad vkmin_format %d", (int)f);
}

/* Block count rounds up, so a 1x1 BC image still costs one whole 4x4 block.
 * Rounding down here would under-size an upload and read past the source. */
static size_t mip_bytes(format_info fi, uint32_t w, uint32_t h) { // pure
    const uint32_t bw = (w + fi.block_dim - 1) / fi.block_dim;
    const uint32_t bh = (h + fi.block_dim - 1) / fi.block_dim;
    return (size_t)bw * bh * fi.block_bytes;
}

#endif
