/* ktx2.c -- see ktx2.h. */
#include "ktx2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static void fail(const char *path, const char *why) {
    fprintf(stderr, "ktx2: %s: %s\n", path, why);
    abort();
}

vkmin_image ktx2_load(vkmin_ctx *c, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) fail(path, "cannot open");
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 80) fail(path, "too small to be KTX2");
    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) fail(path, "read failed");
    fclose(f);

    static const uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(data, identifier, 12) != 0) fail(path, "not a KTX2 file");
    const uint32_t vk_format = rd32(data + 12);
    const uint32_t width = rd32(data + 20), height = rd32(data + 24);
    const uint32_t depth = rd32(data + 28), layers = rd32(data + 32), faces = rd32(data + 36);
    const uint32_t levels = rd32(data + 40), supercompression = rd32(data + 44);
    if (depth > 1 || layers > 1 || faces != 1 || supercompression != 0) fail(path, "unsupported KTX2 layout");
    if (levels == 0 || levels > 16) fail(path, "bad level count");

    vkmin_format fmt;
    switch (vk_format) {
    case 131: fmt = VKMIN_FMT_BC1_UNORM; break;
    case 132: fmt = VKMIN_FMT_BC1_SRGB; break;
    case 138: fmt = VKMIN_FMT_BC3_SRGB; break;
    case 139: fmt = VKMIN_FMT_BC4_UNORM; break;
    case 141: fmt = VKMIN_FMT_BC5_UNORM; break;
    default: fail(path, "VkFormat is not one the cooker writes"); fmt = VKMIN_FMT_NONE;
    }
    const vkmin_image img = vkmin_make_image(
        c, &(vkmin_image_desc){.width = (int)width, .height = (int)height, .mip_levels = (int)levels,
                               .format = fmt, .usage = VKMIN_IMAGE_SAMPLED, .label = path});
    for (uint32_t l = 0; l < levels; ++l) {
        const uint8_t *entry = data + 80 + 24 * l;
        const uint64_t offset = rd64(entry), bytes = rd64(entry + 8);
        if (offset + bytes > (uint64_t)size) fail(path, "level data runs past the end of the file");
        vkmin_image_upload(c, img, (int)l, data + offset, (size_t)bytes);
    }
    free(data);
    return img;
}
