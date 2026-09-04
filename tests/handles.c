/* handles.c -- the generation test. Free a resource, create another that lands
 * in the same slot, and prove the old handle is rejected rather than aliasing
 * the new one. The rejection is an abort, so the test forks: the child must
 * die, the parent checks how. */
#include "vkmin.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static vkmin_ctx *make_ctx(void) {
    return vkmin_init(&(vkmin_desc){.headless = true, .width = 16, .height = 16,
                                    .device_arena_bytes = 4u << 20, .host_ring_bytes = 1u << 20});
}

int main(void) {
    const uint8_t px[4] = {1, 2, 3, 4};
    vkmin_ctx *c = make_ctx();
    const vkmin_buffer_desc bd = {.size = 64, .data = px, .label = "handles.buffer"};
    const vkmin_image_desc id = {.width = 1, .height = 1, .format = VKMIN_FMT_RGBA8_UNORM,
                                 .usage = VKMIN_IMAGE_SAMPLED, .label = "handles.image"};
    const vkmin_buffer b0 = vkmin_make_buffer(c, &bd);
    const vkmin_image i0 = vkmin_make_image(c, &id);
    vkmin_free_buffer(c, b0);
    vkmin_free_image(c, i0);
    const vkmin_buffer b1 = vkmin_make_buffer(c, &bd);
    const vkmin_image i1 = vkmin_make_image(c, &id);
    /* Same slot, different id: the generation moved. */
    if ((b0.id & 0xfffff) != (b1.id & 0xfffff) || b0.id == b1.id) { puts("handles: FAIL buffer slot/gen"); return 1; }
    if ((i0.id & 0xfffff) != (i1.id & 0xfffff) || i0.id == i1.id) { puts("handles: FAIL image slot/gen"); return 1; }
    (void)vkmin_buffer_addr(c, b1); /* the live handle works */

    /* The stale handle must abort. */
    fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        if (!freopen("/dev/null", "w", stderr)) _exit(2);
        (void)vkmin_buffer_addr(c, b0);
        _exit(0); /* reached only if the stale handle was accepted */
    }
    int status = 0;
    waitpid(pid, &status, 0);
    const bool aborted = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    vkmin_shutdown(c);
    if (!aborted) { puts("handles: FAIL stale buffer handle was accepted"); return 1; }
    puts("handles: ok (stale handle aborted, slot reused with a new generation)");
    return 0;
}
