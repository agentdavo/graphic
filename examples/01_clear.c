/* 01_clear -- the shortest program: a context, a cleared frame, a PNG.
 *   ./build/ex_01_clear --headless --frame 0 --out clear.png */
#include "vkmin.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "01_clear"});
    while (vkmin_frame_begin(ctx, &(vkmin_clear){.r = 1.0f, .g = 0.0f, .b = 1.0f, .a = 1.0f})) {
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
