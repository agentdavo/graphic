#include "vkmin.h"
#include <stdio.h>

/* Repeated queries must not discard records, and end-of-demo is stable. */
int main(int argc, char **argv) {
    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .headless = true});
    uint32_t count = 0;
    while (vkmin_running(c)) {
        if (!vkmin_running(c) || !vkmin_running(c)) return 1;
        const vkmin_frame f = vkmin_frame_begin(c, &(vkmin_clear){0});
        if (f.index != count) return 2;
        ++count;
        vkmin_frame_end(c);
    }
    if (vkmin_running(c) || count != 3) return 3;
    vkmin_shutdown(c);
    puts("lifecycle: ok (three frames offered and consumed once)");
    return 0;
}
