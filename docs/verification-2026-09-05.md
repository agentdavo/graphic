# vkmin implementation and verification — 5 September 2026

Base: `d23a148`, pulled fast-forward from `4f43eb4`. Work is local on
`codex/v04-completion`; nothing was pushed. The full `CLAUDE.md` and both
archived build prompts informed the changes. This report supersedes earlier
planning-only verification claims in `DESIGN-v0.4.md`.

## Commit verification on main

The v0.4 changes were assembled separately in `build/main-ready` on local
`main`, excluding concurrent outdoor and audio work. A complete build and
`make HEADLESS=1 -j4 test` reproduced **80 checks, six failures**, all the known
Corridor golden comparisons. All five game goldens, gameplay outcomes and
cross-path journals passed, as did static analysis, shader validation and the
runtime/reference checks. The local log is `build/plan-audit/main-ready-test.log`.
This verifies the v0.4 commit independently of the earlier mixed-source snapshot.
No release acceptance or remote push is implied.

## Earlier result: demo polish snapshot

The frozen-source run in `build/polish-snapshot` completed **80 checks,
6 failures**. Every failure is an unchanged historical Corridor golden:
frames 0/120/240/360, overlay and cascade debug view. The five deliberately
changed game goldens were inspected and replaced; their previous versions
remain in git and locally in `build/plan-audit/pre-polish-goldens`.

All five game renderer journals now replay pixel-identically on the legacy
path after recording on the modern path. Their input-driven gameplay traces
also match across paths. Outcome checks pass: shooter opens its gate and clears
both targets with six hits; RTS issues an order to selected units; top-down
inspects a prop; platformer jumps and lands; anime visits all three animation
states. Pure fixed-step input tests pass. Core/synchronization validation is
silent; static analysis, SPIR-V validation and the remaining reference checks
pass. Windowed rendering, sanitizers, Linux CI and human usability remain
unverified here.

The new mixed world/screen quad coverage found and fixed an actual journal
relocation defect: the screen draw now uses an allocation base plus an integer
start index instead of an unregistered derived device address. See
[the demo report](demo-polish-2026-09-05.md) for implementation details and limits.

An independently active v0.5 task changed the shared checkout during initial
verification. The final run therefore used an isolated source copy, including
the outdoor work present at capture time. It does not certify subsequent
shared-checkout changes or v0.5. Evidence: `build/plan-audit/frozen-final-test.log`,
`frozen-source-sha256.csv`, `frozen-makefile-sha256.txt`, and the per-case logs,
state traces, journals and PNGs in `build/polish-snapshot/tests/out`.
`dependency-final-check.log` verifies that changing a transitive cvar header
rebuilds the standalone play helper test. Game source counts in that snapshot
are 232/288/170/186/112 lines, all below the 500-line example budget.

## Earlier result: core hardening, before demo polish

A fresh headless build in `build/verified` compiled and linked the core,
examples, tools, tests and generated single-header form. GCC's normal warnings
remain errors and `-fanalyzer` remains enabled on first-party translation units.
Cppcheck passed, including the previously omitted examples. All 21 production
shader entry points and two test shaders passed `spirv-val`.

The full native harness completed **62 checks, 11 failures**. The failures are
comparisons against committed golden images: the five games, four Corridor
frames, the overlay and the cascade debug view. These images were **not
regenerated**. A successful compilation or agreement between two paths is not
an explanation of a historical-golden mismatch.

Passed under CPU lavapipe with fatal Vulkan core/synchronization validation:

- Basic example and smoke goldens, including BC1 texture upload.
- Modern versus legacy output for all five game demos and four Corridor frames.
- GPU versus CPU culling, compact versus stable culling, clustered versus
  brute-force lights, and naive versus pipelined synchronization.
- Cube, Corridor and anime GPU-journal replay, including cross-path replay.
- Picking on both paths, with two frames actually rendered and checked.
- Stale handles, reflected push-size rejection, lifecycle, heightfield and
  malformed SPIR-V tests.
- Eight corrupt journal fixtures rejected before reaching unsafe operations.
- Incomplete hot-reload file preserves pixels; a valid replacement with the
  same timestamp changes them; restoring the original shader restores its
  pixels; the replacement replays exactly on the legacy path.

The windowed path was explicitly skipped: this build uses `VKMIN_NO_PLATFORM`
and no display/Xvfb was available. Sanitizers and the hosted Linux CI job were
not executed. Neither human API-usability acceptance test has been performed.

## Environment and reproduction

- Windows, w64devkit 2.9.1, GCC 16.2.0, GNU Make 4.4.1.
- Vulkan SDK 1.4.357.0 compiler, SPIR-V tools and validation layers.
- Mesa 26.2.0 Windows lavapipe, LLVM 22.1.8, advertised Vulkan 1.4.354.
- Cppcheck 2.21.0, built from its upstream source tag.

The CPU device reports host image copy, maintenance5, push descriptors,
pipeline robustness, robust buffer access 2, scalar block layout, buffer device
addresses, descriptor indexing and indirect draw count. Both Vulkan paths were
exercised; modern support was not skipped.

With the dependencies already installed in the documented local locations:

```powershell
./tools/test-windows.ps1 -Build build/verified
```

The script accepts `-Mesa` and `-Cppcheck` directory overrides. It requires the
user's `w64devkit/bin` and `VULKAN_SDK`; it neither installs software globally
nor changes persistent environment variables. It generates a local ICD manifest
with an absolute DLL path because the w64devkit shell normalizes environment
paths and the Windows loader failed to resolve the distribution's relative DLL
path through that form. This was a reproducible loader failure, not a shortage
of host memory despite the returned `VK_ERROR_OUT_OF_HOST_MEMORY`.

