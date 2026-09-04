/* 05_compute -- a compute dispatch writes the vertices, a barrier hands them
 * to the draw. Both address the same buffer through the same push block. The
 * dispatch must sit outside a pass, so this one manages its own pass. */
#include "vkmin.h"
#include "vkmin_math.h"
#include "shaders.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "05_compute"});
    vkmin_buffer vb = vkmin_make_buffer(ctx, &(vkmin_buffer_desc){.size = 3 * sizeof(ExVertex), .label = "spin"});
    vkmin_pipe spin = vkmin_make_pipeline(ctx, &(vkmin_pipe_desc){.cs = ex_spin_comp_spv, .cs_bytes = sizeof ex_spin_comp_spv, .label = "spin"});
    vkmin_pipe draw = vkmin_make_pipeline(ctx, &(vkmin_pipe_desc){
        .vs = ex_vertex_vert_spv, .vs_bytes = sizeof ex_vertex_vert_spv,
        .fs = ex_color_frag_spv, .fs_bytes = sizeof ex_color_frag_spv, .cull = VKMIN_CULL_NONE, .label = "draw"});
    while (vkmin_frame_begin(ctx, NULL)) {
        const ExPush push = {.mvp = vkmin_mat4_ortho(-1, 1, -1, 1, 0, 1) /* y up */, .vertices = vkmin_address(ctx, vb),
                             .frame = vkmin_frame_index(ctx), .time = (float)vkmin_frame_index(ctx) / 60.0f};
        vkmin_barrier(ctx, &(vkmin_barrier_desc){.frame_start = true});
        vkmin_dispatch(ctx, spin, &push, sizeof push, 1, 1, 1);
        vkmin_barrier(ctx, &(vkmin_barrier_desc){.compute_to_indirect_draw = true});
        vkmin_pass_begin(ctx, &(vkmin_pass_desc){.color = vkmin_backbuffer(ctx), .clear_color = true,
                                                 .clear = {0.1f, 0.1f, 0.12f, 1.0f}, .label = "draw"});
        vkmin_draw(ctx, draw, &push, sizeof push, 3, 1);
        vkmin_pass_end(ctx);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
