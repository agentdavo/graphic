/* Renderer geometry: deterministic, allocation-free CPU routines. */
#include "render_geometry.h"
#include <stdio.h>
#include <stdlib.h>
#include "pack.h"
#include <math.h>

#define VKR_ASSERT(cond, ...)                                                     \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fprintf(stderr, "%s:%d: render: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                         \
            fputc('\n', stderr);                                                  \
            abort();                                                              \
        }                                                                         \
    } while (0)

/* ----------------------------------------------------------- heightfield --- */

vkmin_heightfield_size vkmin_heightfield_sizes(const vkmin_heightfield_desc *d) {
    VKR_ASSERT(d && d->heights && d->width >= 2 && d->height >= 2, "vkmin_heightfield: need a grid of at least 2x2 samples");
    VKR_ASSERT(d->chunk >= 0 && isfinite(d->cell) && d->cell >= 0 && isfinite(d->uv_per_unit) && d->uv_per_unit >= 0,
                 "vkmin_heightfield: invalid scale or chunk size");
    const uint32_t chunk = d->chunk > 0 ? (uint32_t)d->chunk : 32u;
    const uint32_t cx = ((uint32_t)d->width - 1u + chunk - 1u) / chunk, cz = ((uint32_t)d->height - 1u + chunk - 1u) / chunk;
    const uint64_t vertices = (uint64_t)d->width * (uint64_t)d->height;
    const uint64_t indices = ((uint64_t)d->width - 1u) * ((uint64_t)d->height - 1u) * 6u;
    const uint64_t meshes = (uint64_t)cx * cz;
    VKR_ASSERT(vertices <= UINT32_MAX && indices <= UINT32_MAX && meshes <= UINT32_MAX &&
                 vertices <= SIZE_MAX / sizeof(Vertex) && indices <= SIZE_MAX / sizeof(uint32_t) && meshes <= SIZE_MAX / sizeof(Mesh),
                 "vkmin_heightfield: grid exceeds index or allocation limits");
    return (vkmin_heightfield_size){.vertices = (uint32_t)vertices, .indices = (uint32_t)indices, .meshes = (uint32_t)meshes};
}

/* One shared vertex grid (x along width, z along height, y up); each chunk
 * owns a contiguous index range and a bounding sphere, so a chunk is a Mesh
 * the cull rejects like any other. Normals by central differences. */
