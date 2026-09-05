#include "vkmin.h"
#include "process.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    float heights[17*17]; vec4 normal[17*17], flow[17*17];
    for (int z = 0; z < 17; ++z) for (int x = 0; x < 17; ++x) heights[z*17+x] = (float)(x-2*z);
    if (argc == 2 && !strcmp(argv[1],"nan")) heights[3] = NAN;
    vkmin_terrain_desc desc = {.heightfield = {.heights = heights, .width = 17, .height = 17, .cell = 2, .chunk = 2}, .skirt = 8};
    if (argc == 2) {
        if (!strcmp(argv[1],"size")) desc.heightfield.width = 16;
        else if (!strcmp(argv[1],"capacity")) { vkmin_terrain_normal(&desc.heightfield,normal,1); return 20; }
        (void)vkmin_terrain_sizes(&desc);
        vkmin_terrain_flow(&desc.heightfield,flow,17*17);
        return 21;
    }
    vkmin_terrain_normal(&desc.heightfield,normal,17*17);
    vkmin_terrain_flow(&desc.heightfield,flow,17*17);
    for (int k = 0; k < 17*17; ++k) {
        if (fabsf(normal[k].x+1.0f/3) > 1e-6f || fabsf(normal[k].y-2.0f/3) > 1e-6f || fabsf(normal[k].z-2.0f/3) > 1e-6f) return 1;
        if (fabsf(flow[k].x+1/sqrtf(5)) > 1e-6f || fabsf(flow[k].z-2/sqrtf(5)) > 1e-6f || flow[k].w <= 0) return 2;
    }
    const vkmin_heightfield_size n = vkmin_terrain_sizes(&desc);
    if (n.meshes != 85 || n.vertices != 1785 || n.indices != 6120) return 3;
    Vertex *v = malloc(n.vertices*sizeof(Vertex)); uint32_t *idx = malloc(n.indices*sizeof(uint32_t)); Mesh meshes[85];
    if (!v || !idx) return 4;
    vkmin_terrain(&desc,v,idx,meshes);
    uint32_t at = 0;
    for (uint32_t m = 0; m < 85; ++m) {
        if (meshes[m].first_index != at) return 5;
        for (uint32_t k = 0; k < meshes[m].index_count; ++k) {
            const uint32_t id = idx[at++];
            if (id >= n.vertices) return 6;
            const vec4 b = meshes[m].bounds;
            const float dx = v[id].px-b.x, dy = v[id].py-b.y, dz = v[id].pz-b.z;
            if (dx*dx+dy*dy+dz*dz > b.w*b.w+1e-3f) return 7;
        }
        for (uint32_t k = 0; k < 24; k += 3) {
            const Vertex a = v[idx[meshes[m].first_index+k]], b = v[idx[meshes[m].first_index+k+1]], c = v[idx[meshes[m].first_index+k+2]];
            if ((b.pz-a.pz)*(c.px-a.px)-(b.px-a.px)*(c.pz-a.pz) <= 0) return 8;
        }
    }
    for (uint32_t trial = 0; trial < 5; ++trial) {
        uint32_t out[64];
        const uint32_t count = vkmin_terrain_select(meshes,(vec4){(float)trial*80,2,3,0},1.2f,trial == 4,out,64);
        if (count == 0 || count > 64 || (trial == 4 && count != 64)) return 9;
        for (uint32_t leaf = 21; leaf < 85; ++leaf) {
            uint32_t cover = 0;
            for (uint32_t k = 0; k < count; ++k) {
                uint32_t parent = leaf;
                for (;;) { if (parent == out[k]) ++cover; if (parent == 0) break; parent = (parent-1)/4; }
            }
            if (cover != 1) return 10;
        }
    }
    memset(heights,0,sizeof heights);
    vkmin_terrain_flow(&desc.heightfield,flow,17*17);
    for (int k = 0; k < 17*17; ++k) if (flow[k].x != 0 || flow[k].z != 0 || flow[k].w != 0) return 11;
    for (uint32_t frame = 0; frame < 32; ++frame) {
        const vec2 jitter = vkmin_taa_jitter(frame), repeated = vkmin_taa_jitter(frame+16);
        if (jitter.x < -.5f || jitter.x >= .5f || jitter.y < -.5f || jitter.y >= .5f || memcmp(&jitter,&repeated,sizeof jitter)) return 13;
    }
    const vec4 sunrise = vkmin_sun_direction(6), noon = vkmin_sun_direction(12);
    if (fabsf(sunrise.y) > 1e-6f || noon.y < .95f || fabsf(noon.x) > 1e-6f) return 14;
    free(v); free(idx);
    if (!test_aborts(argv[0],"size") || !test_aborts(argv[0],"nan") || !test_aborts(argv[0],"capacity")) return 12;
    puts("outside: plane derivatives at every border, flat flow, skirt bounds, winding, disjoint LOD coverage, invalid inputs: ok");
    return 0;
}
