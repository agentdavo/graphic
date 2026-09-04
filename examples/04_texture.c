/* 04_texture -- a texture is a number too: vkmin_index gives the bindless
 * index and the fragment shader samples textures[push.texture]. */
#include "vkmin.h"
#include "vkmin_math.h"
#include "shaders.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "04_texture"});
    const ExVertex quad[6] = {
        {-0.8f, -0.8f, 0, 0xffffffffu, 0, 1, 0, 0}, {0.8f, -0.8f, 0, 0xffffffffu, 1, 1, 0, 0},
        {0.8f, 0.8f, 0, 0xffffffffu, 1, 0, 0, 0},   {-0.8f, -0.8f, 0, 0xffffffffu, 0, 1, 0, 0},
        {0.8f, 0.8f, 0, 0xffffffffu, 1, 0, 0, 0},   {-0.8f, 0.8f, 0, 0xffffffffu, 0, 0, 0, 0},
    };
    vkmin_buffer vb = vkmin_make_buffer(ctx, &(vkmin_buffer_desc){.data = quad, .size = sizeof quad, .label = "quad"});
    vkmin_image tex = vkmin_load_png(ctx, "tests/assets/grid.png", false);
    vkmin_pipe pipe = vkmin_make_pipeline(ctx, &(vkmin_pipe_desc){
        .vs = ex_vertex_vert_spv, .vs_bytes = sizeof ex_vertex_vert_spv,
        .fs = ex_textured_frag_spv, .fs_bytes = sizeof ex_textured_frag_spv, .cull = VKMIN_CULL_NONE, .label = "textured"});
    while (vkmin_frame_begin(ctx, &(vkmin_clear){.r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f})) {
        const ExPush push = {.mvp = vkmin_mat4_ortho(-1, 1, -1, 1, 0, 1) /* y up */, .vertices = vkmin_address(ctx, vb), .texture = vkmin_index(ctx, tex)};
        vkmin_draw(ctx, pipe, &push, sizeof push, 6, 1);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
