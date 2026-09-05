#include "spirv.h"
#include "shaders.h"
#include "shared.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    uint32_t size = 0;
    if (!vkm_spirv_push_size(smoke_comp_spv, sizeof smoke_comp_spv, &size) || size != sizeof(Push)) return 1;
    if (!vkm_spirv_push_size(ex_vertex_vert_spv, sizeof ex_vertex_vert_spv, &size) || size != sizeof(ExPush)) return 2;
    if (!vkm_spirv_push_size(ex_tri_vert_spv, sizeof ex_tri_vert_spv, &size) || size) return 3;
    uint32_t bad[sizeof smoke_comp_spv / 4];
    memcpy(bad, smoke_comp_spv, sizeof bad);
    bad[5] &= 65535u; /* a zero-length instruction must terminate with an error */
    if (vkm_spirv_push_size(bad, sizeof bad, &size)) return 4;
    memcpy(bad, smoke_comp_spv, sizeof bad);
    bad[5] = (65535u << 16) | 21u;
    if (vkm_spirv_push_size(bad, sizeof bad, &size)) return 5;
    bad[3] = UINT32_MAX;
    if (vkm_spirv_push_size(bad, sizeof bad, &size)) return 6;
    if (argc != 2) return 7;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 8;
    uint32_t fixture[4096];
    const size_t bytes = fread(fixture, 1, sizeof fixture, f);
    fclose(f);
    if (!vkm_spirv_push_size(fixture, bytes, &size) || size != 80) {
        fprintf(stderr, "padded row-major matrix/array: got %u, want 80\n", size); return 9;
    }
    puts("spirv_test: ok (shared layouts, padded matrix/array, malformed instructions)");
    return 0;
}
