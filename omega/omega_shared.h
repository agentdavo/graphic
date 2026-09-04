/* The scene's C/GLSL interface. All animation is driven by the 60 Hz tick. */
#ifndef OMEGA_SHARED_H
#define OMEGA_SHARED_H
#include "render_shared.h"
#define OMEGA_GATE_MOUTH_Z 1.0f
/* Pylons straddle the mouth, which sits about 12% of the way along them
 * from the camera-side caps, so most of each pylon reaches back into the energy. They
 * splay outward toward the camera by OMEGA_PYLON_SPLAY radians. */
#define OMEGA_GATE_PYLON_Z 38.0f
#define OMEGA_GATE_PYLON_BACK_Z -4.0f
#define OMEGA_GATE_PYLON_RADIAL 15.8f
#define OMEGA_PYLON_SPLAY 0.07f
#define OMEGA_PYLON_STATIONS 7
#define OMEGA_PYLON_STATION_SPACING 6.0f
/* The visible vortex is one straight cone from the mouth to a narrow throat.
 * The ship's approach route is far deeper; inside the gate it is only ever
 * seen through the mouth disc, so the cone need not contain it. */
#define OMEGA_GATE_MOUTH_RADIUS 14.0f
#define OMEGA_GATE_THROAT_Z 75.0f
#define OMEGA_GATE_THROAT_RADIUS 1.5f
#define OMEGA_GATE_ENTRANCE_Z 1200.0f
/* Bow cannon muzzles in hull space; the beams and their lights start here. */
#define OMEGA_MUZZLE_Z -20.2f
#define OMEGA_MUZZLE_X 0.9f
/* About nine kilometres between centres at the authored 1.7 km hull scale. */
#define OMEGA_BATTLE_SEPARATION 190.0f
#define OMEGA_SEQUENCE_TICKS 1800
#define OMEGA_PARTICLE_START 810
#define OMEGA_PARTICLE_END 1620
#define OMEGA_PARTICLE_PERIOD 108
#define OMEGA_PARTICLE_STAGGER 42
#define OMEGA_PARTICLE_SHOT_SPACING 11
#define OMEGA_PARTICLE_FLIGHT 30
#define OMEGA_FORMATION_COS 0.6967067f
#define OMEGA_FORMATION_SIN 0.7173561f
/* Engine centers after the official-book proportions pass. */
#define OMEGA_ENGINE_X 1.804f
#define OMEGA_ENGINE_Y 1.628f
#define OMEGA_GATE_CLOSE_START 17.5f
#define OMEGA_GATE_CLOSE_END 19.5f
/* 24 bytes, down from three vec4s. The mesh is the largest thing this program
 * puts on the device, and three quarters of those 48 bytes were padding, a
 * normal stored to a precision the authored model never had (it ships as
 * int16), and an alpha channel no shader ever read.
 *   normal          octahedral, two snorm16      -- omegaOctDecode
 *   color_rg        half r, half g
 *   color_b_codes   low half: b. high 16: material | part<<8
 * Colour is half float, not unorm8, because the pylon window is 1.6 and would
 * clamp. Codes are small integers: material 0..8, part 0..27. */
VKMIN_STRUCT(OmegaVertex) { F32 x, y, z; U32 normal, color_rg, color_b_codes; };
/* Per-frame state lives in a ring-allocated block addressed from the push
 * constants, so the push carries only what varies per draw: the pass, the
 * buffers and the images. Every field is a plain value with one meaning. */
VKMIN_STRUCT(OmegaScene) {
    mat4 vp;
    vec4 eye;   /* xyz camera position */
    vec4 scene; /* seconds, aspect, ship translation, gate aperture */
    F32 flash;  /* cannon muzzle level */
    U32 hull_texture;
    F32 reserved[2];
    vec4 blur;  /* xy: hull motion this frame in UV; z: shutter fraction */
};
VKMIN_STRUCT(OmegaPush) {
    ADDR vertices;
    ADDR frame; /* OmegaScene for this frame */
    U32 texture_id;
    U32 bloom_id;
    U32 gate_id;
    U32 pass;   /* OMEGA_PASS_* */
};
#define OMEGA_PASS_SCENE 0u
#define OMEGA_PASS_SHADOW 1u
#define OMEGA_PASS_BACKDROP 2u
#define OMEGA_PASS_BLOOM 3u
#define OMEGA_PASS_GRADE 4u
#ifndef VKMIN_GLSL
_Static_assert(sizeof(OmegaVertex)==24, "omega vertex layout");
_Static_assert(sizeof(OmegaScene)==128, "omega scene layout");
_Static_assert(sizeof(OmegaPush)==32, "omega push layout");
#endif
#endif
