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
#include "omega_mounts.h"
/* About nine kilometres between centres at the authored 1.7 km hull scale. */
#define OMEGA_BATTLE_SEPARATION 190.0f
/* Two hull floodlights over the forward identification panel, which the
 * Blender source puts at x +-2.24, y 0.56..1.00, z -19.31..-16.49. They are
 * the only practical lighting the ship carries: a soft warm pool on the name
 * and nothing else. */
/* A gas giant off the port bow. It is the only motivated light the demo has
 * besides the gate and the guns, and it earns its cost twice: omega_gate.frag
 * draws the body into the backdrop, and omega_hull.frag takes a broad cool
 * fill from the same direction. It sits to starboard, opposite the warm key,
 * because a split is the point; and off the quarter the launch and fly-by
 * cameras actually look, or it would never be in frame. That cold side against the warm key is most
 * of what separates a lit model from a photographed one. */
#define OMEGA_PLANET_X 2600.0f
#define OMEGA_PLANET_Y 450.0f
#define OMEGA_PLANET_Z 700.0f
#define OMEGA_PLANET_RADIUS 1300.0f
#define OMEGA_PLANET_FILL 0.58f
#define OMEGA_FLOOD_X 1.72f
#define OMEGA_FLOOD_Y 2.25f
#define OMEGA_FLOOD_Z (-17.90f)
#define OMEGA_FLOOD_LEVEL 0.62f
/* Embers thrown off the armour where the bow lasers land. Each is a streak
 * drawn along its own velocity; the index is baked into the vertex's local z,
 * so the spray needs no per-particle upload and no extra scene field. */
#define OMEGA_EMBER_COUNT 960
#define OMEGA_EMBER_LIFE 1.6f
#define OMEGA_EMBER_SIZE 0.034f
/* Vacuum carries nothing. The only thing that reaches a listener out here is
 * conducted: an impact rings the structure it lands in, and an observer near
 * enough feels it. Past this range in model units there is nothing to feel. */
/* The habitat drum, measured off the authored hull. It is a solid body of
 * revolution amidships that stands well proud of the rest of the ship, so it
 * masks any mount behind it firing forward -- and because it is a cylinder,
 * rotating it does not open a lane. Eight of the twelve turret mounts are
 * masked toward anything ahead; only the four at z = -13.55, forward of the
 * drum, have a line. The batteries fire from those. */
#define OMEGA_DRUM_FORE (-10.96f)
#define OMEGA_DRUM_AFT (-2.82f)
#define OMEGA_DRUM_RADIUS 5.72f
#define OMEGA_BATTERY_A 6u              /* top, forward of the drum */
#define OMEGA_BATTERY_B 0u              /* belly, forward of the drum */
#define OMEGA_FELT_RANGE 260.0f
/* A fighter closer than this gets its own pass. Beyond it there are seventy-two
 * of them and the mix would be soup. */
#define OMEGA_PASS_RANGE 34.0f
/* The hero's fighters shoot on the way in. An Aurora carries four linked
 * Copeland JC466/A pulse discharge cannons under the cockpit that throw round
 * blobs of plasma rather than a beam, so these are billboards and not tubes.
 * Bolts are pooled rather than owned: the index picks which fighter fired and
 * which shot of its burst, so nothing is stored per bolt between frames and
 * the whole thing stays a function of the tick. */
#define OMEGA_BOLT_SHOTS 14
#define OMEGA_BOLT_COUNT 336            /* OMEGA_FURY_HERO * OMEGA_BOLT_SHOTS */
#define OMEGA_BOLT_LIFE 0.90f
#define OMEGA_BOLT_SPEED 52.0f          /* model units a second */
#define OMEGA_BOLT_SIZE 0.085f
#define OMEGA_BOLT_MUZZLE_Y (-0.022f)
#define OMEGA_BOLT_MUZZLE_Z (-0.116f)
/* The hero's wing presses home from here, each fighter on a repeating cycle,
 * staggered so the wing arrives as a stream rather than a wall. */
#define OMEGA_FURY_ATTACK 30.5f
#define OMEGA_FURY_CYCLE 13.0f
#define OMEGA_FURY_STAGGER 0.42f
/* Engine blocks sit at the stern in hull space; the fragment shader lights
 * from the same place. */
