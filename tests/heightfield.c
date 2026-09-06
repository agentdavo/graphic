#include "vkmin.h"
#include "render_geometry.h"
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include "process.h"

int main(int argc, char **argv) {
    if (argc == 2) {
        const float sample = 0;
        const vkmin_heightfield_desc invalid = {.heights = &sample,
            .width = !strcmp(argv[1], "overflow") ? INT_MAX : 2,
            .height = !strcmp(argv[1], "overflow") ? INT_MAX : 2,
            .cell = !strcmp(argv[1], "scale") ? NAN : 1};
        (void)vkmin_heightfield_sizes(&invalid);
        return 10;
    }
    float heights[15];
    for (int z = 0; z < 5; ++z) for (int x = 0; x < 3; ++x) heights[z * 3 + x] = (float)(x - z);
    const vkmin_heightfield_desc d = {.heights = heights, .width = 3, .height = 5, .cell = 2, .chunk = 3};
    const vkmin_heightfield_size n = vkmin_heightfield_sizes(&d);
    if (n.vertices != 15 || n.indices != 48 || n.meshes != 2) return 1;
    Vertex v[15]; uint32_t idx[48]; Mesh meshes[2];
    vkmin_heightfield(&d, v, idx, meshes);
    uint32_t at = 0;
    for (uint32_t m = 0; m < n.meshes; ++m) {
        const Mesh b = meshes[m];
        if (b.first_index != at) return 2;
        for (uint32_t i = 0; i < b.index_count; ++i) {
            const uint32_t id = idx[at++];
            if (id >= n.vertices) return 3;
            const float x = v[id].px - b.bounds.x, y = v[id].py - b.bounds.y, z = v[id].pz - b.bounds.z;
            if (x*x + y*y + z*z > b.bounds.w*b.bounds.w + 1e-4f) return 4;
        }
    }
    for (uint32_t i = 0; i < n.indices; i += 3) {
        const Vertex a = v[idx[i]], b = v[idx[i+1]], c = v[idx[i+2]];
        if ((b.pz-a.pz)*(c.px-a.px) - (b.px-a.px)*(c.pz-a.pz) <= 0) return 5;
    }
    if (!test_aborts(argv[0], "overflow") || !test_aborts(argv[0], "scale")) return 7;
    puts("heightfield: ok (partial chunks, topology, bounds, invalid dimensions/scales)");
    return at == n.indices ? 0 : 6;
}
