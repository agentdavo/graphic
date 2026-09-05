/* 02_triangle -- one pipeline, one draw, no buffers: the vertex shader makes
 * the triangle from gl_VertexIndex. Pipeline creation with nothing else. */
#include "vkmin.h"
#include "shaders.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "02_triangle"});
    vkmin_pipeline pipe = vkmin_make_pipeline(ctx, &(vkmin_pipeline_desc){
        .vs = VKMIN_BYTES(ex_tri_vert_spv), .fs = VKMIN_BYTES(ex_color_frag_spv), .cull = VKMIN_CULL_NONE, .label = "triangle"});
    while (vkmin_running(ctx)) {
        vkmin_frame_begin(ctx, &(vkmin_clear){.r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f});
        vkmin_draw(ctx, pipe, NULL, 3, 1);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
