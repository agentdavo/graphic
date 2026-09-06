# Source consolidation — 6 September 2026

The repository now has two library boundaries, vkmin and sndmin, and a separate
render archive built on vkmin. This is a dependency and ownership consolidation;
it preserves the existing rendering and DSP algorithms.

## Changes

- `min_types.h` and `min_math.h` hold independent value types and pure math.
  Audio's public interface no longer imports GPU or renderer declarations.
- `vkmin_gpu.h` contains the GPU transport contract. Renderer layouts and their
  C/GLSL size/offset assertions live in `render_shared.h`.
- Heightfield, terrain, sun and jitter helpers moved out of `vkmin.c` into
  `render_geometry.c`. Their public names and arithmetic remain unchanged;
  validation now uses the renderer's local fail-fast check.
- GPU object compilation no longer depends on generated renderer shaders or
  font data. Examples 01–07 link only the GPU object group.
- `make libraries` produces separate GPU and render archives. `make packages`
  exports these plus audio and vkmin's generated single header.
- `make package-test`, included in `make test`, compiles consumers with only
  exported include directories. It executes a GPU frame, offline audio render,
  renderer construction/destruction and a geometry query.
- Third-party stb headers use their own system include directory in the
  single-header consumer checks. Warnings remain errors for project code.
- Line-budget checks include the moved/common files, so splitting files cannot
  make the measured implementation disappear from the budget.

The generated GPU header decreased from 5,497 to 4,636 lines. This measures
the reduced package surface, not a performance improvement.

## Migration

Include `render_shared.h` when using renderer layouts, or `render.h` when using
the renderer. Terrain-only clients include `render_geometry.h` and link render.
`shared.h` remains a compatibility include. Existing math spellings remain in
`vkmin_math.h`; new shared/audio code uses `min_math.h` explicitly.

The single-header distribution still requires Vulkan and stb headers, and GLFW
unless built without platform support. Audio remains independently buildable
through `Makefile.sndmin`, with exactly one null or live backend.

## Verification

GCC and Clang package consumers compiled and ran under Windows/MSYS2 with
lavapipe. Logs: `build/consolidation-gcc-packages-final.log` and
`build/consolidation-clang-packages-final.log`.

Audio golden hashes, replay equality, thread/queue checks and frame-inspection
regressions passed in `build/consolidation-full-test-final.log`. The final audio
budget check also passed in `build/consolidation-audio-budget.log`: 1,881 core
lines, including common math/types, against the existing 4,000-line budget.

The graphics harness completed **114 checks with one failure** in
`build/consolidation-graphics-final.log`. The sole failure is the current OMEGA
image golden (19,627 pixels outside tolerance), following concurrent model,
surface and shader edits. All other graphics/reference/replay and shared-audio
checks passed. Graphics source is 9,204 lines against the existing 10,000 budget.

As a control, the original `HEAD` OMEGA C source, shared header and shaders were
built in `build/consolidation-baseline` against the consolidated libraries.
That image **matches the committed golden at the harness tolerance of 2**.
See `build/consolidation-omega-baseline-final.log` and the reproduction script
`build/consolidation-baseline/check.py`. The OMEGA mismatch therefore remains
with the concurrent artwork change; its golden was not updated by this work.

The repo-wide analysis also needed `tools/cppcheck-vkmin.cfg` to model the
public `_Noreturn vkmin_fail` contract across translation units. This avoids
false returning-control-flow assumptions in client assertions without
suppressing diagnostics. The analysis rerun is in
`build/consolidation-analysis-final.log`; the graphics harness is in
`build/consolidation-graphics-final.log`.

The final repo-wide cppcheck run completed successfully with **no diagnostics**,
including whole-program analysis. GCC builds ran with `-fanalyzer` and warnings
as errors. GCC and Clang exported consumers also ran from their package working
directory, without relying on the repository working directory.

The function model includes its three fixed arguments and variadic format
signature; a minimal allocation/assert/use reproduction verifies it. Repo-wide
cppcheck now uses four workers (override `CPPCHECK_JOBS`) and a build directory
for whole-program analysis and cached unchanged translation units.

The earlier run caught an invalid sequence in the new GPU test, missing
explicit renderer capacities in the new renderer test, third-party warnings
leaking into the amalgamation check, and a cross-translation-unit analysis
issue in terrain assertions. Those were corrected before the final run.

No golden files were regenerated. Separate OMEGA model/asset edits appeared
in the shared workspace during this work and are outside this change's scope.

See the [source ownership map and next gates](../src/README.md) for the remaining
resource-lifetime, configuration-ownership and measured-optimization work.
