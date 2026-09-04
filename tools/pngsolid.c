/* pngsolid -- assert every pixel of a PNG is exactly one RGBA value. This is
 * milestone 1's check: it proves the whole readback chain before there is any
 * rendering to confuse it. usage: pngsolid <file.png> <r> <g> <b> <a> */
#include <stdio.h>
#include <stdlib.h>

#include "stb_bridge.h"

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: pngsolid <file.png> <r> <g> <b> <a>\n");
        return 2;
    }
    unsigned char want[4];
    for (int k = 0; k < 4; ++k) want[k] = (unsigned char)atoi(argv[2 + k]);
    int w = 0, h = 0;
    unsigned char *px = vkmin_png_load(argv[1], &w, &h);
    if (!px) {
        fprintf(stderr, "pngsolid: cannot load %s\n", argv[1]);
        return 1;
    }
    const size_t n = (size_t)w * (size_t)h * 4u;
    for (size_t i = 0; i < n; i += 4) {
        for (int k = 0; k < 4; ++k) {
            if (px[i + k] != want[k]) {
                fprintf(stderr,
                        "pngsolid: %s pixel %zu is (%u,%u,%u,%u), expected (%u,%u,%u,%u)\n",
                        argv[1], i / 4u, px[i], px[i + 1], px[i + 2], px[i + 3], want[0],
                        want[1], want[2], want[3]);
                vkmin_png_free(px);
                return 1;
            }
        }
    }
    vkmin_png_free(px);
    printf("pngsolid: %s is %dx%d, uniformly (%u,%u,%u,%u)\n", argv[1], w, h, want[0], want[1],
           want[2], want[3]);
    return 0;
}
