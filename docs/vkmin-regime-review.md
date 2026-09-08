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
- Device-free suite: 60 checks; sound suite: 28 checks; inspector suite: 10 tests.
- GPU regression covers supported 1x/2x/4x/8x counts, MRT and depth resolve, alpha-to-coverage, helper cleanup/fallback, vertex sampling, exact legacy/modern replay, sparse timestamp reuse, wrapper failure paths, CLI precedence and malformed inputs. Debug additionally checks Khronos rejection of invalid Vulkan configurations.
- Omega frame 720 at 1280x720 and 8x MSAA remains byte-identical to the pre-review image. A saved version-7 multi-frame journal replays frame 300 byte-identically. Current journals also exercise replay with different ring sizes and naive synchronization.
- Standalone Clang analysis: `clang --analyze -std=c11 -DVKMIN_NO_PLATFORM -Isrc -isystem C:/VulkanSDK/1.4.357.0/include src/vkmin.c`.
- Source budgets pass: core 3669/4200 code lines, public header 266/300. The core is 53 code lines smaller than the start of this review.

## Boundaries and remaining limitations

This is a focused correction of the existing implementation, not a claim that every practice in the regime is automated. No debugger stepping session or resize interaction was performed. Hardware verification used Intel Vulkan 1.4; the legacy execution path was tested there, not on a separate Vulkan 1.3 device. Counts 16x/32x/64x and `VK_EXT_multisampled_render_to_single_sampled` are unsupported on this device and explicitly skipped.

Address relocation still recognizes aligned issued address bases heuristically. An integer equal to an address is ambiguous, and interior or unaligned addresses are not recognized; use separately recorded offsets. Legacy payloads retain native ABI/endianness constraints. Replay framing is bounded, but semantically invalid commands can still fail wrapper invariants or Vulkan validation: journal replay is not a sandbox for untrusted GPU commands.

Further restructuring should follow a measured need. The existing explicit context, bounded storage and parallel legacy/modern paths remain appropriate; splitting them merely to shorten functions would add indirection without improving correctness.
