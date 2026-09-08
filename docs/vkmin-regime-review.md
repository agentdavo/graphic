# vkmin review against the Carmack Regime

Reviewed all of `src/vkmin.h` and `src/vkmin.c`, including initialization, feature negotiation, memory and resource lifetime, pipelines, transfers, frame synchronization, drawing, inspection and journal replay. Supporting pure logic, journal framing and regression code were checked where those paths depend on them.

## Changes

- Removed duplicate Vulkan validation from shipping MSAA/pass/draw code: image usages, sample/format/resolve compatibility and attachment matching belong to Khronos validation. Debug builds retain that layer. Wrapper handle, logical allocation and C/GLSL transport checks remain.
- Preserved `vkmin_target`: it performs useful capability negotiation and coordinated allocation, resolve setup and cleanup requested for Omega. Low-level image/pipeline settings remain exact; the helper resolves its configuration to ordinary recorded operations.
- Made logical range checks overflow-safe and tested boundary arithmetic. Ring operations now respect the current frame region; indirect command/count access respects logical buffer allocations inside the shared Vulkan buffer.
- Checked push-data presence before indirect draw copying and reused the checked pipeline slot when binding.
- Fixed sparse timestamp collection: writing indices 0 and 7 previously aborted on the third frame with `VK_NOT_READY`. Availability is now collected per query; missing values are NaN, and index 0 defines the time baseline.
- Included vertex shader access in sampled-image transitions, exercised by a vertex-stage texture sample.
- Honoured descriptor defaults for vsync, synchronization and readback, with explicit cvar assignments taking precedence in either direction. Rejected malformed numeric CLI input before device creation.
- Fixed `VKMIN_BYTES` to accept scalar and struct objects as its documentation promised, while continuing to accept arrays.
- Checked journal seek failures and expressed relocation reads in record units. Added complete/missing/truncated relocation tests. Clang now reports no diagnostics on the core analysis.
- Removed duplicate SPIR-V checks and redundant per-frame backbuffer rebinding. Corrected comments about slot reuse, resize, robustness, optional depth and relocation limitations. Updated the policy file's obsolete claim that no tests exist.

## Verification

- GCC Debug and RelWithDebInfo builds use the project's warnings-as-errors and `-fanalyzer` settings.
- Device-free suite: 66 checks; sound suite: 28 checks; inspector suite: 10 tests.
- GPU regression covers supported 1x/2x/4x/8x counts, MRT and depth resolve, alpha-to-coverage, helper cleanup/fallback, vertex sampling, exact legacy/modern replay, sparse timestamp reuse, wrapper failure paths, CLI precedence and malformed inputs. Debug additionally checks Khronos rejection of invalid Vulkan configurations.
- Omega frame 720 at 1280x720 and 8x MSAA remains byte-identical to the pre-review image. A saved version-7 multi-frame journal replays frame 300 byte-identically. Current journals also exercise replay with different ring sizes and naive synchronization.
- Standalone Clang analysis: `clang --analyze -std=c11 -DVKMIN_NO_PLATFORM -Isrc -isystem C:/VulkanSDK/1.4.357.0/include src/vkmin.c`.
- Source budgets pass: core 3815/4200 code lines, public header 272/300. Private GPU headers now include the pure-logic and arena headers in their 601/900 count.

## Follow-up: disposition of every boundary

- **Debugger and window lifecycle:** performed real GDB stepping through frame begin/end, inspected locals and call stacks, and traced 94 distinct functions during a complete initialization/frame/shutdown cycle. Automated three resizes, minimize, restore and close on an isolated Xvfb/Openbox display. Actual geometry, iconic state and three swapchain recreation waits were verified. These are repeatable scripts and CI steps; they do not imply that one frame visits every branch.
- **Vulkan versions and optional MSAA:** added software-driver coverage alongside Intel Vulkan 1.4. Current Mesa covers 1x/4x/8x plus both alpha-to-coverage settings with `VK_EXT_multisampled_render_to_single_sampled`. Actual Mesa 24.0.5 reports Vulkan 1.3 and replays the supported 1x/4x and extension captures with identical images, using a current validation layer. Forced legacy/modern paths remain a separate comparison. A version-reporting environment override was not accepted as 1.3 evidence.
- **16x/32x/64x:** exercised the complete bitmask/fallback algorithm through 64x in pure tests and added hard `--require-samples`/`--require-single` coverage gates. None of the available drivers supports 16x/32x/64x for this fixture. Actual rendering at those counts remains unverified; reports identify missing coverage rather than counting a fallback as success.
- **Ambiguous relocation:** replaced default scanning with explicit public address layouts for pipeline pushes, initial buffers, typed uploads and typed ring allocations. Interior and unaligned buffer/ring pointers relocate; equal integer values remain unchanged. Metadata is bounded and copied synchronously, and replay can itself be re-recorded. Omega and render supply the layouts. The old heuristic path remains explicitly selectable for comparison/migration.
- **Native ABI/endianness:** version 9 rejects incompatible native-layout fingerprints before device creation. Typed captures replayed Windows-to-Linux and Linux-to-Windows byte-for-byte. This guards compatibility; it cannot reconstruct an unknown legacy ABI or impose a canonical layout on opaque shader data. Versions 3–8 remain readable on compatible runtimes, with explicit legacy admission in the offline tool.
- **Malformed and unfamiliar captures:** added bounded, device-free admission, including eight unit tests, all partial truncations and 1000 deterministic mutations. Native replay checks opcode/header shape before inspection, relocation references and wrapper-owned frame/pass/handle/range contracts before issuing affected calls. Nine malformed command cases exit cleanly without a wrapper abort or validation error. Vulkan semantics remain Khronos validation's responsibility.
- **Execution containment:** added a software-only Podman runner with no host GPU or network, read-only input/root, unprivileged UID, dropped capabilities, memory/CPU/process/storage limits and deadlines. Seven Vulkan 1.4 cases passed exact-image and inspected-policy checks; an oversized replay was terminated by enforced limits. The Vulkan 1.3 run covers the six compatible cases. Native GPU replay remains for trusted captures; a container still depends on its runtime/kernel.
- **Immediate uploads:** validation across driver/layer versions exposed the need for an explicit GPU timeline dependency between immediate submissions. Added that dependency while retaining the host staging wait; repeated typed uploads and the MSAA suite pass synchronization validation.

The follow-up also rechecked the outdoor/cel scene at frames 299–301: frame 300
replays byte-identically. Windows Debug and optimized builds, Linux Debug,
GCC analysis, independent Clang analysis and all CTest checks pass.

Local evidence is under `build/boundaries/`: `debug-cycle.log`,
`debug-functions.json`, `window-glfw-ready/result.json`,
`windows-msaa-complete/result.json`, `lavapipe-msaa-verified/result.json`,
`relocation-final/`, `windows-final/`, `scene-final/`, `replay-errors/`,
`isolated-final/result.json` and `isolated-13-final/result.json`.
CI regenerates evidence and uploads its boundary-coverage artifact.
See [journal contracts and isolated replay](replay.md) for commands and limits.

The restructuring item is a design constraint, not unfinished implementation:
retain explicit state, bounded storage and the comparison paths. Further splits
need a measured correctness or performance reason. Hardware coverage at high
counts, unknown legacy layouts and kernel-level containment guarantees cannot
be manufactured by restructuring `vkmin.h/c`.
