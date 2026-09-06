# Omega reference review — 6 September 2026

## Footage inspected

Local `b5.mp4`: 0–10 seconds at half-second intervals. Four longitudinal finned
pylons ignite red-white, merge into an overexposed flash around 2–2.5 seconds,
then surround a soft blue funnel with a dark, off-centre throat. Fine irregular
filaments cover the funnel. Escort silhouettes become visible before the capital
ship; this demo retains its own 20-second cinematic timing.

Local `isn.mp4`: sampled from 7 seconds through the end (about 42 seconds), with
full-size inspection of the 9- and 17-second hull views. The clear identifiers
are the tall command bow, narrow axial hull, opposed rectangular habitat banks,
exposed diagonal webs, oxide-red service panels, and four separated engines.
The previous native model's cylindrical cage was the largest silhouette mismatch.

## Implemented

- Replaced the native ship with a 21,096-triangle Blender-authored model, retaining
  habitat animation, forward muzzle coordinates and four engine positions.
- Added packed UV armor maps, scuffed panel edges, variation, nameplates, turrets,
  antenna lights and external pipework. The native renderer samples the same
  armor pattern from a compact compiled atlas.
- Packaged Agamemnon and Alexander GLBs with embedded textures and animation.
  The demo uses Agamemnon; the two variants share the hull and differ in nameplates.
- Preserved the four-pylon gate; strengthened blue filaments and adjusted the
  opening view from about 9 to about 13 degrees off-axis to clarify the throat.
- Fixed motion blur using widely separated captured frames as if consecutive.
  Discontinuities now clear motion; consecutive frames have a smaller smear cap.

This is a reference-inspired improvement, not an exact reconstruction of the
original footage. The demo retains its own flyby, camera track, escorts and timing.

## Validation

Build and capture evidence is in `build/omega-review/`. The GLB checks verified
container lengths, UVs on textured primitives, six embedded texture images and
an animation in each variant. Native captures cover ignition, opening, approach,
emergence, firing and closing; Blender renders were inspected during iteration.

- Final UCRT GCC and Clang builds passed with warnings as errors; Clang's scene
  static analysis passed. All Omega SPIR-V modules passed `spirv-val`.
- A 1,205-frame live run completed through the 20-second sequence and restart.
  Capture and live logs reported no Vulkan validation errors.
- Frame 750 was pixel-identical between GCC and Clang on this Intel GPU.
  Isolated and sparse-batch captures of that frame were also pixel-identical.
- Full historical regression suites and other GPU devices were not tested.

## Close-up lighting and machinery refinement

The user's four supplied ISN stills guided a second pass. The native model now
contains 39,480 triangles (45,288 with the gate and escorts). Bow tiers, an
octagonal hangar rim, sensor housings, manifold bays, radiator fins, faceted
habitat end webs and armored nacelle service units replace large plain areas.
The two GLB variants were regenerated with six embedded albedo maps each.

The higher directional key and reduced ambient light leave machinery bays dark.
The shadow map follows the hull beyond the mouth, fixing loss of self-shadowing
once the ship leaves the gate's original shadow footprint. Gate lighting falls
off with distance, and blue engine spill comes from four local positions.
Opaque exhaust cones are omitted; luminous discs, bloom and rear-view optical
halos match the supplied exhaust appearance more closely. Blender includes local
exhaust lamps and a Fog Glow compositor. The sky's nebula is more restrained.

Evidence for this pass: `build/omega-detail/`. Native emergence, side, rear and
closure captures were inspected. The live executable completed 1,205 frames,
including the loop boundary, without reported Vulkan validation errors. All
Omega shader modules passed SPIR-V validation. Source scripts compile; the GLBs
passed container, embedded-image and animation presence checks. The historical
regression suite and other GPUs were not tested.

See `assets/omega/lighting-comparison.png` for the same native frame before and
after this refinement, and `assets/omega/demo-preview-rear.png` for the exhausts.

Final GCC/UCRT and Clang builds passed with warnings as errors and their configured
analysis. Rear-view frame 870 was pixel-identical between the two compilers.

## Silhouette revision from the Agrippa and stern stills

Rebuilt the bow as tall wedge cheeks with an open equipment face, octagonal
aperture, rounded sensor housings, paired lower intakes and extended antennae.
Widened the rotating habitat banks and replaced their rectangular end caps with
thick, double-lobed plates. Continuous armor rails enclose the spine's recessed
machinery. The stern uses an open angular cradle, separated cylindrical engines,
exposed red collars and divider plates between the upper and lower engines.
Reduced the square seam pattern in favor of subtly mottled metal.

The final native mesh contains 53,654 ship triangles and 59,462 total scene
triangles. Geometry export has no zero-length normals. Agamemnon and Alexander
GLBs were rebuilt and checked for embedded textures and animation. Native
approach, flyby, rear and closure frames were inspected; a full 1,205-frame live
run completed without reported validation errors. GCC/UCRT and Clang builds and
shader validation passed, with pixel-identical frame 870 across compilers.
Evidence is in `build/omega-silhouette/`; the pre-revision Blender file is
`build/omega-detail/before-silhouette.blend`.

## Official book elevation proportions

Used the user's supplied book elevation as the primary proportions reference.
The rotating habitat is shorter and farther forward; the exposed forward spine
is 4.09 model units versus 8.08 aft. Added continuous armor around recessed
equipment rows, paired red sockets, small service pipes and repositioned turrets.
The narrower stern has matching exhaust spill and native optical glow centers.
Bow and exhaust longitudinal anchors remain fixed for jump-gate timing.

The current export contains 50,810 ship triangles and 56,618 scene triangles.
Both GLBs contain six embedded images and a habitat animation; packed native
normals were checked for zero length. Clang and GCC/UCRT builds and SPIR-V
validation passed. The 1,205-frame native loop completed without reported
validation errors. Front/rear Blender renders and the side elevation were
inspected. Evidence is in `build/omega-book/`; the saved prior source is
`build/omega-silhouette/before-book-proportions.blend`.

## Engine and small-light shader revision

The native exhaust discs now grade from white-hot centers to blue rims, with
soft periwinkle halos and slightly stronger local blue spill. Red navigation
beacons and warm-white running lights have separate emission colors and steady
output. Compact HDR scatter uses visible scene samples, preserving occlusion
and giving small lamps a tighter glow than the engines. Front and rear captures
at frames 750, 870 and 930 were generated and inspected. Clang and GCC/UCRT
builds, SPIR-V validation and a full 1,205-frame live loop passed.
Evidence: `build/omega-lights/`.