#define OMEGA_ENGINE_Z 16.0f
/* Battle damage. A breach is not a muzzle flash: once open it stays open, so
 * each site is a hull-space point and the second it was made. The points are
 * the ray-cast contacts the exporter baked, which is the whole reason a breach
 * lands on plating something actually hit rather than where it looked good.
 * Six on the hero, four on whoever she is shooting.

 * Scale is the point: a model unit is 39.95 m, so the burn radii below are
 * tens of metres to a couple of hundred. On a 1.7 km hull a fire the size of
 * a house would be invisible, and one the size of the ship would be a kill. */
#define OMEGA_DAMAGE_SITES 10
#define OMEGA_SEQUENCE_TICKS 3600
#define OMEGA_PARTICLE_START 810
#define OMEGA_PARTICLE_END 3390
#define OMEGA_PARTICLE_PERIOD 108
#define OMEGA_PARTICLE_STAGGER 42
#define OMEGA_PARTICLE_SHOT_SPACING 11
#define OMEGA_PARTICLE_FLIGHT 30
#define OMEGA_FORMATION_COS 0.6967067f
#define OMEGA_FORMATION_SIN 0.7173561f
/* Fighter wings. Sixteen Starfuries come through the gate in formation
 * with the hero and hold station on it; eight more scramble from the
 * forward bay a second after the bow is clear of the mouth, a pair every
 * second. Each opponent arrived with a full wing already deployed.
 * One pose per fighter is computed on the CPU each tick and indexed by
 * part code, exactly as the turrets are. The counts are literals so both
 * toolchains can size an array with them; the asserts below keep them
 * honest. */
#define OMEGA_FURY_WING 16
#define OMEGA_FURY_LAUNCH 8
#define OMEGA_FURY_HERO 24
#define OMEGA_FURY_TOTAL 72
#define OMEGA_FURY_PART 128            /* part codes 128..199 */
#define OMEGA_FURY_LAUNCH_START 11.4f  /* the bow passes the mouth at 10.4 */
#define OMEGA_FURY_LAUNCH_INTERVAL 1.0f
#define OMEGA_FURY_LAUNCH_FLIGHT 5.2f
/* An Omega does not catapult its fighters out of the bow. The launch hatches
 * are on the rim of the rotating centrifuge, at either end of it, and the
 * Starfuries are thrown out radially on the drum's own spin -- the same trick
 * as the station's Cobra Bays. The forward end of the drum measures z -10.96
 * in the authored hull, rim radius 5.7, so the hatches sit just inside that.
 * A pair leaves from opposite sides at once, which is also the only way to
 * launch off a spinning drum without unbalancing it. */
#define OMEGA_FURY_BAY_R 6.05f          /* just clear of the 5.72 rim */
#define OMEGA_FURY_BAY_Z (-10.2f)       /* forward end of the rotating section */
#define OMEGA_FURY_BAY_RUN 34.0f        /* radial throw */
#define OMEGA_FURY_BAY_SPIN 12.0f       /* the rim's own tangential velocity */
#define OMEGA_FURY_BAY_EASE 30.0f       /* how hard the curve straightens onto station */
/* The centrifuge's rotation, read by omega.vert to turn the drum and by
 * omega_weapons.h to find where a hatch was pointing when a fighter left it. */
#define OMEGA_HABITAT_RATE 0.17f
#define OMEGA_HABITAT_PHASE 0.23f
#define OMEGA_HABITAT_STAGGER 1.1f
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
 * clamp. Codes are small integers: material 0..8, part 0..127. */
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
    F32 reserved[2]; /* [0] selects the period tone curve; see omega_post.frag */
    vec4 blur;  /* xy: hull motion this frame in UV; z: shutter fraction */
    mat4 turrets[96]; /* four ships, twelve mounts, yaw housing + pitched barrels */
    vec4 beam_hit[2];
    vec4 pulse_start[18]; /* xyz birth muzzle, w age in ticks */
    vec4 pulse_end[18];
    mat4 fury[OMEGA_FURY_TOTAL]; /* one world pose per Starfury */
    vec4 damage[OMEGA_DAMAGE_SITES]; /* xyz hull-space breach, w the second it opened */
    vec4 bolt[OMEGA_BOLT_COUNT];     /* xyz the muzzle it left, w the second it left */
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
_Static_assert(sizeof(OmegaScene)==17024, "omega scene layout");
_Static_assert(OMEGA_FURY_HERO==OMEGA_FURY_WING+OMEGA_FURY_LAUNCH, "omega hero wing");
_Static_assert(OMEGA_FURY_TOTAL==OMEGA_FURY_HERO+3*OMEGA_FURY_WING, "omega fury total");
_Static_assert(sizeof(OmegaPush)==32, "omega push layout");
#endif
#endif
