/* cook_image.c -- see cook_image.h. Compiled with the third-party flags. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include "cook_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fatal(const char *what) {
    fprintf(stderr, "cook_image: %s\n", what);
    exit(1);
}

cook_rgba cook_image_load(const char *path) {
    cook_rgba out = {0};
    int ch = 0;
    out.px = stbi_load(path, &out.w, &out.h, &ch, 4);
    return out;
}

cook_rgba cook_image_decode(const uint8_t *bytes, size_t size) {
    cook_rgba out = {0};
    int ch = 0;
    out.px = stbi_load_from_memory(bytes, (int)size, &out.w, &out.h, &ch, 4);
    return out;
}

/* ------------------------------------------------------------ resampling -- */

static float srgb_to_linear(uint8_t c) {
    const float f = c / 255.0f;
    return f <= 0.04045f ? f / 12.92f : powf((f + 0.055f) / 1.055f, 2.4f);
}

static uint8_t linear_to_srgb(float f) {
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    const float s = f <= 0.0031308f ? f * 12.92f : 1.055f * powf(f, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)lroundf(s * 255.0f);
}

/* 2x box filter. Colour channels of sRGB images are averaged in linear light;
 * alpha and data channels are averaged as stored. Odd sizes clamp the last
 * row/column, which is what every offline mipper does. */
static cook_rgba downsample(const cook_rgba *src, int srgb, int normal_map) {
    cook_rgba out = {.w = src->w > 1 ? src->w / 2 : 1, .h = src->h > 1 ? src->h / 2 : 1};
    out.px = malloc((size_t)out.w * out.h * 4);
    if (!out.px) fatal("out of memory");
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = x * 2 + dx < src->w ? x * 2 + dx : src->w - 1;
                    const int sy = y * 2 + dy < src->h ? y * 2 + dy : src->h - 1;
                    const uint8_t *p = src->px + ((size_t)sy * src->w + sx) * 4;
                    for (int k = 0; k < 4; ++k) {
                        acc[k] += (srgb && k < 3) ? srgb_to_linear(p[k]) : p[k] / 255.0f;
                    }
                }
            }
            uint8_t *o = out.px + ((size_t)y * out.w + x) * 4;
            if (normal_map) {
                float n[3] = {acc[0] * 0.5f - 1.0f, acc[1] * 0.5f - 1.0f, acc[2] * 0.5f - 1.0f};
                const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                for (int k = 0; k < 3; ++k) acc[k] = len > 0 ? (n[k] / len * 0.5f + 0.5f) * 4.0f : acc[k];
            }
            for (int k = 0; k < 4; ++k) {
                o[k] = (srgb && k < 3) ? linear_to_srgb(acc[k] * 0.25f) : (uint8_t)lroundf(acc[k] * 0.25f * 255.0f);
            }
        }
    }
    return out;
}

/* ------------------------------------------------------------ BC encoders - */

typedef struct { float r, g, b; } col3;