void vkmin_heightfield(const vkmin_heightfield_desc *d, Vertex *verts, uint32_t *indices, Mesh *meshes) {
    const vkmin_heightfield_size n = vkmin_heightfield_sizes(d);
    VKR_ASSERT(verts && indices && meshes, "vkmin_heightfield: null output");
    const uint32_t w = (uint32_t)d->width, h = (uint32_t)d->height;
    const float cell = d->cell > 0.0f ? d->cell : 1.0f, uvs = d->uv_per_unit > 0.0f ? d->uv_per_unit : 1.0f;
    const uint32_t chunk = d->chunk > 0 ? (uint32_t)d->chunk : 32u;
    for (uint32_t z = 0; z < h; ++z) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t xl = x > 0 ? x - 1 : 0, xr = x + 1 < w ? x + 1 : w - 1;
            const uint32_t zd = z > 0 ? z - 1 : 0, zu = z + 1 < h ? z + 1 : h - 1;
            const float dx = (d->heights[z * w + xr] - d->heights[z * w + xl]) / ((float)(xr - xl) * cell);
            const float dz = (d->heights[zu * w + x] - d->heights[zd * w + x]) / ((float)(zu - zd) * cell);
            const float len = sqrtf(dx * dx + 1.0f + dz * dz);
            const float px = (float)x * cell, pz = (float)z * cell;
            VKR_ASSERT(isfinite(d->heights[z * w + x]) && isfinite(len) && isfinite(px) && isfinite(pz),
                         "vkmin_heightfield: non-finite sample or extent");
            verts[z * w + x] = (Vertex){.px = px, .py = d->heights[z * w + x], .pz = pz,
                                        .normal = oct_encode(-dx / len, 1.0f / len, -dz / len),
                                        .tangent = pack_tangent(1.0f, 0.0f, 0.0f, 1.0f), .uv = pack_half2(px * uvs, pz * uvs)};
        }
    }
    const uint32_t cx = (w - 1u + chunk - 1u) / chunk;
    uint32_t at = 0;
    for (uint32_t m = 0; m < n.meshes; ++m) {
        const uint32_t x0 = (m % cx) * chunk, z0 = (m / cx) * chunk;
        const uint32_t x1 = x0 + chunk < w - 1u ? x0 + chunk : w - 1u, z1 = z0 + chunk < h - 1u ? z0 + chunk : h - 1u;
        float ymin = INFINITY, ymax = -INFINITY;
        const uint32_t first = at;
        for (uint32_t z = z0; z < z1; ++z) {
            for (uint32_t x = x0; x < x1; ++x) {
                const uint32_t a = z * w + x, b = a + 1, c2 = a + w, e = c2 + 1;
                /* counter-clockwise seen from above (+y) */
                indices[at++] = a; indices[at++] = c2; indices[at++] = b;
                indices[at++] = b; indices[at++] = c2; indices[at++] = e;
            }
        }
        for (uint32_t z = z0; z <= z1; ++z) {
            for (uint32_t x = x0; x <= x1; ++x) {
                ymin = fminf(ymin, d->heights[z * w + x]);
                ymax = fmaxf(ymax, d->heights[z * w + x]);
            }
        }
        const float ex = (float)(x1 - x0) * cell * 0.5f, ez = (float)(z1 - z0) * cell * 0.5f, ey = (ymax - ymin) * 0.5f;
        meshes[m] = (Mesh){.first_index = first, .index_count = at - first, .vertex_offset = 0, .skin_offset = VKMIN_NONE,
                           .bounds = {(float)x0 * cell + ex, ymin + ey, (float)z0 * cell + ez, sqrtf(ex * ex + ey * ey + ez * ez)}};
    }
    VKR_ASSERT(at == n.indices, "heightfield index count mismatch");
}

/* Four levels, fixed fanout, no allocator. The normal/flow calculation uses
 * one-sided differences at edges: a tilted plane stays tilted at the border. */
static vec4 terrain_derivative(const vkmin_heightfield_desc *d, uint32_t x, uint32_t z) {
    const uint32_t w = (uint32_t)d->width, h = (uint32_t)d->height;
    const uint32_t xl = x ? x-1 : x, xr = x+1 < w ? x+1 : x;
    const uint32_t zl = z ? z-1 : z, zr = z+1 < h ? z+1 : z;
    const float cell = d->cell > 0 ? d->cell : 1;
    const float dx = (d->heights[z*w+xr] - d->heights[z*w+xl]) / ((float)(xr-xl)*cell);
    const float dz = (d->heights[zr*w+x] - d->heights[zl*w+x]) / ((float)(zr-zl)*cell);
    VKR_ASSERT(isfinite(dx) && isfinite(dz) && isfinite(d->heights[z*w+x]), "terrain: non-finite height or derivative");
    return (vec4){dx, 0, dz, hypotf(dx, dz)};
}

void vkmin_terrain_normal(const vkmin_heightfield_desc *d, vec4 *out, size_t capacity) {
    const vkmin_heightfield_size size = vkmin_heightfield_sizes(d);
    VKR_ASSERT(out && capacity >= size.vertices, "terrain normal: output too small");
    for (uint32_t z = 0; z < (uint32_t)d->height; ++z) for (uint32_t x = 0; x < (uint32_t)d->width; ++x) {
        const vec4 a = terrain_derivative(d, x, z);
        const float inv = 1.0f / hypotf(a.w, 1.0f);
        out[z*(uint32_t)d->width+x] = (vec4){-a.x*inv, inv, -a.z*inv, 0};
    }
}

