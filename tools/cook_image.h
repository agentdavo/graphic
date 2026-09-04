/* cook_image.h -- image decoding, mip generation and BCn encoding for cook.c.
 * Lives in its own translation unit because it includes stb_image, and stb
 * is compiled with the third-party warning set, never ours. */
#ifndef COOK_IMAGE_H
#define COOK_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *px; /* RGBA8, tightly packed */
} cook_rgba;

typedef enum { COOK_BC1_SRGB, COOK_BC1_UNORM, COOK_BC3_SRGB, COOK_BC4, COOK_BC5 } cook_bc_format;

cook_rgba cook_image_load(const char *path);
cook_rgba cook_image_decode(const uint8_t *bytes, size_t size);
/* Downsamples to at most max_size, builds the full mip chain, compresses every
 * level and writes a KTX2 file. Aborts on any failure. */
void cook_image_write_ktx2(const cook_rgba *src, cook_bc_format fmt, int max_size, const char *out_path);

#endif