static uint16_t to565(col3 c) {
    const int r = (int)lroundf(fminf(fmaxf(c.r, 0.f), 1.f) * 31.0f);
    const int g = (int)lroundf(fminf(fmaxf(c.g, 0.f), 1.f) * 63.0f);
    const int b = (int)lroundf(fminf(fmaxf(c.b, 0.f), 1.f) * 31.0f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static col3 from565(uint16_t c) {
    return (col3){((c >> 11) & 31) / 31.0f, ((c >> 5) & 63) / 63.0f, (c & 31) / 31.0f};
}

static float dist2(col3 a, col3 b) {
    const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

/* BC1: principal-axis endpoints, nearest-palette indices, one least-squares
 * refinement of the endpoints, requantise, reassign. Classic and adequate. */
static void encode_bc1(const col3 px[16], uint8_t out[8]) {
    col3 mean = {0, 0, 0};
    for (int i = 0; i < 16; ++i) { mean.r += px[i].r; mean.g += px[i].g; mean.b += px[i].b; }
    mean.r /= 16; mean.g /= 16; mean.b /= 16;
    /* covariance, then a few power iterations for the principal axis */
    float cov[6] = {0};
    for (int i = 0; i < 16; ++i) {
        const float r = px[i].r - mean.r, g = px[i].g - mean.g, b = px[i].b - mean.b;
        cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
    }
    col3 axis = {1, 1, 1};
    for (int it = 0; it < 8; ++it) {
        const col3 n = {cov[0] * axis.r + cov[1] * axis.g + cov[2] * axis.b,
                        cov[1] * axis.r + cov[3] * axis.g + cov[4] * axis.b,
                        cov[2] * axis.r + cov[4] * axis.g + cov[5] * axis.b};
        const float len = sqrtf(n.r * n.r + n.g * n.g + n.b * n.b);
        if (len < 1e-12f) break;
        axis = (col3){n.r / len, n.g / len, n.b / len};
    }
    float pmin = 1e30f, pmax = -1e30f;
    col3 e0 = px[0], e1 = px[0];
    for (int i = 0; i < 16; ++i) {
        const float p = (px[i].r - mean.r) * axis.r + (px[i].g - mean.g) * axis.g + (px[i].b - mean.b) * axis.b;
        if (p < pmin) { pmin = p; e0 = px[i]; }
        if (p > pmax) { pmax = p; e1 = px[i]; }
    }
    uint16_t c0 = to565(e0), c1 = to565(e1);
    uint8_t idx[16];
    for (int pass = 0; pass < 2; ++pass) {
        if (c0 == c1) { memset(idx, 0, sizeof idx); break; }
        if (c0 < c1) { const uint16_t t = c0; c0 = c1; c1 = t; }
        const col3 p0 = from565(c0), p1 = from565(c1);
        const col3 pal[4] = {p0, p1,
                             {(2 * p0.r + p1.r) / 3, (2 * p0.g + p1.g) / 3, (2 * p0.b + p1.b) / 3},
                             {(p0.r + 2 * p1.r) / 3, (p0.g + 2 * p1.g) / 3, (p0.b + 2 * p1.b) / 3}};
        for (int i = 0; i < 16; ++i) {
            float best = 1e30f;
            for (int k = 0; k < 4; ++k) {
                const float d = dist2(px[i], pal[k]);
                if (d < best) { best = d; idx[i] = (uint8_t)k; }
            }
        }
        if (pass == 1) break;
        /* least squares: palette weight of endpoint 0 per index is 1, 0, 2/3, 1/3 */
        const float w0[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
        float a = 0, b = 0, d = 0;
        col3 s0 = {0, 0, 0}, s1 = {0, 0, 0};
        for (int i = 0; i < 16; ++i) {
            const float w = w0[idx[i]], v = 1.0f - w;
            a += w * w; b += w * v; d += v * v;
            s0.r += w * px[i].r; s0.g += w * px[i].g; s0.b += w * px[i].b;
            s1.r += v * px[i].r; s1.g += v * px[i].g; s1.b += v * px[i].b;
        }
        const float det = a * d - b * b;
        if (fabsf(det) < 1e-8f) break;
        const float ia = d / det, ib = -b / det, id = a / det;
        e0 = (col3){ia * s0.r + ib * s1.r, ia * s0.g + ib * s1.g, ia * s0.b + ib * s1.b};
        e1 = (col3){ib * s0.r + id * s1.r, ib * s0.g + id * s1.g, ib * s0.b + id * s1.b};
        c0 = to565(e0); c1 = to565(e1);
    }
    if (c0 == c1) memset(idx, 0, sizeof idx);
    out[0] = (uint8_t)(c0 & 0xff); out[1] = (uint8_t)(c0 >> 8);
    out[2] = (uint8_t)(c1 & 0xff); out[3] = (uint8_t)(c1 >> 8);
    for (int row = 0; row < 4; ++row) {
        out[4 + row] = (uint8_t)(idx[row * 4] | (idx[row * 4 + 1] << 2) | (idx[row * 4 + 2] << 4) | (idx[row * 4 + 3] << 6));
    }
}

/* BC4 (also the alpha block of BC3 and each half of BC5): 8-value mode with
 * endpoints a0 > a1, 3-bit indices. */
static void encode_bc4(const uint8_t v[16], uint8_t out[8]) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
    if (hi == lo) { hi = lo < 255 ? lo + 1 : lo; lo = hi > 0 ? hi - 1 : lo; }
    /* index k decodes to a0 + (a1 - a0) * k / 7 for the ramp 0,7,1,2,3,4,5,6 ordering */
    const int a0 = hi, a1 = lo;
    uint8_t idx[16];
    for (int i = 0; i < 16; ++i) {
        const float t = (float)(v[i] - a1) / (float)(a0 - a1);
        int k = (int)lroundf(t * 7.0f);
        k = k < 0 ? 0 : k > 7 ? 7 : k;
        /* BC4 8-value mode: index 0 = a0, 1 = a1, 2..7 = interpolated from a0 to a1 */
        static const uint8_t ramp_to_index[8] = {1, 7, 6, 5, 4, 3, 2, 0};
        idx[i] = ramp_to_index[k];
    }
    out[0] = (uint8_t)a0; out[1] = (uint8_t)a1;
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= (uint64_t)idx[i] << (3 * i);
    for (int i = 0; i < 6; ++i) out[2 + i] = (uint8_t)(bits >> (8 * i));
}

static size_t encode_level(const cook_rgba *img, cook_bc_format fmt, uint8_t **out) {
    const int bw = (img->w + 3) / 4, bh = (img->h + 3) / 4;
    const size_t block_bytes = (fmt == COOK_BC3_SRGB || fmt == COOK_BC5) ? 16 : 8;
    const size_t total = (size_t)bw * bh * block_bytes;
    uint8_t *dst = malloc(total);
    if (!dst) fatal("out of memory");
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            col3 px[16];
            uint8_t a[16], r[16], g[16];
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3) < img->w ? bx * 4 + (i & 3) : img->w - 1;
                const int y = by * 4 + (i >> 2) < img->h ? by * 4 + (i >> 2) : img->h - 1;
                const uint8_t *p = img->px + ((size_t)y * img->w + x) * 4;
                px[i] = (col3){p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f};
                r[i] = p[0]; g[i] = p[1]; a[i] = p[3];
            }
            uint8_t *block = dst + ((size_t)by * bw + bx) * block_bytes;
            switch (fmt) {
            case COOK_BC1_SRGB:
            case COOK_BC1_UNORM: encode_bc1(px, block); break;
            case COOK_BC3_SRGB: encode_bc4(a, block); encode_bc1(px, block + 8); break;
            case COOK_BC4: encode_bc4(r, block); break;
            case COOK_BC5: encode_bc4(r, block); encode_bc4(g, block + 8); break;
            }
        }
    }
    *out = dst;
    return total;
}

