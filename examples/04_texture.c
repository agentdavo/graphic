/* 04_texture -- a texture is a number too: vkmin_index gives the bindless
 * index and the fragment shader samples textures[push.texture]. */
#include "vkmin.h"
#include "render_shared.h"
#include "vkmin_math.h"
#include "shaders.h"

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "04_texture"});
    const ExVertex quad[6] = {
        {-0.8f, -0.8f, 0, 0xffffffffu, 0, 1, 0, 0}, {0.8f, -0.8f, 0, 0xffffffffu, 1, 1, 0, 0},
        {0.8f, 0.8f, 0, 0xffffffffu, 1, 0, 0, 0},   {-0.8f, -0.8f, 0, 0xffffffffu, 0, 1, 0, 0},
        {0.8f, 0.8f, 0, 0xffffffffu, 1, 0, 0, 0},   {-0.8f, 0.8f, 0, 0xffffffffu, 0, 0, 0, 0},
    };
    vkmin_buffer vb = vkmin_make_buffer(ctx, &(vkmin_buffer_desc){.data = VKMIN_BYTES(quad), .label = "quad"});
    vkmin_image tex = vkmin_load_png(ctx, "tests/assets/grid.png", false);
    vkmin_pipeline pipe = vkmin_make_pipeline(ctx, &(vkmin_pipeline_desc){
        .vs = VKMIN_BYTES(ex_vertex_vert_spv), .fs = VKMIN_BYTES(ex_textured_frag_spv),
        .push_size = sizeof(ExPush), .cull = VKMIN_CULL_NONE, .label = "textured"});
    const uint64_t verts = vkmin_address(ctx, vb);
    const uint32_t grid = vkmin_index(ctx, tex);
    while (vkmin_running(ctx)) {
        vkmin_frame_begin(ctx, &(vkmin_clear){.r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f});
        vkmin_draw(ctx, pipe, &(ExPush){.mvp = vkmin_mat4_ortho(-1, 1, -1, 1, 0, 1) /* y up */, .vertices = verts, .texture = grid}, 6, 1);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
