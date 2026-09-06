/* Two spatially disjoint draws, each pulling vertices from the frame ring.
 * Stopping after draw one must retain its late journal payload and omit draw two. */
#include "vkmin.h"
#include "render_shared.h"
#include "vkmin_math.h"
#include "shaders.h"
#include <string.h>

int main(int argc, char **argv) {
    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "inspection"});
    const vkmin_pipeline pipeline = vkmin_make_pipeline(c, &(vkmin_pipeline_desc){
        .vs = VKMIN_BYTES(ex_vertex_vert_spv), .fs = VKMIN_BYTES(ex_color_frag_spv),
        .push_size = sizeof(ExPush), .cull = VKMIN_CULL_NONE, .label = "ring-triangle"});
    const ExVertex vertices[3] = {
        {-0.8f,-0.8f,0,0xffffffffu,0,0,0,0}, {0.8f,-0.8f,0,0xffffffffu,0,0,0,0},
        {0,0.8f,0,0xffffffffu,0,0,0,0}};
    while (vkmin_running(c)) {
        const vkmin_frame frame = vkmin_frame_begin(c, NULL);
        vkmin_pass_begin(c, &(vkmin_pass_desc){.color = vkmin_backbuffer(c), .depth = vkmin_default_depth(c),
            .clear_color = true, .clear_depth = true, .clear = {0,0,0,1}, .label = "two triangles"});
        for (int i = 0; i < 2; ++i) {
            uint64_t address = 0;
            void *mapped = vkmin_ring_alloc(c, sizeof vertices, &address);
            memcpy(mapped, vertices, sizeof vertices);
            vkmin_set_viewport(c, i * frame.width / 2, 0, frame.width / 2, frame.height);
            vkmin_draw(c, pipeline, &(ExPush){.mvp = vkmin_mat4_identity(), .vertices = address}, 3, 1);
        }
        vkmin_pass_end(c);
        vkmin_frame_end(c);
    }
    vkmin_shutdown(c);
    return 0;
}