/* ---------------------------------------------------------------- KTX2 ---- */

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put64(FILE *f, uint64_t v) { fwrite(&v, 8, 1, f); }

void cook_image_write_ktx2(const cook_rgba *src, cook_bc_format fmt, int max_size, const char *out_path) {
    const int srgb = fmt == COOK_BC1_SRGB || fmt == COOK_BC3_SRGB;
    const int normal_map = fmt == COOK_BC5;
    cook_rgba cur = {.w = src->w, .h = src->h, .px = malloc((size_t)src->w * src->h * 4)};
    if (!cur.px) fatal("out of memory");
    memcpy(cur.px, src->px, (size_t)src->w * src->h * 4);
    while (cur.w > max_size || cur.h > max_size) {
        cook_rgba next = downsample(&cur, srgb, normal_map);
        free(cur.px);
        cur = next;
    }
    /* mip chain down to 4x4: below that a BC block is padding anyway */
    uint8_t *levels[16];
    size_t level_bytes[16];
    int level_w[16], level_h[16], level_count = 0;
    for (;;) {
        level_bytes[level_count] = encode_level(&cur, fmt, &levels[level_count]);
        level_w[level_count] = cur.w;
        level_h[level_count] = cur.h;
        ++level_count;
        if ((cur.w <= 4 && cur.h <= 4) || level_count == 16) break;
        cook_rgba next = downsample(&cur, srgb, normal_map);
        free(cur.px);
        cur = next;
    }
    free(cur.px);

    /* VkFormat values, from vulkan_core.h: BC1_RGB_UNORM 131, BC1_RGB_SRGB 132,
     * BC3_UNORM 137, BC3_SRGB 138, BC4_UNORM 139, BC5_UNORM 141. */
    const uint32_t vk_format = fmt == COOK_BC1_SRGB ? 132 : fmt == COOK_BC1_UNORM ? 131
                               : fmt == COOK_BC3_SRGB ? 138 : fmt == COOK_BC4 ? 139 : 141;
    const uint32_t samples = (fmt == COOK_BC3_SRGB || fmt == COOK_BC5) ? 2 : 1;
    const uint32_t dfd_block = 24 + 16 * samples;
    const uint32_t dfd_bytes = 4 + dfd_block;
    const uint64_t header_bytes = 80 + 24 * (uint64_t)level_count;
    const uint64_t dfd_offset = header_bytes;
    uint64_t data_offset = (dfd_offset + dfd_bytes + 15) & ~15ull;

    FILE *f = fopen(out_path, "wb");
    if (!f) fatal("cannot open ktx2 for writing");
    static const uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(identifier, 1, 12, f);
    put32(f, vk_format);
    put32(f, 1);              /* typeSize */
    put32(f, (uint32_t)level_w[0]);
    put32(f, (uint32_t)level_h[0]);
    put32(f, 0);              /* pixelDepth */
    put32(f, 0);              /* layerCount */
    put32(f, 1);              /* faceCount */
    put32(f, (uint32_t)level_count);
    put32(f, 0);              /* supercompressionScheme */
    put32(f, (uint32_t)dfd_offset); put32(f, dfd_bytes);
    put32(f, 0); put32(f, 0); /* no key/value data */
    put64(f, 0); put64(f, 0); /* no supercompression global data */
    /* Level index. Data is stored smallest level first, as the spec asks. */
    uint64_t offsets[16];
    uint64_t cursor = data_offset;
    for (int l = level_count - 1; l >= 0; --l) {
        cursor = (cursor + 15) & ~15ull;
        offsets[l] = cursor;
        cursor += level_bytes[l];
    }
    for (int l = 0; l < level_count; ++l) {
        put64(f, offsets[l]); put64(f, level_bytes[l]); put64(f, level_bytes[l]);
    }
    /* A nominal basic data format descriptor: enough structure to be well
     * formed. Our own reader takes the format from the header; other tools
     * may want a fuller DFD than this. */
    put32(f, dfd_bytes);
    put32(f, 0);              /* vendor 0 (Khronos), descriptor type 0 (basic) */
    put32(f, (2u << 16) | dfd_block); /* version 2, block size */
    const uint8_t color_model = fmt == COOK_BC1_SRGB || fmt == COOK_BC1_UNORM ? 128 : fmt == COOK_BC3_SRGB ? 130 : fmt == COOK_BC4 ? 131 : 132;
    const uint8_t transfer = srgb ? 2 : 1;
    fputc(color_model, f); fputc(1, f); fputc(transfer, f); fputc(0, f); /* model, primaries, transfer, flags */
    fputc(3, f); fputc(3, f); fputc(0, f); fputc(0, f);                  /* texel block 4x4 (stored minus one) */
    fputc((int)((samples == 2 ? 16 : 8)), f); for (int i = 0; i < 7; ++i) fputc(0, f); /* bytes per plane */
    for (uint32_t s = 0; s < samples; ++s) {
        put32(f, (63u << 0) | (0u << 16) | (0u << 24)); /* bit offset 0, length 64, channel 0 */
        put32(f, 0); put32(f, 0); put32(f, 0xffffffffu);   /* sample position, lower, upper */
    }
    for (uint64_t pos = dfd_offset + dfd_bytes; pos < data_offset; ++pos) fputc(0, f);
    for (int l = level_count - 1; l >= 0; --l) {
        while ((uint64_t)ftell(f) < offsets[l]) fputc(0, f);
        fwrite(levels[l], 1, level_bytes[l], f);
        free(levels[l]);
    }
    fclose(f);
}
