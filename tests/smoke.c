/* smoke.c -- the smallest program that exercises every mechanism the GPU
 * layer offers: a compute dispatch writing vertices by device address, a
 * barrier into a draw, vertex pulling, a bindless texture sample, timestamps,
 * and the PNG readback. If this is right, the renderer is built on a floor
 * that has been stood on. */
#include "vkmin.h"
#include "ktx2.h"
#include "shaders.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "tests/out/smoke.png";
    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.headless = true, .width = 256, .height = 256,
                                            .device_arena_bytes = 16u << 20,
                                            .host_ring_bytes = 4u << 20});

    /* A second argument names a KTX2 to sample instead of the PNG, which is
     * how the cooker's BCn encoders get looked at through a real sampler. */
    const vkmin_image grid = argc > 2 ? ktx2_load(c, argv[2]) : vkmin_load_png(c, "tests/assets/grid.png", false);
    const uint32_t grid_tex = vkmin_register_texture(c, grid, VKMIN_SAMPLER_LINEAR_CLAMP);
    const vkmin_buffer verts = vkmin_make_buffer(c, &(vkmin_buffer_desc){.size = 3 * sizeof(Vertex), .label = "smoke.verts"});
    const vkmin_pipe fill = vkmin_make_compute(c, smoke_comp_spv, sizeof smoke_comp_spv, "smoke.fill");
    const vkmin_pipe draw = vkmin_make_pipeline(
        c, &(vkmin_pipe_desc){.vs = smoke_vert_spv, .vs_bytes = sizeof smoke_vert_spv,
                              .fs = smoke_frag_spv, .fs_bytes = sizeof smoke_frag_spv,
                              .color_format = VKMIN_FMT_RGBA8_UNORM, .cull = VKMIN_CULL_NONE,
                              .label = "smoke.draw"});

    /* Three frames: the second runs with the first still in flight, and the
     * third reads back the timestamps the first one wrote. */
    for (int frame = 0; frame < 3; ++frame) {
        vkmin_frame_begin(c);
        vkmin_timestamp(c, 0);
        vkmin_barrier(c, &(vkmin_barrier_desc){.frame_start = true});
        vkmin_bind_pipeline(c, fill);
        vkmin_push(c, &(Push){.aux = vkmin_buffer_addr(c, verts)});
        vkmin_dispatch(c, 1, 1, 1);
        vkmin_timestamp(c, 1);
        vkmin_barrier(c, &(vkmin_barrier_desc){.compute_to_indirect_draw = true});
        vkmin_pass_begin(c, &(vkmin_pass_desc){.color = vkmin_backbuffer(c), .clear_color = true,
                                               .clear = {0.1f, 0.1f, 0.12f, 1.0f}, .label = "smoke"});
        vkmin_bind_pipeline(c, draw);
        vkmin_push(c, &(Push){.aux = vkmin_buffer_addr(c, verts), .param = grid_tex});
        vkmin_draw(c, 3, 1);
        vkmin_pass_end(c);
        vkmin_timestamp(c, 2);
        vkmin_frame_end(c);
    }
    double ms[4];
    const int n = vkmin_timestamps_read(c, ms, 4);
    printf("smoke: %d timestamps", n);
    for (int i = 0; i < n; ++i) printf(" %.3fms", ms[i]);
    printf("\n");
    const bool ok = vkmin_save_png(c, out);
    vkmin_shutdown(c);
    return ok ? 0 : 1;
}