void vkmin_terrain_flow(const vkmin_heightfield_desc *d, vec4 *out, size_t capacity) {
    const vkmin_heightfield_size size = vkmin_heightfield_sizes(d);
    VKR_ASSERT(out && capacity >= size.vertices, "terrain flow: output too small");
    for (uint32_t z = 0; z < (uint32_t)d->height; ++z) for (uint32_t x = 0; x < (uint32_t)d->width; ++x) {
        const vec4 a = terrain_derivative(d, x, z);
        const float inv = a.w > 0 ? 1.0f/a.w : 0;
        out[z*(uint32_t)d->width+x] = (vec4){-a.x*inv, 0, -a.z*inv, a.w};
    }
}

vkmin_heightfield_size vkmin_terrain_sizes(const vkmin_terrain_desc *d) {
    VKR_ASSERT(d, "terrain: null desc");
    (void)vkmin_heightfield_sizes(&d->heightfield);
    const uint64_t n = d->heightfield.chunk ? (uint32_t)d->heightfield.chunk : 32u;
    VKR_ASSERT(d->heightfield.width == d->heightfield.height && (uint64_t)d->heightfield.width == 8*n+1 &&
                 isfinite(d->skirt) && d->skirt >= 0, "terrain: expected square 8*chunk+1 samples and nonnegative skirt");
    const uint64_t vn = 85*((n+1)*(n+1)+4*(n+1)), in = 85*(6*n*n+24*n);
    VKR_ASSERT(vn <= UINT32_MAX && in <= UINT32_MAX && vn <= SIZE_MAX/sizeof(Vertex) && in <= SIZE_MAX/sizeof(uint32_t),
                 "terrain: output overflow");
    return (vkmin_heightfield_size){(uint32_t)vn, (uint32_t)in, 85};
}

void vkmin_terrain(const vkmin_terrain_desc *d, Vertex *v, uint32_t *idx, Mesh *mesh) {
    const vkmin_heightfield_size size = vkmin_terrain_sizes(d);
    VKR_ASSERT(v && idx && mesh, "terrain: null output");
    const vkmin_heightfield_desc *h = &d->heightfield;
    const uint32_t n = h->chunk ? (uint32_t)h->chunk : 32u, row = n+1;
    const uint32_t per = row*row+4*row, w = (uint32_t)h->width;
    const float cell = h->cell > 0 ? h->cell : 1, skirt = d->skirt > 0 ? d->skirt : cell*8;
    uint32_t ox[85] = {0}, oz[85] = {0}, step[85] = {8}, at = 0;
    for (uint32_t m = 0; m < 85; ++m) {
        if (m) {
            const uint32_t parent = (m-1)/4, corner = (m-1)%4;
            step[m] = step[parent]/2;
            ox[m] = ox[parent] + (corner&1u)*n*step[m];
            oz[m] = oz[parent] + (corner>>1u)*n*step[m];
        }
        const uint32_t base = m*per, first = at;
        float lo = INFINITY, hi = -INFINITY;
        for (uint32_t z = oz[m]; z <= oz[m]+n*step[m]; ++z) for (uint32_t x = ox[m]; x <= ox[m]+n*step[m]; ++x) {
            VKR_ASSERT(isfinite(h->heights[z*w+x]), "terrain: non-finite height");
            lo = fminf(lo, h->heights[z*w+x]); hi = fmaxf(hi, h->heights[z*w+x]);
        }
        for (uint32_t z = 0; z <= n; ++z) for (uint32_t x = 0; x <= n; ++x) {
            const uint32_t sx = ox[m]+x*step[m], sz = oz[m]+z*step[m];
            const vec4 a = terrain_derivative(h, sx, sz);
            v[base+z*row+x] = (Vertex){.px = (float)sx*cell, .py = h->heights[sz*w+sx], .pz = (float)sz*cell,
                .normal = oct_encode(-a.x, 1, -a.z), .tangent = pack_tangent(1, a.x, 0, 1),
                .uv = pack_half2((float)sx*cell, (float)sz*cell)};
            if (x < n && z < n) {
                const uint32_t a0 = base+z*row+x, q[6] = {a0, a0+row, a0+1, a0+1, a0+row, a0+row+1};
                for (uint32_t k = 0; k < 6; ++k) idx[at++] = q[k];
            }
        }
        /* Edge order follows the surface boundary; skirts face outward. */
        for (uint32_t edge = 0; edge < 4; ++edge) for (uint32_t k = 0; k <= n; ++k) {
            const uint32_t top = edge == 0 ? k : edge == 1 ? k*row+n : edge == 2 ? n*row+n-k : (n-k)*row;
            const uint32_t bottom = base+row*row+edge*row+k;
            v[bottom] = v[base+top]; v[bottom].py -= skirt;
            if (k < n) {
                const uint32_t next = edge == 0 ? top+1 : edge == 1 ? top+row : edge == 2 ? top-1 : top-row;
                const uint32_t q[6] = {base+top, base+next, bottom, bottom, base+next, bottom+1};
                for (uint32_t j = 0; j < 6; ++j) idx[at++] = q[j];
            }
        }
        lo -= skirt;
        VKR_ASSERT(isfinite(lo) && isfinite(hi-lo), "terrain: height/skirt range overflow");
        const float half = (float)(n*step[m])*cell*0.5f, ey = (hi-lo)*0.5f;
        VKR_ASSERT(isfinite(half) && isfinite((float)(w-1)*cell), "terrain: extent overflow");
        mesh[m] = (Mesh){.first_index = first, .index_count = at-first, .skin_offset = VKMIN_NONE,
            .bounds = {(float)ox[m]*cell+half, lo+ey, (float)oz[m]*cell+half, hypotf(hypotf(half,half),ey)}};
    }
    VKR_ASSERT(at == size.indices, "terrain: internal index count");
}

