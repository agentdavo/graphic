# vkmin / sndmin — v0.6 integration candidate

**New demo: [OMEGA — Through the Blue](docs/omega.md).** A procedural destroyer
emerges from a blue jump gate and fires its forward cannons, with a rotating
habitat, escorts, custom shaders, shadows, bloom and synchronized sndmin audio.
Four finned pylons power an ignition flash and expanding funnel with a dark
throat; the ship approaches rapidly, brakes near the exit, and the gate closes.
The 20-second sequence includes a crackling bass-heavy gate startup and an
original 150 BPM electronic score synthesized with sndmin. The gate's energy
veils the recessed pylons while leaving the emerging ship clear.
On Windows, open `OMEGA.cmd`. Both MSYS2 UCRT and Clang builds are supported via
`tools/build-msys2.ps1`; see the demo guide for controls and verified builds.

Two C11 libraries, one integer simulation timeline and one shared replay file.
The valley now advances every audio tick even when images are skipped. Its
two-minute ambient score uses three public synth patches. A small
[public API example](examples/sndmin/06_unison.c) demonstrates matching audio and
image cues, pause, mute and restart. Audio builds without Vulkan or a sound card:
`make -f Makefile.sndmin sndmin-test`.

This candidate adds persistent literal bus gains, transactional song loading,
audio journal compatibility checks and a fix for unnamed rendering passes under
Vulkan validation. See the [integration report and remaining gates](docs/v0.6-integration.md),
[audio usage](docs/using-sndmin.md) and [review plan](docs/v0.6-review.md).
Existing Corridor golden failures remain open; this is not full release acceptance.

## vkmin v0.5 — outside

The valley demo combines terrain LOD, GPU scatter, grass patches, foliage and
impostors, shared wind, an analytic sky, atmospheric perspective, river water,
bloom and deterministic temporal AA. Original artwork is baked offline into
images and one scene file. The camera travels downstream for two minutes.

```sh
make
./build/ex_20_valley
./build/ex_20_valley --profile=lavapipe --frame 2400 --out shallows.png
./build/ex_20_valley --headless --frame 48 --timings --out meadow.png
./build/ex_20_valley --headless --frame 120 +taa 0 --out isolated.png
```

TAA-enabled `--frame N` evaluates frames 0 through N and prints the warm-up
notice. `+taa 0` restores isolated evaluation. Golden images use journal replay.
The lavapipe profile uses 320×180, 1/50 grass density, one terrain level, a
1000-metre far plane and no bloom or TAA. `+r_grass_patch 0` selects the simpler
single-blade reference; the normal path groups 48 blades per scatter instance.

See the [v0.5 report](docs/v0.5-outside.md) for measured performance, visual
comparisons, release checks, reuse candidates and remaining limitations.

## Previous release

# vkmin v0.4 — a Doom 3-class world in a small C11 codebase, and five games on it

**Current verification:** the isolated v0.4 Windows/lavapipe build for `main` completed 80
checks, with six historical Corridor golden mismatches still open. All five
game journals replay exactly across Vulkan paths; gameplay traces and outcomes,
static analysis, shader validation and runtime/reference checks passed. The
five changed demo goldens were inspected and replaced. Concurrent v0.5 and audio
work was excluded from this commit; this is not yet v0.4 acceptance. See the
[executed report](docs/verification-2026-09-05.md) and
[remaining upgrade plan](DESIGN-v0.4.md). Native reproduction with the local
w64devkit, Vulkan SDK, Mesa and cppcheck: `./tools/test-windows.ps1`.

For the API's ownership model, the different kinds of numeric IDs, and runnable
record/replay commands, read [Using vkmin](docs/using-vkmin.md). The five games
share **R** to restart and **F1** to toggle controls. Add `--state-trace FILE`
to record named gameplay checkpoints alongside the input demo or GPU journal.

A Vulkan 1.3 renderer whose whole design is subtraction: no render passes, no
framebuffers, no vertex input state, no per-draw descriptor binding, no
material scripts, no runtime shader compilation, no allocator. What is left is
one context, one device arena, one bindless texture set bound once per frame,
ten pipelines created at init, and a frame that is a single function written
in the order the GPU executes it:

```
cull -> shadow atlas -> depth prepass -> light clusters
     -> forward opaque -> forward transparent -> tonemap -> overlay
```

The demo is **The Corridor**: a deterministic flythrough of Sponza with one sun
(four cascades), sixty-four point lights on fixed orbits, three hundred
instanced props, a skinned character on a loop, Sponza's own alpha-masked
foliage, and two transparent panes. The camera rides a closed spline driven by
the frame index and nothing else.

```
make                     # build everything: core, demo, tools
make test                # golden-image harness under lavapipe; no GPU needed
make golden              # regenerate tests/golden
make analyze             # second static analyser (cppcheck)
make SANITIZE=1 BUILD=build_san build_san/corridor    # ASan + UBSan build

./build/corridor                                         # windowed; F1 debug views, Space pause, F12 save
./build/corridor --headless --frame 240 --out shot.png   # exactly frame 240
./build/corridor --headless --frames 0,120,240,360 --out-dir shots/
./build/corridor --profile lavapipe --headless --frame 240 --out shot.png
./build/corridor r_debug=2 r_shadows=0 r_gpu_cull=0      # any cvar as name=value
./build/corridor --cvars                                 # list them all
```

## What is where

| | lines | |
| --- | --- | --- |
| `src/shared.h` | 252 | every struct that crosses the CPU/GPU boundary, compiled by C and GLSL |
| `src/vkmin.h`, `src/vkmin.c` | 205 + 2036 | the GPU layer: device, arenas, ring, bindless, pipelines, recording, timestamps, readback |
| `src/render.h`, `src/render.c` | 71 + 782 | the renderer: the frame, the shadow view builder, the transparent sort, the overlay layout |
| `src/cvar.h`, `src/cvar.c` | ~110 | the flat tunable table |
| `src/ktx2.c`, `src/scene.c`, `src/vkm_format.h` | ~200 | runtime loaders for the cooked assets |
| `src/mat4.h`, `src/pack.h`, `src/plat*.c` | ~310 | pure maths, attribute packing, the GLFW backend |
| `shaders/` | 563 | ten GLSL files, one shared preamble, one shared vertex fetch |
| `demo/corridor.c`, `demo/anim.h` | 498 + 89 | the demo and its skeletal animation sampler |
| `tools/cook.c`, `tools/cook_image.c` | ~800 | glTF â†’ `.vkm` + BCn `.ktx2`, offline |
| `tools/imgdiff.c`, `tools/mkfont.c`, â€¦ | | golden comparison, font baking, small checks |

Core (`src/`, excluding the baked font and the stb bridge) is **4056 lines**,
about 3200 without comments and blanks. The budget was four to five thousand.

## The ten systems, and how each one is checked

**1. Memory and resources.** One device arena backs one `VkBuffer` for every
buffer the renderer ever makes; a second arena holds every image; a
persistently mapped host ring holds staging at init and per-frame data
thereafter, one region per frame in flight. Bump allocation, nothing freed:
running out is an init-time abort with the numbers in it. Handles are 20 bits
of index and 12 of generation; every lookup checks both. Render targets are
made once at the maximum size and a larger window is upscaled by the tonemap.

**2. Bindless texturing.** One descriptor set, one binding: a partially bound,
update-after-bind array of 4096 combined image samplers, declared in GLSL as
both `sampler2D` and `sampler2DShadow` over the same binding and indexed with
`nonuniformEXT`. Bound once per frame to both bind points and never again.
Five sampler presets; nothing outside `vkmin.c` sees a `VkSampler`.

**3. Compressed textures.** `tools/cook.c` writes BC1/BC3/BC5 KTX2 files with
the full mip chain generated offline in linear light; `src/ktx2.c` reads them
in one pass. The encoders were checked by cooking the numbered test grid and
sampling it on the GPU (`tests/golden/smoke_bc1.png`).

