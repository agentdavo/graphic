# vkmin v0.3 / v0.4: upgrade plan after the pull

Baseline: `d23a148`, fast-forwarded from `4f43eb4` on branch
`claude/vkmin-minimal-vulkan-jghhhn`. Four commits added the v0.4 renderer,
five games, the reviewed v0.3 API, and updated test reports. This replaces
the earlier plan, which described several now-implemented features as missing.

`CLAUDE.md` was read in full after the pull and governs this plan. The exact
[v0.3 prompt](docs/prompts/v0.3.md) and [v0.4 prompt](docs/prompts/v0.4.md)
remain archived unchanged. The user's later v0.3 amendments take precedence:
byte spans, pipeline-owned reflected push size, and the returned frame value.
Implementation is now underway on `codex/v04-completion`. See
[the executed verification report](docs/verification-2026-09-05.md) for current
results. The original audit below records the starting point, not the current
verification status.

## Execution update, 5 September 2026

Implemented: native headless builds with w64devkit and lavapipe; mandatory
shader validation; journal bounds checks and negative tests; bounded push-layout
reflection; idempotent frame queries; transactional shader reload and its replay
record; explicit fragment-shader selection; screen/world quad output signatures;
orthographic outline depth; shadow tile clamping; heightfield sizing guards;
and draining pending cull comparisons at the end of a run. Generated sanitizer
outputs are untracked while local copies and w64devkit are retained.

The subsequent demo pass completed **80 checks with six historical Corridor
golden mismatches** in a frozen source copy. All five game journals and their
gameplay traces agree across Vulkan paths; validation is silent. The five
changed demo goldens were inspected and replaced. This is **not v0.4 acceptance**.

Input edge queuing/interpolation, canonical scalar checkpoints, full journal
coverage for all five games, distinct authored idle/jump pose tracks, a second
grading consumer and a single-mesh top-down floor are now implemented. The
expanded replay coverage also fixed a derived-address bug in mixed quad batches.
See [the demo report](docs/demo-polish-2026-09-05.md).

Next gate: explain/reproduce the six historical Corridor differences in the
original Linux toolchain, validate windowed controls, and perform the external
developer usability review. Imported animation clips and the documented
post/lighting deviations remain distinct from those completed improvements.
The earlier core count was 5,868 with a 273-line public header; concurrent v0.5
work now changes that scope, so those are historical counts.

## Initial audit before implementation (historical)

- Pulled four commits without conflicts; preserved local planning files.
- Read all of `CLAUDE.md`, both prompts, the revised public headers,
  build/CI and test harness, and relevant implementation/game paths.
- Compiled all 20 shader entry points with Vulkan SDK 1.4.357.0
  `glslangValidator`, targeting Vulkan 1.3: all passed.
- Validated all 20 modules with `spirv-val --target-env vulkan1.3
  --scalar-block-layout`: all passed. Logs: `build/plan-audit/shaders.json`.