uint32_t vkmin_terrain_select(const Mesh *nodes, vec4 camera, float lod_scale, bool one_level, uint32_t *out, size_t capacity) {
    VKR_ASSERT(nodes && out && capacity >= 64 && isfinite(lod_scale) && lod_scale >= 0 &&
                 isfinite(camera.x) && isfinite(camera.y) && isfinite(camera.z), "terrain select: invalid argument");
    if (one_level) { for (uint32_t i = 0; i < 64; ++i) out[i] = 21+i; return 64; }
    uint32_t stack[85] = {0}, todo = 1, count = 0;
    const float scale = lod_scale > 0 ? lod_scale : 2;
    while (todo) {
        const uint32_t m = stack[--todo];
        const vec4 b = nodes[m].bounds;
        const float dist = hypotf(hypotf(camera.x-b.x, camera.z-b.z), camera.y-b.y);
        if (m < 21 && dist < b.w*scale) {
            for (uint32_t k = 4; k > 0; --k) stack[todo++] = 4*m+k;
        } else out[count++] = m;
    }
    return count;
}

vec4 vkmin_sun_direction(float time_of_day) {
    VKR_ASSERT(isfinite(time_of_day), "sun: non-finite time of day");
    const float angle = (fmodf(time_of_day,24.0f)-6.0f)/12.0f*3.14159265359f;
    const float inv = 1.0f/sqrtf(1.09f);
    return (vec4){cosf(angle)*inv,sinf(angle)*inv,0.3f*inv,0};
}

vec2 vkmin_taa_jitter(uint32_t frame) {
    float jitter[2] = {0};
    for (uint32_t dim = 0; dim < 2; ++dim) {
        uint32_t index = frame%16u+1u;
        const uint32_t radix = dim == 0 ? 2u : 3u;
        float digit = 1;
        while (index) { digit /= (float)radix; jitter[dim] += digit*(float)(index%radix); index /= radix; }
    }
    return (vec2){jitter[0]-0.5f,jitter[1]-0.5f};
}
