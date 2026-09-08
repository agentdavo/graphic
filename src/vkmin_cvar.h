/* vkmin_cvar.h -- the flat table of tunables. One X-macro declares the enum, the
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
    X(r_msaa, 1.0f, "target default: requested sample count 1/2/4/8/16/32/64")          \
    X(r_msaa_single, 0.0f, "target default: prefer EXT render-to-single-sampled when available") \
    X(r_alpha_to_coverage, 0.0f, "Omega startup: alpha-to-coverage on geometry")       \
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
    X(r_cascade_blend, 0.0f, "cross-fade this fraction of each cascade into the next; 0 hard splits") \
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
    X(d_frame_step, 1.0f, "frames advanced per rendered frame (0 pauses)")           \
    X(r_arena_mb, 256.0f, "device buffer arena reserved at init, MB")                \
    X(r_image_arena_mb, 256.0f, "device image arena reserved at init, MB")           \
    X(r_ring_mb, 64.0f, "host-visible ring for uploads and per-frame data, MB")      \
    X(r_default_depth, 1.0f, "allocate the default pass depth buffer; 0 for programs that own their depth") \
    X(r_hdr_packed, 0.0f, "omega HDR targets: 1 R11G11B10 (half the bytes), 0 RGBA16F reference") \
    X(r_omega_shadow, 2048.0f, "omega key shadow map size in texels")

typedef enum {
#define VKMIN_CVAR_ENUM(name, def, help) CV_##name,
    VKMIN_CVAR_LIST(VKMIN_CVAR_ENUM)
#undef VKMIN_CVAR_ENUM
    CV_COUNT
} cvar_id;

typedef struct cvar_state {
    float values[CV_COUNT];
    bool assigned[CV_COUNT];
    bool locked; /* init-only settings cannot change after context creation */
} cvar_state;

/* Initialize before use. Edit through these functions; the fields are public so
 * a whole state can be copied, not so validation can be bypassed. Out-of-range
 * values and edits to an init-only setting after `locked` abort rather than
 * being clamped: a tunable that quietly took a different value than was asked
 * for would make a measurement lie.
 *
 * cvar_set supplies a program or profile default and does NOT mark the setting
 * as assigned; only parsing does, which is why cvar_was_set means "the user
 * said so on the command line" -- including an explicit zero, which
 * cvar_is_overridden (a comparison against the table default) cannot see. */
void cvar_init(cvar_state *state);
float cvar_get(const cvar_state *state, cvar_id id);
bool cvar_get_bool(const cvar_state *state, cvar_id id);
int cvar_get_int(const cvar_state *state, cvar_id id);
void cvar_set(cvar_state *state, cvar_id id, float value);
bool cvar_is_overridden(const cvar_state *state, cvar_id id);
bool cvar_was_set(const cvar_state *state, cvar_id id);
const char *cvar_name(cvar_id id);

/* Parses one "name=value" string. The "+name value" spelling accepted on a
 * command line is joined into that form by the caller (parse_command_line in
 * vkmin.c), so there is one parser and not two. Returns false, having printed
 * why, on an unknown name, an unparsable or out-of-range value, or an edit to
 * an init-only setting once the state is locked. */
bool cvar_parse_assignment(cvar_state *state, const char *text);
void cvar_print_all(const cvar_state *state);
/* Writes "name=value name=value" for changed or explicitly assigned values.
 * Returns the length the text would have had, snprintf-style, so a result >= cap
 * means buf holds a truncated line. */
int cvar_format_overrides(const cvar_state *state, char *buf, int cap);

#endif