Mesa came from the [Mesa Windows distribution's 26.2.0 release](https://github.com/pal1000/mesa-dist-win/releases/tag/26.2.0).
Downloaded archive SHA-256:
`dcb2719ef346dab5b609fcb193a5f13cfc4b0502e3f4de1ad43d349477402f47`.
Cppcheck came from [its 2.21.0 source tag](https://github.com/cppcheck-opensource/cppcheck/tree/2.21.0),
built with w64devkit's `make -j4 CXX=g++ FILESDIR=cfg` in that checkout.
Dependencies and generated build products are ignored, not vendored into core.

## Defects addressed

1. **Journal parsing trusted file-controlled bounds.** Relocation now receives
   the payload length and validates offsets, kinds and address ranges. Record
   headers, payload sizes and resource descriptions have boundary checks;
   truncation is distinguished from clean EOF. Writer limits match reader
   limits and exceeding the relocation limit fails instead of silently dropping
   relocations. Framing errors return false and the replay example reports a
   failing exit status. This is not a claim that arbitrary hostile SPIR-V or
   all invalid API sequences are safely sandboxed.
2. **Push reflection guessed layouts and trusted operand lengths.** The new
   bounded reader honours matrix/array strides and row-major members, bounds
   recursion and IDs, rejects unsupported layouts, and distinguishes no push
   block from a parse error. Tests include shared C layouts, an 80-byte padded
   matrix/array layout, and malformed instructions. `spirv-val` remains the
   separate semantic validator; this reader is deliberately not one.
3. **Repeated frame queries consumed input.** An already offered frame is
   stable until `frame_begin`; reaching a demo's frame limit is also stable.
   Demo records must start at zero and be contiguous. Input is gathered by
   `running` and returned by `frame_begin`, once per offered frame.
4. **Hot reload destroyed working state before replacement succeeded.** Shader
   spans needed for future reloads are owned. Candidate creation precedes the
   wait/swap/destruction; rejected reads/layouts retain the last successful
   timestamps so retry works. Journal v4 adds pipeline replacement; v3 remains
   readable. Pipeline handles retain their identity across a reload.
5. **KTX2 index reads could pass the file end.** Level-index size, dimensions,
   seeks and overflow-safe payload bounds are checked before accessing data.
   The allocation and read failures are separate explicit branches.
6. **Screen quads declared attachments their pass did not have.** The current
   validation layer caught this in all five games and the overlay. Two tiny
   entry points share the same quad fragment code. World quads retain the
   declared, masked MRT outputs; screen quads declare only colour.
7. **Orthographic outlines used reciprocal perspective depth.** They now invert
   the actual orthographic projection. PCF taps are clamped to half a texel
   inside their own atlas tile, and samples outside the near plane are lit.
8. **Final cull comparisons could remain pending.** `vkr_finish` waits through
   `vkmin_wait` and checks all remaining slots. Corridor and RTS use it before
   reporting the final mismatch count.
9. **Heightfield sizing could wrap before allocation.** Sizing uses wide
   arithmetic with index/allocation limits; non-finite or negative scale inputs
   are rejected. Tests cover partial chunks, bounds, winding, oversized grids
   and invalid scales. The output writer's state contract now says it writes
   outputs; callers must allocate the returned counts.

The renderer shading enum was removed. `vkr_desc.fs` selects the user's compiled
fragment shader directly; an empty span selects the supplied PBR example. The
two cel games demonstrate the same shader-composition route. GLSL library
functions remain the lighting API.

Newer glslang emitted `DemoteToHelperInvocation` for `discard` without vkmin
enabling that optional Vulkan feature. Targeting SPIR-V 1.5 retains `OpKill`
and the existing feature requirements. Shader validity alone had not exposed
this; runtime validation did. The single-header check retains its prior warning
set because it incorporates third-party stb implementation code; first-party
sources receive the full analysis policy separately.

## Golden investigation and release limits

The Corridor already differed from the committed goldens before the outline
and shadow changes. Its frame 240 before/after images compare at tolerance
zero on this machine. CPU/GPU cull, sync and Vulkan-path comparisons also agree.
The rendered game and Corridor images were inspected, including difference
images. This localizes the problem but does not establish whether the historical
differences come from the older compiler/driver environment or stale goldens.
Reproduce the original Linux environment before changing accepted images or
raising tolerances. Raw logs and diffs remain available for that investigation.

At this earlier verification, the v0.4 gaps included fixed-tick input edge consumption and render
interpolation, canonical game-state checks, full gameplay-journal coverage,
distinct idle/run/jump clips, a second grading consumer, and explicit evidence
for the fused post/forward-normal/2D LUT-strip deviations. The top-down example's
floor chunks and additional sun also differ from the literal brief. These gaps
are recorded in the upgrade plan rather than described as completed features.

| Primitive | Existing consumers |
| --- | --- |
| Pure camera maths | All five games |
| GPU-culled instancing | Shooter, RTS, top-down, platformer, anime |
| Quad batcher | All five: particles, sprites, health bars, parallax and HUD |
| Skinning | Shooter, platformer, anime |
| Shadow atlas | All five |
| GLSL lighting library | All five |
| ID picking | RTS and top-down |
| Heightfield | RTS and top-down |
| Post stack | Platformer and anime; grading had one consumer before the later demo pass |

Physical counts including comments/blanks: core `src/*.c,h`, excluding baked
font and stb bridge, **5,868**; public header **273**; shader library **262**.
All five individual game sources remain below 500 lines. The v0.4 core and
library budgets hold; the earlier v0.3 2,500-line target was exceeded.

Local evidence: `build/plan-audit/baseline-test.log`, `improvement-test.log`,
`verified-test.log`, `reload-test.log`, `heightfield-check.log`, and
`baseline-images/`; final per-case outputs and diffs are in `tests/out/`.
