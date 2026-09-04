/* stb_bridge.c -- third-party image I/O, quarantined. Compiled with relaxed
 * warnings; nothing else in the build includes stb. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include "stb_bridge.h"

unsigned char *vkmin_png_load(const char *path, int *w, int *h) {
    int channels = 0;
    return stbi_load(path, w, h, &channels, 4);
}

void vkmin_png_free(void *pixels) { stbi_image_free(pixels); }

bool vkmin_png_write(const char *path, int w, int h, const void *rgba, int row_pitch) {
    return stbi_write_png(path, w, h, 4, rgba, row_pitch) != 0;
}
