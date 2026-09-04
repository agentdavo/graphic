/* mktex -- writes the test texture: a 4x4 numbered grid, not a checkerboard.
 * A flipped V coordinate or a wrong row pitch has to be obvious at a glance
 * rather than merely plausible, so every cell is labelled and the top-left
 * cell carries a corner marker. Run once; the PNG is committed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_bridge.h"

enum { CELLS = 4, CELL = 32, SIZE = CELLS * CELL, GLYPH_W = 3, GLYPH_H = 5, SCALE = 5 };

/* 3x5 glyphs for 1..9 then A..G: sixteen labels for sixteen cells. */
static const unsigned char glyphs[16][GLYPH_H] = {
    {0x2, 0x6, 0x2, 0x2, 0x7}, /* 1 */
    {0x7, 0x1, 0x7, 0x4, 0x7}, /* 2 */
    {0x7, 0x1, 0x7, 0x1, 0x7}, /* 3 */
    {0x5, 0x5, 0x7, 0x1, 0x1}, /* 4 */
    {0x7, 0x4, 0x7, 0x1, 0x7}, /* 5 */
    {0x7, 0x4, 0x7, 0x5, 0x7}, /* 6 */
    {0x7, 0x1, 0x2, 0x2, 0x2}, /* 7 */
    {0x7, 0x5, 0x7, 0x5, 0x7}, /* 8 */
    {0x7, 0x5, 0x7, 0x1, 0x7}, /* 9 */
    {0x7, 0x5, 0x7, 0x5, 0x5}, /* A */
    {0x6, 0x5, 0x6, 0x5, 0x6}, /* B */
    {0x7, 0x4, 0x4, 0x4, 0x7}, /* C */
    {0x6, 0x5, 0x5, 0x5, 0x6}, /* D */
    {0x7, 0x4, 0x7, 0x4, 0x7}, /* E */
    {0x7, 0x4, 0x7, 0x4, 0x4}, /* F */
    {0x7, 0x4, 0x5, 0x5, 0x7}, /* G */
};

static void put(unsigned char *px, int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return;
    unsigned char *p = px + ((size_t)y * SIZE + (size_t)x) * 4;
    p[0] = (unsigned char)r;
    p[1] = (unsigned char)g;
    p[2] = (unsigned char)b;
    p[3] = 255;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "tests/assets/grid.png";
    unsigned char *px = calloc((size_t)SIZE * SIZE * 4, 1);
    if (!px) return 1;

    for (int cy = 0; cy < CELLS; ++cy) {
        for (int cx = 0; cx < CELLS; ++cx) {
            const int index = cy * CELLS + cx;
            /* Brightness rises left-to-right and hue shifts top-to-bottom, so
             * even a blurred thumbnail shows which way is up. */
            const int base = 40 + cx * 30;
            const int r = base + (cy == 0 ? 110 : 0);
            const int g = base + (cy == 1 ? 110 : 0) + (cy == 3 ? 60 : 0);
            const int b = base + (cy == 2 ? 110 : 0) + (cy == 3 ? 60 : 0);
            for (int y = 0; y < CELL; ++y) {
                for (int x = 0; x < CELL; ++x) {
                    const bool edge = x == 0 || y == 0;
                    put(px, cx * CELL + x, cy * CELL + y, edge ? 20 : r, edge ? 20 : g,
                        edge ? 20 : b);
                }
            }
            const int ox = cx * CELL + (CELL - GLYPH_W * SCALE) / 2;
            const int oy = cy * CELL + (CELL - GLYPH_H * SCALE) / 2;
            for (int gy = 0; gy < GLYPH_H; ++gy) {
                for (int gx = 0; gx < GLYPH_W; ++gx) {
                    if (!(glyphs[index][gy] & (1u << (GLYPH_W - 1 - gx)))) continue;
                    for (int sy = 0; sy < SCALE; ++sy) {
                        for (int sx = 0; sx < SCALE; ++sx) {
                            put(px, ox + gx * SCALE + sx, oy + gy * SCALE + sy, 255, 255, 255);
                        }
                    }
                }
            }
        }
    }
    /* Corner marker: a solid red L in the image's top-left, i.e. at uv (0,0). */
    for (int i = 0; i < 12; ++i) {
        for (int t = 0; t < 3; ++t) {
            put(px, i, t, 255, 0, 0);
            put(px, t, i, 255, 0, 0);
        }
    }

    const bool ok = vkmin_png_write(out, SIZE, SIZE, px, SIZE * 4);
    free(px);
    if (!ok) {
        fprintf(stderr, "mktex: could not write %s\n", out);
        return 1;
    }
    printf("wrote %s (%dx%d)\n", out, SIZE, SIZE);
    return 0;
}