**4. Geometry.** One vertex arena, one index arena. A `Vertex` is 24 bytes:
position, octahedral normal, octahedral tangent with the bitangent sign in its
low bit, two half UVs. (The design note said twenty; twelve plus three fours is
twenty-four.) Vertices are pulled by device address; there is no
`VkPipelineVertexInputStateCreateInfo` in the codebase.

**5. Instances and culling.** `cull.comp` tests every instance against every
view in one dispatch and writes `VkDrawIndexedIndirectCommand` records into
two lists per view (back-face culled, double-sided); each list is one
`vkCmdDrawIndexedIndirectCount`. Two modes behind `r_cull_compact`: atomic
append (fast) and stable slot-per-instance (fixed draw order, bit-exact
goldens). `r_gpu_cull=0` is the CPU reference cull. `make test` requires all
three to agree at tolerance 0, and at frame 240 they do.

**6. Clustered forward lighting.** A 16Ã—9Ã—24 froxel grid filled by
`cluster.comp`, one thread per cluster, no atomics. `r_clustered=0` is the
brute-force reference (every light, every pixel); the harness requires the two
to agree within 2/255. `r_debug=2` shows lights-per-cluster as a heat map.

**7. Shadows.** One depth atlas (4096Â², 1024Â² in the software profile). Sun
cascades share quadrant 0; local lights get 512Â² tiles from the other three,
ranked by projected size, six tiles for a point light, one for a spot. Shadow
rendering reuses the indirect draw path with the depth-only pipelines. 3Ã—3 PCF
through the compare sampler, normal-offset plus slope-scaled bias. `r_debug=6`
shows the raw atlas, `r_debug=3` colours by cascade.

**Resource destruction.** `vkmin_free_buffer` / `vkmin_free_image` wait
idle, release the slot and bump its 12-bit generation; `tests/handles.c` frees
a resource, re-creates one in the same slot, and proves the stale handle
aborts while the new one works. Arena memory is not reclaimed (bump
allocators); a free-list suballocator is a later parallel implementation.

**8. Skinning.** Four joints per vertex in a parallel `SkinVertex` buffer, bone
matrices in the ring, applied in the shared vertex fetch when the instance says
so. CesiumMan runs its walk loop. The TODO for compute pre-skinning is in
`shaders/scene_vertex.glsl`, where it will be read.

**9. Transparency and post.** Blend materials are sorted back to front on the
CPU (stable, deterministic) and drawn with the same shader through a blending
pipeline. The frame renders into `B10G11R11_UFLOAT` and one full-screen pass
tonemaps (ACES fit, Reinhard, or clamp) and sRGB-encodes onto the backbuffer.

**10. Overlay and instrumentation.** GPU timestamps bracket every pass and the
overlay shows them per frame together with draw counts (read back one frame
late through the ring), view and light counts, memory use, and every cvar that
differs from its default. The font is baked at build time from a system TTF
and committed as `src/font.h`, so the binary has nothing to fail to find.

## v0.2: the Vulkan 1.4 baseline, with 1.3 kept alive

The three operations where 1.3 and 1.4 differ are implemented twice, as two
complete functions each, and the path is chosen **once at init** into
`ctx->path` from the features the device reports â€” a 1.3 device exposing the
promoted extensions is "modern" â€” then never consulted again except at those
three call sites. `--path=legacy|modern` forces it; a forced path the device
cannot do fails at init naming the missing feature. `--probe` prints what a
device offers and what would be chosen, creating nothing else.

| seam | legacy (staging, 1.3) | modern (1.4 promotions) |
| --- | --- | --- |
| readback | per-frame copy into a mapped buffer, read after the fence | `vkCopyImageToMemory` from the host after the fence; no command, no staging |
| upload | ring staging + `vkCmdCopyBufferToImage` in an immediate submit | `vkCopyMemoryToImage` with host-side layout transitions; no command buffer |
| shader stages | transient `VkShaderModule`s destroyed after pipeline creation | SPIR-V chained into the stage (`maintenance5`); no module object |

