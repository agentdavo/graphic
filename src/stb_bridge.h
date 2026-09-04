/* stb_bridge.h -- the only three things vkmin wants from stb, declared so that
 * third-party code compiles in its own translation unit with its own warning
 * settings and never appears in vkmin.c's include graph. */
#ifndef VKMIN_STB_BRIDGE_H
#define VKMIN_STB_BRIDGE_H

#include <stdbool.h>

unsigned char *vkmin_png_load(const char *path, int *w, int *h); /* always RGBA8 */
void vkmin_png_free(void *pixels);
bool vkmin_png_write(const char *path, int w, int h, const void *rgba, int row_pitch);

#endif
