/* The scene's C/GLSL interface. All animation is driven by the 60 Hz tick. */
#ifndef OMEGA_SHARED_H
#define OMEGA_SHARED_H
#include "shared.h"
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
