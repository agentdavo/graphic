/* handles.c -- the generation test. Free a resource, create another that lands
 * in the same slot, and prove the old handle is rejected rather than aliasing
 * the new one. Each rejection runs in a fresh child process: no Vulkan
 * threads or driver locks are inherited by a child that uses the driver. */
#include "vkmin.h"
#include "shaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process.h"

static vkmin_ctx *make_ctx(void) {
    return vkmin_init(&(vkmin_desc){.headless = true, .width = 16, .height = 16,
                                    .device_arena_bytes = 4u << 20, .host_ring_bytes = 1u << 20});
}

int main(int argc, char **argv) {
    const uint8_t px[4] = {1, 2, 3, 4};
    vkmin_ctx *c = make_ctx();
    const vkmin_buffer_desc bd = {.size = 64, .data = {px, sizeof px}, .label = "handles.buffer"};
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
    (void)vkmin_address(c, b1); /* the live handle works */

    if (argc == 2) {
        if (!strcmp(argv[1], "stale")) (void)vkmin_address(c, b0);
        else if (!strcmp(argv[1], "image")) (void)vkmin_index(c, i0);
        else if (!strcmp(argv[1], "push"))
            (void)vkmin_make_pipeline(c, &(vkmin_pipeline_desc){.cs = VKMIN_BYTES(smoke_comp_spv), .push_size = sizeof(Push) + 16});
        else if (!strcmp(argv[1], "ring")) {
            /* The journal relocates only ring addresses vkmin registered. Passing
             * the per-frame limit must abort here, not stop registering quietly. */
            (void)vkmin_running(c);
            (void)vkmin_frame_begin(c, &(vkmin_clear){0});
            for (int k = 0; k < 4096; ++k) (void)vkmin_ring_alloc(c, 16, NULL);
        }
        vkmin_shutdown(c);
        return 0;
    }
    (void)vkmin_make_pipeline(c, &(vkmin_pipeline_desc){.cs = VKMIN_BYTES(smoke_comp_spv), .push_size = sizeof(Push), .label = "right push"});
    vkmin_shutdown(c);
    if (!test_aborts(argv[0], "stale") || !test_aborts(argv[0], "image") || !test_aborts(argv[0], "push") ||
        !test_aborts(argv[0], "ring")) {
        puts("handles: FAIL a negative case did not abort"); return 1;
    }
    puts("handles: ok (stale buffer/image, wrong push size and ring allocation overflow rejected)");
    return 0;
}