On both paths the backbuffer is now an image we own; windowed frames blit it to
the swapchain image. Nothing is ever captured from a swapchain image and the
BGRA swizzle is gone. Push descriptors are **not** implemented: the tree was
already at the bindless end state the v0.2 brief names as the goal, so that
seam would have been dead on arrival. Debug builds create every pipeline with
`robustBufferAccess2` where the device has it (the init report says whether it
does). `scalarBlockLayout` is required on both paths.

`tools/pathlines.sh` counts the marked regions of `src/vkmin.c`: **2375 lines
total, 108 legacy-only, 74 modern-only.** The modern side is smaller, which is
what the version bump was for. `make test` renders every golden frame on both
paths and requires bit-identical output; on a device with one path the other
is reported as skipped by name.

## v0.3: the API surface

`src/vkmin.h` is **271 lines** and is the documentation: the
header comment states the shape (one context, typed handles, zero-default
descs, no callbacks, no allocation after init, no clock, single-threaded,
failure fatal) and every function carries a state contract in its trailing
comment â€” `pure`, `reads ctx`, `writes ctx`, `gpu`, `io`. The compile-time
block at the top (`VKMIN_MAX_BUFFERS/IMAGES/PIPES`, `VKMIN_FAIL`,
`VKMIN_ASSERT`, `VKMIN_NO_PLATFORM`) has a default for every value.

**Resources are numbers.** A shader reaches a texture by its bindless index
(`vkmin_index`) and a buffer by its device address (`vkmin_address`); both
travel in the push block the user defines in `shared.h`. There is no
`vkmin_bind`: a draw is `vkmin_draw(ctx, pipe, push, verts, inst)`. The push
block's size is the pipeline's (`.push_size`), not the draw's, and at
creation it is checked against the push-constant block the SPIR-V declares:
the CPU/GPU layout mismatch `shared.h` cannot `_Static_assert` -- the game's
own push struct against the game's own shader -- is caught at init, once, by
a tool. Folding that check in immediately found three of the seven examples
declaring the engine's 32-byte block through `common.glsl` while pushing
their own 88-byte one; the draws had worked by luck of layout.

**A pointer never travels without its size.** `vkmin_bytes` is
`{ const void *data; size_t size; }` and `VKMIN_BYTES(x)` pairs an array or
object with its `sizeof` in one compound literal, so `.vs`, `.fs`, `.cs`,
`.data`, `.pixels` and the uploads take one argument each and the wrong size
is unwritable. **A frame reads the outside world at one point:** the loop is
`while (vkmin_running(ctx)) { f = vkmin_frame_begin(ctx, clear); ...; vkmin_frame_end(ctx); }`,
where `vkmin_running` polls the window and the demo file and decides the
next frame, and `f` is a value carrying the frame index, slot, size, aspect
and input snapshot -- the same record the journal writes. `vkmin_frame_index`,
`vkmin_aspect`, `vkmin_input` and `vkmin_key_hit` are gone. The command line
goes into `vkmin_init` and is parsed there: `--headless --frame N --out PNG
--path=... --record FILE --replay FILE --demo FILE --play FILE`, and any cvar
as `name=value`.

**The journal.** With `--record FILE`, every public call that reaches the GPU
is appended as a record `{op, header, payload, relocation list}`, including
the bytes of every upload, shader and push block. `vkmin_replay` re-issues the
stream in a fresh process: `07_replay` is the replay of `06_cube`, without the
cube's code, shaders or texture. Device addresses are the difficulty â€” the
replayed process gets different ones â€” so the journal header stores the
recording's arena and ring base addresses and every 8-byte word of a payload
that *exactly equals* an address issued during recording (an arena allocation
or a ring allocation of the current frame) is relocated. That is a heuristic,
stated as one: an address computed by the user (base + offset) that was never
issued as such is not relocated, and a data word that happens to equal an
issued address would be. The renderer stays inside the rule (it stores issued
addresses only), and replay asserts that handles and ring usage match the
recording, so a divergence fails loudly rather than drawing garbage. The
harness records `06_cube` and The Corridor at frame 240 on the modern path
and replays both at tolerance 0, and replays the modern recording on the
legacy path.

