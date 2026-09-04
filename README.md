# vkmin v0.3 — a Doom 3-class world in a small C11 codebase

A Vulkan 1.3 renderer whose whole design is subtraction: no render passes, no
framebuffers, no vertex input state, no per-draw descriptor binding, no
material scripts, no runtime shader compilation, no allocator. What is left is
one context, one device arena, one bindless texture set bound once per frame,
nine pipelines created at init, and a frame that is a single function written
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
| `tools/cook.c`, `tools/cook_image.c` | ~800 | glTF → `.vkm` + BCn `.ktx2`, offline |
| `tools/imgdiff.c`, `tools/mkfont.c`, … | | golden comparison, font baking, small checks |

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

**6. Clustered forward lighting.** A 16×9×24 froxel grid filled by
`cluster.comp`, one thread per cluster, no atomics. `r_clustered=0` is the
brute-force reference (every light, every pixel); the harness requires the two
to agree within 2/255. `r_debug=2` shows lights-per-cluster as a heat map.

**7. Shadows.** One depth atlas (4096², 1024² in the software profile). Sun
cascades share quadrant 0; local lights get 512² tiles from the other three,
ranked by projected size, six tiles for a point light, one for a spot. Shadow
rendering reuses the indirect draw path with the depth-only pipelines. 3×3 PCF
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
`ctx->path` from the features the device reports — a 1.3 device exposing the
promoted extensions is "modern" — then never consulted again except at those
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

`src/vkmin.h` is **223 lines** (201 non-blank) and is the documentation: the
header comment states the shape (one context, typed handles, zero-default
descs, no callbacks, no allocation after init, no clock, single-threaded,
failure fatal) and every function carries a state contract in its trailing
comment — `pure`, `reads ctx`, `writes ctx`, `gpu`, `io`. The compile-time
block at the top (`VKMIN_MAX_BUFFERS/IMAGES/PIPES`, `VKMIN_FAIL`,
`VKMIN_ASSERT`, `VKMIN_NO_PLATFORM`) has a default for every value.

**Resources are numbers.** A shader reaches a texture by its bindless index
(`vkmin_index`) and a buffer by its device address (`vkmin_address`); both
travel in the push block the user defines in `shared.h`. There is no
`vkmin_bind`: a draw is `vkmin_draw(ctx, pipe, push, bytes, verts, inst)`.
`vkmin_frame_begin` returns `false` when the program should stop (window
closed, `--frame N` reached), so the main loop is a `while`. The command line
goes into `vkmin_init` and is parsed there: `--headless --frame N --out PNG
--path=... --record FILE --replay FILE`, and any cvar as `name=value`.

**The journal.** With `--record FILE`, every public call that reaches the GPU
is appended as a record `{op, header, payload, relocation list}`, including
the bytes of every upload, shader and push block. `vkmin_replay` re-issues the
stream in a fresh process: `07_replay` is the replay of `06_cube`, without the
cube's code, shaders or texture. Device addresses are the difficulty — the
replayed process gets different ones — so the journal header stores the
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
`07_replay` (24); each renders frame 60 at 256² against a golden.

**Budgets, honestly.** The brief's budget for the core was 2500 lines with
both paths, written for the cube lineage; `src/vkmin.c` is **2972 lines
(2777 non-blank)** carrying both paths (108 legacy-only, 74 modern-only),
the journal, hot reload, stats and dump. The renderer (`render.c`, 776) and
the rest of `src/` bring the core to about 4800. It is over the number and I
have not cut it to fit; the journal alone is ~400 lines, and taking it out
would remove the one feature that turns every bug into a batch job. The
brief's other acceptance test — hand the header to someone and time them to a
cube — cannot be run here; `06_cube` is my own answer to it, and it is 33
lines.

## Test coverage, in numbers

`make test` runs **42 checks**: a pure-function unit test, the handle-
generation test, the `--no-readback` refusal, seven example goldens, the
journal (two recordings replayed at tolerance 0, one across paths), the
`--probe` report, an amalgamation compile, the GPU-layer smoke test (plus
its BC1 variant), **9 golden images** compared at **2/255 per channel** with a
diff image written on failure (four Corridor frames, two debug views, the
overlay, two smoke renders), the same four frames on the modern path at
tolerance 0, one frame under naive sync at tolerance 0, one frame rendered
twice at tolerance 0, four reference-vs-fast agreements (GPU/CPU cull,
compact/stable cull, clustered/brute-force lighting, naive/pipelined sync),
six debug-view renders, and two windowed runs under Xvfb (one per path)
compared to the offscreen render at tolerance 0. cppcheck is a prerequisite
of the target and fails it. From clean on this machine the whole run takes
**~95 s**, of which the build is about 20.

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
the spec under CLEAR — the barrier now names all transfer stages); the same
fill racing the previous frame's count-readback copy; the count readback
overwriting its own ring bytes two frames apart; and, in v0.0, a swapchain
image read after present. Each fix is a comment at the barrier it changed.

## Determinism

Nothing in the render path reads a clock or calls `rand()`. `--frame N` renders
frame N without simulating the frames before it. `make test` renders frame 240
twice and requires bit-identical output, and requires `--sync-naive` (one frame
in flight, wait idle per submit) to match the two-frames-in-flight path
exactly. Headless overlays omit timings for the same reason.

Golden images are rendered under `--profile lavapipe` (320×180, 8 lights, one
cascade, 1024 atlas, no transparents, no overlay) at frames 0, 120, 240, 360,
one process per frame, compared at 2/255 per channel with a diff image written
beside any failure. Under lavapipe this manages about five frames a second,
which is what it is for.

## Deviations from the design note, stated plainly

- **The vertex is 24 bytes, not 20** — see system 4.
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
