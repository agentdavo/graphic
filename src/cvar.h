/* cvar.h -- the flat table of tunables. One X-macro declares the enum, the
 * default and the help text together, so a cvar cannot exist in one place and
 * not the other. Per-frame code reads by enum index; strings are touched only
 * when parsing the command line or a runtime `set` command.
 *
 * Every switch between a fast path and its reference implementation lives here.
 * That is the whole point: flipping one of these is cheaper than a rebuild, so
 * the honest comparison actually gets made.
 */
#ifndef VKMIN_CVAR_H
#define VKMIN_CVAR_H

#include <stdbool.h>

#define VKMIN_CVAR_LIST(X)                                                           \
    X(r_host_layouts, 1.0f, "1 supported host image transitions, 0 GPU transition reference") \
    X(r_grass_patch, 1.0f, "valley grass: 1 48-blade patches, 0 single-blade reference") \
    X(taa, 0.0f, "temporal AA; --frame N warms up 0..N when enabled")                \
    X(bloom, 0.0f, "HDR bloom strength; fixed exposure via r_exposure")                 \
    X(r_width, 1280.0f, "render width in pixels (headless or window)")               \
    X(r_height, 720.0f, "render height in pixels")                                   \
    X(r_exposure, 1.0f, "tonemap exposure multiplier")                               \
    X(r_tonemap, 1.0f, "0: clamp, 1: ACES fit, 2: Reinhard")                          \
    X(r_shadows, 1.0f, "render and sample shadow maps")                              \
    X(r_shadow_atlas, 4096.0f, "shadow atlas size in texels")                        \
    X(r_cascades, 4.0f, "sun cascade count, 1..4")                                   \
    X(r_cascade_lambda, 0.6f, "log/uniform split blend, 0 uniform .. 1 logarithmic") \
    X(r_shadow_lights, 8.0f, "max local lights given shadow tiles per frame")        \
    X(r_shadow_bias, 1.5f, "depth bias in atlas texels")                             \
    X(r_normal_bias, 1.0f, "normal offset bias in atlas texels")                     \
    X(r_normal_maps, 1.0f, "sample normal maps")                                     \
    X(r_clustered, 1.0f, "1: clustered light lists, 0: brute force every light")     \
    X(r_prepass, 1.0f, "depth prepass before the forward pass")                      \
    X(r_gpu_cull, 1.0f, "1: compute culling, 0: CPU-written draw list (reference)")  \
    X(r_cull_compact, 1.0f, "1: atomic append (fast), 0: stable slot per instance")   \
    X(r_shadow_distance, 40.0f, "far end of the sun cascades, world units")           \
    X(r_transparent, 1.0f, "render the sorted transparent pass")                     \
    X(r_overlay, 1.0f, "draw the stats overlay")                                     \
    X(r_debug, 0.0f, "0 off 1 normals 2 clusters 3 cascades 4 overdraw 5 albedo 6 atlas 7 ids") \
    X(r_outline, 1.0f, "post: multiply the look's outline strength (0 disables the pass's effect)") \
    X(r_lut, 1.0f, "post: multiply the look's LUT strength")                            \
    X(r_quads, 1.0f, "draw the quad batcher's sprites, particles and UI")               \
    X(d_check_cull, 0.0f, "compare the GPU draw list with the CPU reference each frame; counts mismatches") \
    X(r_freeze_cull, 0.0f, "cull against the frozen camera, render from the live one") \
    X(r_max_lights, 256.0f, "cap on lights uploaded this frame")                     \
    X(r_ambient, 0.10f, "ambient radiance")                                          \
    X(r_sun_intensity, 5.0f, "sun radiance multiplier")                              \
    X(r_sync_naive, 0.0f, "one frame in flight and wait idle per submit")            \
    X(r_readback, 1.0f, "copy the backbuffer for PNG capture every frame")           \
    X(r_path, 0.0f, "API path: 0 auto, 1 legacy (staging, modules), 2 modern (host copy, inline SPIR-V)") \
    X(r_vsync, 1.0f, "FIFO present mode when 1, immediate/mailbox when 0")           \
    X(r_hotreload, 0.0f, "rebuild pipelines whose SPIR-V files changed, checked once per frame") \
    X(d_frame_step, 1.0f, "frames advanced per rendered frame (0 pauses)")

typedef enum {
#define VKMIN_CVAR_ENUM(name, def, help) CV_##name,
    VKMIN_CVAR_LIST(VKMIN_CVAR_ENUM)
#undef VKMIN_CVAR_ENUM
    CV_COUNT
} cvar_id;

float cvar_get(cvar_id id);
bool cvar_get_bool(cvar_id id);
int cvar_get_int(cvar_id id);
void cvar_set(cvar_id id, float value);
bool cvar_is_overridden(cvar_id id); /* value differs from the default */
bool cvar_was_set(cvar_id id);       /* explicitly assigned, even to the default value */
const char *cvar_name(cvar_id id);

/* Parses "name=value" or "+name value" forms. Returns false on an unknown
 * name or unparsable value, having printed why. */
bool cvar_parse_assignment(const char *text);
void cvar_print_all(void);
/* Writes "name=value name=value" for every overridden cvar into buf. */
int cvar_format_overrides(char *buf, int cap);

#endif
