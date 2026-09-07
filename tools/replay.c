/* replay -- plays a journal back. Record one with any program:
 *   ./build/omega --headless --frame 300 --record frame300.vkj
 *   ./build/replay --replay frame300.vkj --frame 300 --out shot.png
 * The replay creates the same resources and issues the same calls; the pixels
 * are the same. The program that recorded it is not needed, nor its shaders,
 * nor its assets: the journal carries everything. */
#include "vkmin.h"

#include <string.h>

int main(int argc, char **argv) {
    const char *journal = NULL;
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--replay") == 0) journal = argv[i + 1];
    }
    if (!journal) {
        fprintf(stderr, "usage: replay --replay FILE [--frame N] [--out PNG] [--path=legacy|modern]\n");
        return 2;
    }
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "replay"});
    const bool ok = vkmin_replay(ctx, journal);
    vkmin_shutdown(ctx);
    return ok ? 0 : 1;
}
