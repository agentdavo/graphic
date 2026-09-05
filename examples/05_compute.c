/* 05_compute -- a compute dispatch writes the vertices, a barrier hands them
 * to the draw. Both address the same buffer through the same push block. The
 * dispatch must sit outside a pass, so this one manages its own pass. */
#include "vkmin.h"
#include "vkmin_math.h"
#include "shaders.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "05_compute"});
    vkmin_buffer vb = vkmin_make_buffer(ctx, &(vkmin_buffer_desc){.size = 3 * sizeof(ExVertex), .label = "spin"});
    vkmin_pipeline spin = vkmin_make_pipeline(ctx, &(vkmin_pipeline_desc){.cs = VKMIN_BYTES(ex_spin_comp_spv), .push_size = sizeof(ExPush), .label = "spin"});
    vkmin_pipeline draw = vkmin_make_pipeline(ctx, &(vkmin_pipeline_desc){
        .vs = VKMIN_BYTES(ex_vertex_vert_spv), .fs = VKMIN_BYTES(ex_color_frag_spv),
        .push_size = sizeof(ExPush), .cull = VKMIN_CULL_NONE, .label = "draw"});
    while (vkmin_running(ctx)) {
        const vkmin_frame f = vkmin_frame_begin(ctx, NULL);
        const ExPush push = {.mvp = vkmin_mat4_ortho(-1, 1, -1, 1, 0, 1) /* y up */, .vertices = vkmin_address(ctx, vb),
                             .frame = f.index, .time = (float)f.index / 60.0f};
        vkmin_barrier(ctx, &(vkmin_barrier_desc){.frame_start = true});
        vkmin_dispatch(ctx, spin, &push, 1, 1, 1);
        vkmin_barrier(ctx, &(vkmin_barrier_desc){.compute_to_indirect_draw = true});
        vkmin_pass_begin(ctx, &(vkmin_pass_desc){.color = vkmin_backbuffer(ctx), .clear_color = true,
                                                 .clear = {0.1f, 0.1f, 0.12f, 1.0f}, .label = "draw"});
        vkmin_draw(ctx, draw, &push, 3, 1);
        vkmin_pass_end(ctx);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
