#include "vkmin.h"
#include "shaders.h"
#include <stdio.h>
#include <string.h>
#include <utime.h>

/* A failed write and its completed replacement deliberately have the same
 * timestamp. Retrying must depend on the last successful load. */
static bool write_shader(const char *path, const void *data, size_t bytes, time_t stamp) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(data, 1, bytes, f) == bytes;
    if (fclose(f) != 0) return false;
    struct utimbuf t = {.actime = stamp, .modtime = stamp};
    return ok && utime(path, &t) == 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return 1; /* output prefix, replacement SPIR-V, vkmin options */
    char shader[512], capture[512];
    if (snprintf(shader, sizeof shader, "%s.frag.spv", argv[1]) >= (int)sizeof shader) return 2;
    uint32_t replacement[4096];
    FILE *f = fopen(argv[2], "rb");
    if (!f) return 3;
    const size_t size = fread(replacement, 1, sizeof replacement, f);
    fclose(f);
    if (!size || !write_shader(shader, ex_color_frag_spv, sizeof ex_color_frag_spv, 100)) return 4;
    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .headless = true, .width = 256, .height = 256});
    const vkmin_pipeline p = vkmin_make_pipeline(c, &(vkmin_pipeline_desc){
        .vs = VKMIN_BYTES(ex_tri_vert_spv), .fs = VKMIN_BYTES(ex_color_frag_spv), .fs_path = shader, .label = "reload.test"});
    uint32_t count = 0;
    while (vkmin_running(c)) {
        const uint32_t invalid = 0;
        if (count == 1 && !write_shader(shader, &invalid, sizeof invalid, 101)) return 5;
        if (count == 2 && !write_shader(shader, replacement, size, 101)) return 6;
        if (count == 3 && !write_shader(shader, ex_color_frag_spv, sizeof ex_color_frag_spv, 102)) return 7;
        (void)vkmin_frame_begin(c, &(vkmin_clear){0.08f, 0.08f, 0.12f, 1.0f});
        vkmin_draw(c, p, NULL, 3, 1); /* same handle before and after every reload */
        vkmin_frame_end(c);
        if (snprintf(capture, sizeof capture, "%s-%u.png", argv[1], count) >= (int)sizeof capture) return 8;
        if (!vkmin_save_png(c, capture)) return 9;
        ++count;
    }
    vkmin_shutdown(c);
    return count == 4 ? 0 : 10;
}
