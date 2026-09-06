/* Link the renderer as a separate consumer, including its geometry helpers. */
#include "render.h"
int main(void) {
    const float heights[4] = {0, 0, 0, 0};
    const vkmin_heightfield_size size = vkmin_heightfield_sizes(
        &(vkmin_heightfield_desc){.heights = heights, .width = 2, .height = 2});
    if (size.vertices != 4 || size.indices != 6 || size.meshes != 1) return 1;
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.headless = true, .width = 32, .height = 32});
    vkr *renderer = vkr_init(gpu, &(vkr_desc){.width = 32, .height = 32, .shadow_atlas = 256,
        .max_vertices = 4, .max_indices = 6, .max_meshes = 1, .max_materials = 1, .max_instances = 1});
    vkr_shutdown(renderer);
    vkmin_shutdown(gpu);
    return 0;
}
