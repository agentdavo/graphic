/* imgdiff -- golden-image comparison with a per-channel tolerance. Writes a
 * diff image beside the failure so a mismatch can be looked at, not guessed at.
 * usage: imgdiff <a.png> <b.png> <tolerance> [diff.png] */
#include <stdio.h>
#include <stdlib.h>

#include "stb_bridge.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: imgdiff <a.png> <b.png> <tolerance> [diff.png]\n");
        return 2;
    }
    const int tolerance = atoi(argv[3]);
    int aw = 0, ah = 0, bw = 0, bh = 0;
    unsigned char *a = vkmin_png_load(argv[1], &aw, &ah);
    unsigned char *b = vkmin_png_load(argv[2], &bw, &bh);
    if (!a || !b) {
        fprintf(stderr, "imgdiff: cannot load %s\n", a ? argv[2] : argv[1]);
        return 1;
    }
    if (aw != bw || ah != bh) {
        fprintf(stderr, "imgdiff: size mismatch %dx%d vs %dx%d\n", aw, ah, bw, bh);
        return 1;
    }
    const size_t n = (size_t)aw * (size_t)ah * 4u;
    unsigned char *diff = argc > 4 ? malloc(n) : NULL;
    long bad_pixels = 0;
    int worst = 0;
    for (size_t i = 0; i < n; i += 4) {
        int pixel_worst = 0;
        for (int k = 0; k < 4; ++k) {
            const int d = (int)a[i + k] - (int)b[i + k];
            const int ad = d < 0 ? -d : d;
            if (ad > pixel_worst) pixel_worst = ad;
        }
        if (pixel_worst > worst) worst = pixel_worst;
        const bool bad = pixel_worst > tolerance;
        if (bad) ++bad_pixels;
        if (diff) {
            diff[i + 0] = bad ? 255u : (unsigned char)(a[i] / 3u);
            diff[i + 1] = bad ? 0u : (unsigned char)(a[i + 1] / 3u);
            diff[i + 2] = bad ? 255u : (unsigned char)(a[i + 2] / 3u);
            diff[i + 3] = 255u;
        }
    }
    int status = 0;
    if (bad_pixels > 0) {
        fprintf(stderr, "imgdiff: %s vs %s: %ld pixels differ by more than %d (worst %d)\n",
                argv[1], argv[2], bad_pixels, tolerance, worst);
        if (diff && vkmin_png_write(argv[4], aw, ah, diff, aw * 4)) {
            fprintf(stderr, "imgdiff: wrote %s\n", argv[4]);
        }
        status = 1;
    }
    free(diff);
    vkmin_png_free(a);
    vkmin_png_free(b);
    return status;
}
