/* Built with only the exported audio headers, archive and null backend. */
#include "sndmin.h"
#ifdef VKMIN_GPU_H
#error "Audio must not depend on graphics"
#endif
#ifdef RENDER_SHARED_H
#error "Audio must not depend on the renderer"
#endif
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    sndmin_ctx *audio = sndmin_init(&(sndmin_desc){.offline = true});
    if (!audio) return 1;
    sndmin_frame(audio, &(sndmin_frame_desc){0});
    const bool ok = sndmin_ok(audio) && sndmin_render(audio, 1, argv[1], NULL);
    sndmin_shutdown(audio);
    return ok ? 0 : 1;
}
