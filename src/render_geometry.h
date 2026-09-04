/* Pure geometry generation for render. Caller owns every output array.
 * Existing vkmin_ names are retained for source compatibility. */
#ifndef RENDER_GEOMETRY_H
#define RENDER_GEOMETRY_H
#include <stdbool.h>
#include <stddef.h>
#include "render_shared.h"

typedef struct {                  /* heights in, one chunked grid mesh out; sizes from _sizes */
    const float *heights; int width, height;   /* samples across and down; heights in world units */
    float cell;                   /* world units between samples; 0 = 1 */
    int chunk;                    /* cells per chunk side, each chunk one Mesh; 0 = 32 */
    float uv_per_unit;            /* texture repeats per world unit; 0 = 1 */
} vkmin_heightfield_desc;
typedef struct { uint32_t vertices, indices, meshes; } vkmin_heightfield_size;

vkmin_heightfield_size vkmin_heightfield_sizes(const vkmin_heightfield_desc *);            // pure
/* Output arrays must hold the counts from sizes(); zero scale/chunk selects defaults. */
void vkmin_heightfield(const vkmin_heightfield_desc *, Vertex *, uint32_t *, Mesh *);      // writes outputs

/* Outdoor primitives. No allocation, clock or hidden state. Maps are world
 * normals (xyz) and downhill unit directions (xz, w = slope); flats return
 * zero flow. Output capacity is width*height vec4s; source is never modified. */
void vkmin_terrain_normal(const vkmin_heightfield_desc *, vec4 *out, size_t capacity); // writes outputs
void vkmin_terrain_flow(const vkmin_heightfield_desc *, vec4 *out, size_t capacity);   // writes outputs
/* Four-level square quadtree, breadth-first: root, 4 children, 16, 64.
 * width == height == 8*chunk+1; each node has chunk*chunk cells and skirts.
 * Geometry is world space. All arrays use terrain_sizes counts. */
typedef struct { vkmin_heightfield_desc heightfield; float skirt; } vkmin_terrain_desc;
vkmin_heightfield_size vkmin_terrain_sizes(const vkmin_terrain_desc *); // pure
void vkmin_terrain(const vkmin_terrain_desc *, Vertex *, uint32_t *, Mesh *); // writes outputs
/* Writes a disjoint covering of the quadtree as mesh indices, capacity >=64.
 * lod_scale is distance / chunk width (default 2); one_level selects all 64 finest nodes. */
uint32_t vkmin_terrain_select(const Mesh *nodes, vec4 camera, float lod_scale, bool one_level,
                             uint32_t *out, size_t capacity); // writes outputs

/* Unit sun direction, +Y up: hours since midnight, sunrise 6, sunset 18.
 * The orbital approximation is fixed; a game's latitude model can replace it. */
vec4 vkmin_sun_direction(float time_of_day); // pure
vec2 vkmin_taa_jitter(uint32_t frame);       // pure; centred 16-sample Halton(2,3), in pixels

#endif
