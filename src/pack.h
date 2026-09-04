/* pack.h -- vertex attribute packing, the mirror of the decode helpers in
 * shaders/common.glsl. Used by the cooker and by the demo's generated meshes. */
#ifndef VKMIN_PACK_H
#define VKMIN_PACK_H

#include <math.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t pack_snorm16x2(float x, float y) {
    const int32_t ix = (int32_t)lroundf(fmaxf(-1.0f, fminf(1.0f, x)) * 32767.0f);
    const int32_t iy = (int32_t)lroundf(fmaxf(-1.0f, fminf(1.0f, y)) * 32767.0f);
    return ((uint32_t)(uint16_t)iy << 16) | (uint32_t)(uint16_t)ix;
}

/* Octahedral encoding of a unit vector into two snorm16. */
static inline uint32_t oct_encode(float x, float y, float z) {
    const float l1 = fabsf(x) + fabsf(y) + fabsf(z);
    if (l1 == 0.0f) return pack_snorm16x2(0.0f, 0.0f);
    float ex = x / l1, ey = y / l1;
    if (z < 0.0f) {
        const float ox = (1.0f - fabsf(ey)) * (ex >= 0.0f ? 1.0f : -1.0f);
        const float oy = (1.0f - fabsf(ex)) * (ey >= 0.0f ? 1.0f : -1.0f);
        ex = ox;
        ey = oy;
    }
    return pack_snorm16x2(ex, ey);
}

/* Tangent: octahedral xyz with the bitangent sign in the low bit. */
static inline uint32_t pack_tangent(float x, float y, float z, float w) {
    return (oct_encode(x, y, z) & ~1u) | (w < 0.0f ? 1u : 0u);
}

static inline uint16_t float_to_half(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    const int32_t exp = (int32_t)((x >> 23) & 0xffu) - 127 + 15;
    const uint32_t mant = x & 0x7fffffu;
    if (exp <= 0) return (uint16_t)sign; /* flush tiny to zero: UVs never need denormals */
    if (exp >= 31) return (uint16_t)(sign | 0x7c00u);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
}

static inline uint32_t pack_half2(float u, float v) {
    return ((uint32_t)float_to_half(v) << 16) | float_to_half(u);
}

#endif
