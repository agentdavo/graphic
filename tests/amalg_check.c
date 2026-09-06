/* amalg_check.c -- proves the generated single header compiles and links on
 * its own: one translation unit, the implementation define, nothing else. */
#define VKMIN_IMPLEMENTATION
#include "vkmin_single.h"
#ifdef RENDER_SHARED_H
#error "The GPU package must not include renderer layouts"
#endif

int main(void) {
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.headless = true, .width = 32, .height = 32});
    if (!vkmin_running(gpu)) { vkmin_shutdown(gpu); return 1; }
    (void)vkmin_frame_begin(gpu, &(vkmin_clear){0});
    vkmin_frame_end(gpu);
    vkmin_shutdown(gpu);
    return 0;
}
