/* 06_cube -- the acceptance test for the API's shape: a spinning textured
 * cube with depth, in the lines below and no more. Animation is a pure
 * function of the frame index, so --frame 60 is the same pixels every run. */
#include "vkmin.h"
#include "render_shared.h"
#include "vkmin_math.h"
#include "shaders.h"
#include "cube_data.h"

static mat4 cube_mvp(uint32_t frame, float aspect) { // pure
    const float a = (float)frame * (6.2831853f / 240.0f);
    const mat4 model = vkmin_mat4_mul(vkmin_mat4_rotate_y(a), vkmin_mat4_rotate_x(a * 0.5f));
    const mat4 view = vkmin_mat4_look_at((vec3){0, 0, 4.2f}, (vec3){0, 0, 0}, (vec3){0, 1, 0});
    const mat4 proj = vkmin_mat4_perspective(50.0f * 3.14159265f / 180.0f, aspect, 0.1f, 20.0f);
    return vkmin_mat4_mul(proj, vkmin_mat4_mul(view, model));
}

int main(int argc, char **argv) {
    vkmin_ctx *ctx = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "06_cube"});
    vkmin_buffer vb = vkmin_make_buffer(ctx, &(vkmin_buffer_desc){.data = VKMIN_BYTES(cube_verts)});
    vkmin_image tex = vkmin_load_png(ctx, "tests/assets/grid.png", false);
    vkmin_pipeline pipe = vkmin_make_pipeline(ctx, &(vkmin_pipeline_desc){
        .vs = VKMIN_BYTES(ex_vertex_vert_spv), .fs = VKMIN_BYTES(ex_textured_frag_spv),
        .push_size = sizeof(ExPush), .depth = true, .depth_write = true, .cull = VKMIN_CULL_BACK});
    const uint64_t verts = vkmin_address(ctx, vb); /* stable for the buffer's lifetime */
    const uint32_t grid = vkmin_index(ctx, tex);
    while (vkmin_running(ctx)) {
        const vkmin_frame f = vkmin_frame_begin(ctx, &(vkmin_clear){.r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f});
        const mat4 mvp = cube_mvp(f.index, f.aspect); /* pure */
        vkmin_draw(ctx, pipe, &(ExPush){.mvp = mvp, .vertices = verts, .texture = grid}, CUBE_VERTS, 1);
        vkmin_frame_end(ctx);
    }
    vkmin_shutdown(ctx);
    return 0;
}
