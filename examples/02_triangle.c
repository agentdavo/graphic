/* 02_triangle -- one pipeline, one draw, no buffers: the vertex shader makes
 * the triangle from gl_VertexIndex. Pipeline creation with nothing else. */
#include "vkmin.h"
#include "shaders.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "02_triangle"});
    vkmin_pipe pipe = vkmin_make_pipeline(ctx, &(vkmin_pipe_desc){
        .vs = ex_tri_vert_spv, .vs_bytes = sizeof ex_tri_vert_spv,
        .fs = ex_color_frag_spv, .fs_bytes = sizeof ex_color_frag_spv,
        .cull = VKMIN_CULL_NONE, .label = "triangle"});
    while (vkmin_frame_begin(ctx, &(vkmin_clear){.r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f})) {
        vkmin_draw(ctx, pipe, NULL, 0, 3, 1);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