- Recounted physical source lines, including comments and blanks.
- After the user added `w64devkit/` 2.9.1, built and ran `mat4_test` with
  GCC 16.2.0 and GNU Make 4.4.1. The normal Makefile warning-as-error and
  `-fanalyzer` flags were retained; the test passed. With `w64devkit/bin` on
  the command's PATH, the commands were `make BUILD=build/plan-audit
  build/plan-audit/mat4_test` and `build/plan-audit/mat4_test.exe`.
- Attempted an object-only headless compile of `src/vkmin.c` using the same
  warning/analysis flags, `-DVKMIN_NO_PLATFORM` and the installed Vulkan SDK
  include directory. It failed with five analyzer diagnostics. Full output:
  `build/plan-audit/native-core.log`.

Not executed here: a complete application build, `make test`, lavapipe
rendering, runtime Vulkan validation, cppcheck, sanitizers or human usability
reviews. The added devkit supplies a native C compiler, make and shell;
cppcheck and GLFW were not found in that kit, and WSL inventory returned no
installed distribution. Shader validity does not establish runtime correctness.
README's 57 checks with zero failures is an upstream report, not a result
reproduced in this session.

The five GCC diagnostics are not five independently established bugs: two
point to relocation reads/writes at `src/vkmin.c:3009` and `:3012`, two concern
the same file-controlled allocation at `:3035`, and one flags aborting on
file-controlled validation. Inspection confirms that `relocate()` receives no
payload length and uses unchecked offsets. Bound the data first; separately
review the intentional fatal-input policy against the tainted-assertion
diagnostic. Do not silence the analyzer or count compilation as successful.

## Current implementation versus the brief

| Requirement | Present at this commit | Remaining work |
| --- | --- | --- |
| v0.3 spans, reflected push size, frame value | `vkmin_bytes`, pipeline `push_size`, returned `vkmin_frame` | Harden reflection/lifecycle; do not redesign these again. |
| Resources as numbers, examples, journal | Examples 01–07, addresses, bindless textures, GPU journal | Broader journal/API-contract coverage. |
| Cameras and tick helper | `src/vkmin_math.h`; used by all games | Input edges, catch-up semantics and interpolation. |
| Instancing and reference cull | GPU cull, sorted full-command CPU comparison | Drain final pending checks; exercise both cull modes and boundaries. |
| Quads and SDF text | Shared `Quad`, shaders, `vkr_text`, font baker | Ordering, scaling and attachment-masking tests. |
| Skinning, shadows and assets | Shared vertex fetch, animation sampler, atlas, cgltf cooker/KTX2 | Real animation states, animated bounds, tile-edge sampling. |
| Composable lighting | `shaders/lib/`, three canonical shaders, custom `vkr_desc.fs` | Remove the remaining shading enum; test user composition. |
| ID picking | Integer target and both readback paths; GPU journal records result | Frame/coordinate contract, stronger tests, input-demo pick records. |
| Heightfield | Chunked generator, consumed by RTS and top-down | Safe sizing, topology/bounds tests and surface-query agreement. |
| Post | Forward normal MRT, fused outline/tonemap/LUT, 16³ LUT in a 2D strip | Orthographic correctness and prompt-deviation comparisons. |
| Five games | Examples 10–14, `.vkd` input fixtures and goldens | State assertions, complete replay matrix and exact game briefs. |

Shader include dependencies and mandatory static analysis in `make test`
already exist. Preserve them. The prior plan's proposed second heightfield
consumer is unnecessary: top-down already supplies it.

## Implementation discipline

Start each change with a reproducible failure or unmet acceptance condition.
Keep input/resource ownership explicit and the frame's execution order visible.
Extract finicky pure computations for direct tests; avoid a new abstraction
for an operation already expressible through the existing API.

Keep legacy/modern Vulkan, CPU cull and naive synchronization as references.
Develop substantial reflection/post replacements alongside the current path
and compare before retiring redundant code. Small cleanup uses the existing
custom-shader route. No genre modules, entity system, physics or scene graph.
Every milestone reports executed checks and unavailable paths separately.
Golden changes need explained image differences, not blanket regeneration.

## 1. Establish a trustworthy baseline

Files: `Makefile`, `tests/run_tests.sh`, `.github/workflows/test.yml`,
`.gitignore`, tracked `build_san/` outputs.

- Select/provision Linux with compiler analysis, cppcheck, GLFW, shader tools,
  validation layers, Xvfb and lavapipe. Record versions and the device feature
  report; reproduce a fresh `make test` before calling current goldens verified.
- Keep the new native w64devkit checks as a useful second compiler environment.
  Resolve its reproduced journal diagnostics before declaring the core clean.
  Full native execution additionally needs dependency/link configuration and
  equivalents for the harness's POSIX `fork` tests; do not weaken the existing
  Linux test suite to force it through Windows. Treat the user's devkit as a
  local dependency, not shipping source; exclude it from future repository
  source counts and broad staging.
- Require lavapipe in the software CI job. The harness currently leaves
  device choice open if the ICD is missing. Report unsupported modern
  features explicitly; release acceptance needs a capable software runtime.
- Use fresh output locations or remove expected outputs before each producer.
  Never compare an old PNG after a render failed. Preserve logs/journals/diffs.
- Fix picking-test coverage: the harness passes `--frame 1`, a one-element
  headless list, so the test's two-iteration loop renders only once. Render
  multiple contiguous frames and assert the actual number checked.
- Include `examples/` in cppcheck: its current source set is `src demo tools
  tests`. Keep compiler warnings and analysis enabled.
- Untrack generated `build_san/` binaries, objects, SPIR-V and headers while
  retaining local copies; ignore sanitizer build directories. CI must compile
  sanitizer outputs from source rather than reuse committed products.

Exit: fresh baseline logs, explicit device/path coverage, no stale-output
success, and sanitizer builds demonstrably compiled from source.

## 2. Make the v0.3 contracts dependable

Files: `src/vkmin.h`, `src/vkmin.c`, parser/lifecycle tests, numbered examples
and amalgamation checks.

- First fix the reproduced journal bounds failures. Pass payload length into
  relocation; require each offset to leave at least eight valid bytes;
  validate relocation kind and address range; cap record allocation using
  declared resource/record limits. Add truncated, out-of-range and oversized
  fixtures, then rerun GCC's unchanged analyzer configuration. Retain the
  public fatal-error policy and make validation boundaries clear to the
  analyzer rather than suppressing its diagnostics.
- Make repeated `vkmin_running()` queries preserve an already armed frame.
  It currently sets `armed` but does not check it before reading another demo
  record or input snapshot. Keep the returned frame value and document the
  acquisition point accurately: `running()` gathers, `frame_begin()` returns.
  README references to the removed `vkmin_input()` function need updating.
- Replace the reflection walker with a bounded parser behind the existing
  API. It assumes tight matrix columns, ignores member MatrixStride/major
  order, does not validate each opcode's operand count, and treats unsupported
  types as zero size. Honour supported layouts; reject unsupported ones
  explicitly. Check arithmetic overflow and stage compatibility.
- Test no-push modules, nested structs, arrays, padded/row-major matrices,
  64-bit values and malformed/truncated instructions. Compare valid fixture
  layouts with an offline reflection tool and preserve current valid shaders.
- Make hot reload transactional. Its failed-read branch resets mtimes to zero,
  allowing a successful retry to be treated as initial discovery. It destroys
  the old pipeline before the new one succeeds. Build/validate a candidate,
  swap after success, preserve shader-byte ownership and the working pipeline
  on incomplete files. Remove the cast-away-const label write. Test reload
  during journal recording as well as ordinary iteration.
- Audit spans, typed handles, image sizes, zero defaults, resource lifetime
  and public state comments. Hardware probing correctly says `io`; it cannot
  be pure as the original prompt claimed. Caller-output writers need honest
  contracts under CLAUDE.md's strict purity definition.
- Finish the remaining v0.3 audit: document the four C API comparisons,
  reconcile CLI spellings, verify one indexing model, hot reload, text
  introspection, standalone amalgamation and journals of every numbered example.

Exit: valid examples/journals still match exactly; malformed data fails
deterministically; queries do not advance a frame; failed reload keeps the
previous pipeline usable. Negative tests pass under sanitizers too.

## 3. Strengthen gameplay replay

Files: journal/demo code, `demo/gamekit.h`, all five games, `tools/mkdemo.c`,
`tests/journals/`, harness.

- Distinguish clean EOF from truncated headers/records in both formats.
  Validate opcode lengths, relocation ranges, frame order and versions before
  using data. Reject backward/gapped input frames unless a documented rule
  supplies every intervening input.
- Keep GPU journals and game-input demos distinct. Existing `.vkd` files are
  scripted input fixtures from `mkdemo`, suitable for automation, not human
  recordings. Version additions such as game/settings/asset identity.
- Define tick zero, queue edges until a tick consumes them, and consume each
  edge once during catch-up. `gk_ticks_due` currently provides only a count;
  games discard interpolation alpha. Store previous/current state and use
  alpha when rendering, particularly the 30 Hz RTS/top-down games. No clocks.
- Playback to frame N processes every earlier record. Direct-to-frame with
  zero input remains a diagnostic, not proof of game-state replay.
- Add canonical state hashes and event assertions at meaningful frames:
  shots, selected IDs, orders, jumps/landings and animation state. Hash named
  fields in a fixed order, never pointer values or struct padding.
- Proposed format extension: record consumed pick IDs, coordinates and source
  frame in input demos as well as GPU journals. Replay supplies those IDs to
  game logic; independent real readback checks ensure replay cannot hide
  renderer defects.
- Replay every game's inputs twice, with legacy/modern and naive/normal sync.
  Record/replay every game's GPU journal as well: only anime currently joins
  cube and corridor in GPU-journal coverage.

Exit: state hashes and pixels agree exactly in the pinned runtime; changed
input changes state; malformed demos fail; frame selection and rendering
speed do not change simulation.

## 4. Finish renderer composition and correctness

Files: `src/render.*`, `src/vkmin.c`, `shaders/lib/`, `shaders/tonemap.frag`,
`tests/pick.c` and focused geometry/render tests.

- Remove `vkr_shading` and `.shading`. Canonical shaders use the existing `.fs`
  span exactly as user shaders do; a zero/default PBR remains possible. Have
  an example compose a custom fragment shader with documented surface,
  lighting, shadow, fog, push data and required MRT outputs.
- Keep ID zero for no hit and opaque writes unconditional. Retain the current
  image-explicit `vkmin_pick(ctx, image, x, y)` as the low-level API; document
  its deviation from the sketch rather than introduce hidden scene state.
  Specify the between-frame read, source frame and fence wait, plus window,
  framebuffer and render-target coordinate conversion for HiDPI/upscaling.
- Test overlap, masked holes, skinned geometry, nonselectable occluders,
  transparent/UI masking, first frame, bounds and consecutive submissions on
  both paths. Pixel-exactness needs these tests, not just a centred cube.
- Drain pending cull comparisons before reporting final results. They run on
  slot reuse, so final frames can escape checking. Cover empty/full sets,
  compact/stable modes, material partitions and chunk edges. Preserve sorted
  comparison of every command field, including duplicates.
- Correct outline depth reconstruction for orthographic projections:
  `linear_depth()` currently applies the perspective formula universally.
  Test both projections, near/far changes and resized render regions.
- Constrain shadow PCF taps to their atlas tile or provide a tested border;
  current taps can cross tile edges. Use adjacent tiles with very different
  depths as the regression fixture.
- Test heightfield sizing, topology, normals and conservative chunk bounds;
  check arithmetic before allocation and output capacities. Cover non-square
  grids, partial chunks, invalid spacing, negative heights and overflow.
  Gameplay contact queries should agree with rendered triangles.
- Verify SDF at several sizes, alpha ordering and quad ID/normal masking.
  Keep one batcher with batches for necessary render-state differences.

Exit: boundary fixtures pass with silent runtime validation. Unchanged shader
compositions remain pixel-identical; intentional correctness fixes get
explained diffs and targeted golden updates.

## 5. Reconcile post and finish the game briefs

Forward normals and fused post are implemented deviations, not absent effects.
The 16³ LUT packed in a 256×16 strip is a 3D colour transform, although it is
not a native 3D image. Preserve these implementations as comparison paths.

- Build the prompt's normal-prepass/separate-post implementation alongside
  the fused path. Order: outline in HDR, tonemap once, LUT, output encoding,
  UI/present. This resolves the prompt's contradictory second tonemap mention.
- Add native 3D LUT sampling beside the strip sampler; compare identity,
  channel ramps and nontrivial transforms. Measure code/resource cost and
  timings before choosing the release default. Retaining fusion, forward
  normals or strip storage requires documented equivalence and a stated
  deviation; do not silently claim literal prompt compliance.
- Give grading a meaningful second consumer, proposed platformer, and replay
  it in tests. Counting the anime-only LUT under the shared post label is a
  weak application of the two-consumer rule.
- Supply distinct idle/run/jump data and share the character with anime.
  The current held/walking/held-mid-stride single clip is a placeholder.
  Cook through cgltf offline and keep test assets available without network.
- Preserve shooter's interior/lights/two enemies/fire effects/HUD, RTS's 2,000
  units/selection/orders/health bars/terrain, top-down's sprites/props/following
  light, platformer's three layers/moving platforms, and anime's cel/rim/post.
- Reconcile top-down's chunked floor and extra sun with its single floor mesh
  and single point-light brief. A one-chunk heightfield keeps the generator's
  second user while meeting the one-mesh requirement.
- Demonstrate required interactions in recordings/state checks before updating
  the goldens affected by deliberate visual improvements.

Exit: all game briefs demonstrated, two observable users per primitive, and
every retained deviation backed by evidence and explicit documentation.

## Budget, ordering and completion

Measured at `d23a148`: core `src/*.c,h` excluding baked font and stb bridge **5,692**
lines; public header **271**; shader library **259**. Games are **195, 264,
152, 167, 115**, plus shared `gamekit.h` **303** and `anim.h` **89**. README's
5,582 core count is stale after the API revision.

Strict v0.4 limits: core below 7,000, shader library below 1,000, each example
below 500. Current core headroom is at most **1,307 lines**. Count comments
and blanks; report shared helpers and temporary reference paths without
hiding renderer growth in examples/generated files. Do not cut validation to
fit. Record the original v0.3 2,500-line overrun separately.

Order: baseline -> API/parser/lifecycle fixes -> replay/state checks ->
renderer boundaries -> remaining post/assets work. Keep earlier checks
running at every stage. Sprite-sheet convenience APIs, debug lines, compute
pre-skinning, LOD and inverted-hull outlines remain outside this completion plan.

The release report lists actual users of all nine primitives, test commands
and versions, exact cross-path comparisons, skipped features, changed goldens,
size counts, defects found and remaining deviations. Human header-to-cube and
game-developer reviews remain unrun until performed; the automated technical
gate must stand on its own without a human or physical GPU.