**Also in v0.3.** `vkmin_math.h` is the pure maths header (every function
`pure`, `vkmin_`-prefixed). Shader hot reload: a pipeline created with
`vs_path`/`fs_path`/`cs_path` is rebuilt when the `.spv` mtime changes
(`r_hotreload`). `vkmin_stats_get`, `vkmin_dump` (context state to a stream)
and `vkmin_probe` (device report, creates nothing else). `make amalgamate`
produces `build/vkmin_single.h`; `make test` compiles it as a check. The
examples are the tests: `01_clear` (12 lines), `02_triangle` (18),
`03_buffer` (25), `04_texture` (26), `05_compute` (29), `06_cube` (33),
`07_replay` (24); each renders frame 60 at 256Â² against a golden.

**Budgets, honestly.** The brief's budget for the core was 2500 lines with
both paths, written for the cube lineage; at the end of v0.3 `src/vkmin.c`
was 2972 lines carrying both paths (108 legacy-only, 74 modern-only), the
journal, hot reload, stats and dump, and with the renderer (`render.c`, 776)
and the rest of `src/` the core was about 4800 (v0.4's numbers are below). It is over the number and I
have not cut it to fit; the journal alone is ~400 lines, and taking it out
would remove the one feature that turns every bug into a batch job. The
brief's other acceptance test â€” hand the header to someone and time them to a
cube â€” cannot be run here; `06_cube` is my own answer to it, and it is 33
lines.

## v0.4: what games need

Five games, one engine, and the rule that a primitive goes in only if two of
them use it. Written down first, as the brief asked: a shooter wants a
perspective camera, many lights, shadows, skinned characters, particles and a
HUD; a strategy game a tilted camera over terrain, thousands of units,
picking, selection rectangles and health bars; a top-down game an
orthographic camera, sprites among 3D props, picking; a platformer a side
camera, parallax and one animated character; the anime game any camera,
quantised lighting, outlines and grading. Stripped of the genre words that
is nine things, and the table below is where each lives and who uses it.

| primitive | where | used by |
| --- | --- | --- |
| cameras as pure functions (`camera_fps/rts/ortho_topdown/side`) | `vkmin_math.h` | all five |
| rays (`ray_from_pixel`, ray-AABB, ray-triangle) | `vkmin_math.h` | shooter (hit test), rts (march orders), topdown (ground cursor) |
| `ticks_for_frame` fixed-step time | `vkmin_math.h` | all five |
| returned `vkmin_frame.input` snapshot, journalled; `--demo`/`--play` | `vkmin.h` | all five |
| instancing + GPU cull with the CPU reference (`d_check_cull`) | `render.c`, `cull.comp` | all five (rts: 2000 units + terrain chunks) |
| quad batcher: sprites, particles, billboards, UI, parallax | `render.c`, `quad.vert/frag` | shooter (particles, HUD), rts (selection, health bars), topdown (sprites), platformer (parallax), anime (title) |
| SDF text (`vkr_text`) | `render.c`, `tools/mkfont` | shooter, rts, topdown, platformer, anime |
| skinning | `scene_vertex.glsl`, `demo/anim.h` | shooter (two enemies), platformer, anime |
| shadow atlas | `render.c` | shooter (cascades), rts (one cascade), topdown (point), platformer, anime |
| lighting library the user's shader includes | `shaders/lib/` | `lit_pbr.frag`: shooter, rts, topdown; `lit_cel.frag`: platformer, anime |
| clustered lights | `cluster.comp`, `lib/lights.glsl` | shooter (25 lights), topdown (2); the rest pay nothing |
| ID target + `vkmin_pick` | `vkmin.c`, `render.c` | rts (hover), topdown (click); `tests/pick.c` |
| `vkmin_heightfield` | `vkmin.c` | rts (2 kmÂ² terrain), topdown (the tile floor) |
| post: normal target, outline, LUT, tonemap | `tonemap.frag` | outline and LUT: platformer, anime; fog: shooter, rts |
| glTF via cgltf, KTX2 | `tools/cook.c`, `src/scene.c`, `src/ktx2.c` | shooter (Sponza + CesiumMan), platformer, anime (CesiumMan) |

The nine rendering primitives each have at least two consumers. Colour grading
now has two explicit consumers as well: the platformer and anime showcase.

**Input demos and renderer journals.** `vkmin_running` gathers one input snapshot
and computes edges against the previous snapshot. `vkmin_frame_begin` returns
it and writes the frame-begin journal record. `--demo FILE` writes the
snapshots alone (frame index + inputs, ~150 bytes a frame); `--play FILE`
feeds them back one frame per record. Every game simulates at a fixed rate
from the frame index and reads no clock, so with the same logic, assets and
settings a demo replays the same game and
the goldens are taken from replayed demos. `tests/journals/*.vkd` are
**scripted by `tools/mkdemo.c`, not recorded by a person** -- CI has no
hands -- and a demo recorded in a window is the same file format.
`vkmin_pick` is journalled with its result, so a full journal replay asserts
that the replayed frame picks the same instance the recording did.

**Deviations, stated plainly.** The post stack is one full-screen pass with
every stage always executed and inhibited by its strength, not three passes;
the visible result is the brief's, the ordering is fixed, and it is one
pipeline instead of three. The normal target is written by the forward
pass as a second MRT attachment (with the id as the first) rather than by the
depth prepass, so it survives `r_prepass=0`. A blended pipeline masks the id
and normal attachments off, so one shader serves opaque and transparent
draws. Inverted-hull outlines and compute pre-skinning remain the noted
parallel implementations, not built. glTF goes through the offline cooker
(cgltf, from v0.1), not a runtime loader. The platformer and anime showcase use
the imported CesiumMan walk clip for running and demo-authored keyframed idle
and jump pose offsets. These are not three imported animation clips.
Picking is one frame late by construction (it reads the frame just
submitted). `vkr_look`, the cel ramp, rim, shadow tint, fog, outline and
LUT parameters, travel in the frame desc; the cvars `r_outline` and `r_lut`
are live multipliers.

**A bug found by the platformer, worth recording.** `anim_bones` builds
bone matrices relative to the transform it is given; given the placed
instance transform it cancels the placement and every character lands at
the asset origin. The Corridor had done it right by accident (it passed the
node's own transform), my first draft of the games did it wrong, and the
platformer's hero vanished off the left of the screen. `gk_node_transform`
now exists so the right matrix has a name.

**A lavapipe quirk.** A pipeline whose extra attachments are masked off still
needs its fragment shader to declare those outputs: undeclared, the normal
target came out stippled under the parallax quads and the outline pass drew
the stipple. Declared and written, the mask does its job. World and screen
fragment entry points share `quad.glsl`; the screen entry point declares only
colour because its render pass has no ID or normal attachments.

**Numbers.** Core (`src/*.c`, `src/*.h` less the stb bridge and the baked
font): **5868 lines** with both paths, against a budget of 7000. The shader
library is 262 lines (budget 1000). `vkmin.h` is 273 lines. Every individual
game remains below 500 lines, sharing `demo/gamekit.h` (mesh builders, scene upload,
procedural textures, the lavapipe profile, option parsing) and `demo/anim.h`
(89). The Corridor now uses the same kit and lost 190 lines. The game
developer's test -- someone who has shipped a game reads the header, the
library and one example -- was not run here. Building the five, the things
I reached for and did not have: a sprite-sheet frame helper (three games
faked it with procedural textures), a depth-writing quad mode so sprites can
occlude each other, and a debug line primitive (two games would have drawn
their rays). By the brief's rule the first two belong in v0.5.

## Test coverage, in numbers

The upstream harness reported **57 checks**; the current native run reports
**62**, with the result and added regression tests listed in the verification
report above. The original coverage includes the maths unit test (cameras and rays
included), the handle-generation and wrong-`push_size` aborts, the
`--no-readback` refusal, seven example goldens, the journal (recordings of
`06_cube`, The Corridor and `14_anime` replayed at tolerance 0, two across
paths), `vkmin_pick` on both paths, the five games replaying their demo files
to goldens on the default path and bit-identical on the legacy path (the rts
with `d_check_cull=1`), the `--probe` report, an amalgamation compile, the GPU-layer smoke test (plus
its BC1 variant), **9 golden images** compared at **2/255 per channel** with a
diff image written on failure (four Corridor frames, two debug views, the
overlay, two smoke renders), the same four frames on the modern path at
tolerance 0, one frame under naive sync at tolerance 0, one frame rendered
twice at tolerance 0, four reference-vs-fast agreements (GPU/CPU cull,
compact/stable cull, clustered/brute-force lighting, naive/pipelined sync),
six debug-view renders, and two windowed runs under Xvfb (one per path)
compared to the offscreen render at tolerance 0. cppcheck is a prerequisite
of the target and fails it. From clean on this machine the whole run takes
about six minutes, most of it the shooter's two Sponza replays under lavapipe.

`.github/workflows/test.yml` runs it on a GPU-less runner under lavapipe. It
was written here, where GitHub Actions cannot execute; its first real run is
the check of the file itself.

## Validation is fatal, and it earned its keep

Core validation and synchronization validation are on in debug builds; the
callback aborts at warning severity, and every object is named so the message
says `vkmin.cmd[1]` or `vkr.shadow_atlas`, not a hex handle. Every windowed
variant in `tests/run_tests.sh` runs two frames in flight under both.

Synchronization validation found five real defects during this work, all in
frame-to-frame hazards that a single-frame test can never see: the backbuffer
losing its readback copy as a source scope; a draw-count fill racing the
previous frame's indirect read (syncval files `vkCmdFillBuffer` under COPY,
the spec under CLEAR â€” the barrier now names all transfer stages); the same
fill racing the previous frame's count-readback copy; the count readback
overwriting its own ring bytes two frames apart; and, in v0.0, a swapchain
image read after present. Each fix is a comment at the barrier it changed.

## Determinism

Nothing in the render path reads a clock or calls `rand()`. `--frame N` renders
frame N without simulating the frames before it. `make test` renders frame 240
twice and requires bit-identical output, and requires `--sync-naive` (one frame
in flight, wait idle per submit) to match the two-frames-in-flight path
exactly. Headless overlays omit timings for the same reason.

Golden images are rendered under `--profile lavapipe` (320Ã—180, 8 lights, one
cascade, 1024 atlas, no transparents, no overlay) at frames 0, 120, 240, 360,
one process per frame, compared at 2/255 per channel with a diff image written
beside any failure. Under lavapipe this manages about five frames a second,
which is what it is for.

## Deviations from the design note, stated plainly

- **The vertex is 24 bytes, not 20** â€” see system 4.
- **Instances, lights and bones are re-uploaded every frame** through the host
  ring rather than living in device memory and being patched. At this scene's
  size that is ~80 KB a frame and it keeps "who owns what moves" trivially
  clear; the device-local variant is a later parallel implementation.
- **The forward pipelines use `LESS_EQUAL` with depth write on** rather than
  `EQUAL` after the prepass, so the same pipelines are correct with the prepass
  off. `r_prepass=0` is a live switch.
- **Overdraw visualisation reuses the blend pipeline** with the prepass
  disabled rather than adding a pipeline.
- **Point-light and spot shadow biases are fixed fractions of the light
  radius**, not derived per texel as the cascades' are. Good enough at this
  scale; the atlas view will show when it is not.
- **The KTX2 data-format descriptor is nominal.** It is well formed and our
  reader ignores it; a stricter external tool may want more.
- **Assets are committed cooked** (24 MB), so `make test` needs no network.

## Not done, and known

No compute pre-skinning, no light or instance culling for the shadow views
beyond frustum tests, no cascade blending at split boundaries, no texture
streaming, no resize of render targets (a larger window is upscaled). Removing
any of these would not have made the demo impossible; adding them would have
pushed the core past its budget.
