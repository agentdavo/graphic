/* 03_buffer -- vertices in a buffer, reached by device address. The address
 * rides in the push block; the shader pulls ExVertex records from it. */
#include "vkmin.h"
#include "vkmin_math.h"
#include "shaders.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "03_buffer"});
    const ExVertex quad[6] = {
        {-0.7f, -0.7f, 0, 0xff4040ffu, 0, 1, 0, 0}, {0.7f, -0.7f, 0, 0xff40ff40u, 1, 1, 0, 0},
        {0.7f, 0.7f, 0, 0xffff4040u, 1, 0, 0, 0},   {-0.7f, -0.7f, 0, 0xff4040ffu, 0, 1, 0, 0},
        {0.7f, 0.7f, 0, 0xffff4040u, 1, 0, 0, 0},   {-0.7f, 0.7f, 0, 0xffffff40u, 0, 0, 0, 0},
    };
    vkmin_buffer vb = vkmin_make_buffer(ctx, &(vkmin_buffer_desc){.data = VKMIN_BYTES(quad), .label = "quad"});
    vkmin_pipeline pipe = vkmin_make_pipeline(ctx, &(vkmin_pipeline_desc){
        .vs = VKMIN_BYTES(ex_vertex_vert_spv), .fs = VKMIN_BYTES(ex_color_frag_spv),
        .push_size = sizeof(ExPush), .cull = VKMIN_CULL_NONE, .label = "buffer"});
    const uint64_t verts = vkmin_address(ctx, vb); /* stable for the buffer's lifetime */
    while (vkmin_running(ctx)) {
        vkmin_frame_begin(ctx, &(vkmin_clear){.r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f});
        vkmin_draw(ctx, pipe, &(ExPush){.mvp = vkmin_mat4_ortho(-1, 1, -1, 1, 0, 1) /* y up */, .vertices = verts}, 6, 1);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
