/* The scene's C/GLSL interface. All animation is driven by the 60 Hz tick. */
#ifndef OMEGA_SHARED_H
#define OMEGA_SHARED_H
#include "shared.h"
#define OMEGA_GATE_MOUTH_Z 4.5f
#define OMEGA_GATE_PYLON_Z 6.0f
#define OMEGA_GATE_MOUTH_RADIUS 12.6f
#define OMEGA_GATE_ENTRANCE_Z 1200.0f
#define OMEGA_GATE_CLOSE_START 17.5f
#define OMEGA_GATE_CLOSE_END 19.5f
VKMIN_STRUCT(OmegaVertex) { vec4 position; vec4 normal; vec4 color; };
VKMIN_STRUCT(OmegaPush) {
    mat4 vp;
    ADDR vertices;
    vec4 eye;
    vec4 scene; /* seconds, aspect, ship translation, gate aperture */
    U32 texture_id;
    U32 bloom_id;
    F32 flash;
    F32 padding;
    U32 gate_id;
    U32 reserved;
};
#ifndef VKMIN_GLSL
_Static_assert(sizeof(OmegaVertex)==48, "omega vertex layout");
_Static_assert(sizeof(OmegaPush)==128, "omega push layout");
#endif
#endif
