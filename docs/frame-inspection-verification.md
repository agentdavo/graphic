# Frame inspection verification — 6 September 2026

Environment: Windows, MSYS2 UCRT GCC and Clang, Vulkan SDK 1.4.357.0,
repository lavapipe (LLVM 22.1.8). Artifacts are under `build/inspection/`.

## Executed checks

- The complete headless `make test` finished with **114 checks, 0 failures**,
  including game/Corridor/Valley/OMEGA goldens, reference paths, temporal history,
  shared audio/video journals, shader validation and repository-wide cppcheck.
  Log: `build/inspection-full-test-final.log`.
- Final focused cppcheck on `src/vkmin.c` and the inspection fixture passed with
  the repository's platform configuration. The final standalone amalgamation
  rebuilt successfully. The three Python utility tests pass, including a
  deliberately altered texel and a truncated raw capture.
- GCC builds the complete headless project with the normal warnings-as-errors
  and `-fanalyzer` flags. Clang builds and runs the inspection regression.
- The two-draw fixture uses separate frame-ring allocations for each triangle.
  On both Vulkan paths, a cutoff after draw one preserves the left triangle
  exactly and leaves the right half clear. A cutoff after draw two equals the
  complete raw backbuffer. The late frame-end ring payload is necessary for
  this test to pass.
- Event numbers are contiguous and pass labels survive a version-5 journal.
  Missing events, events outside a frame, negative/overflowing event numbers,
  unwritable trace destinations and truncated frame payloads are rejected.
- HDR, depth, BGRA and octahedral-normal preview conversion tests pass.
- A four-frame fixture reports four completed GPU frames, including both final
  frame slots, with three frame-interval samples. Structured metrics distinguish
  inspected captures from ordinary rendering.
- An existing version-4 Corridor journal replays and exports successfully.
- A new Corridor frame 240 matches the committed golden at tolerance zero.
  Its five checkpoints (four explicit pass ends and completed frame) match
  between legacy and modern paths in **every exported raw image**, not only
  the backbuffer. See `build/inspection/comparison.json`.
- Backbuffer, normal and shadow-atlas PNGs were visually inspected. Browser
  policy rejects local `file:` URLs, so automated interactive browser testing
  of the HTML controls was not performed.

## Historical golden investigation

The older README incorrectly described the six historical Corridor mismatches
as open. The [later verification note](verification-2026-09-05.md#corridor-golden-resolution--5-september-2026-evening)
already contains a reproduction using the original golden-producing commits
and the current compiler/driver. That investigation attributed the major
differences to environment-specific pixels and the small residual differences
to documented shadow/outline changes, then updated the six goldens.

This work corrects the README and checks the current baseline. It does not
replace any golden images. The journal/checkpoint comparison tool can locate
the first divergent stage when a new failure appears; it cannot reconstruct
intermediate buffers from an old PNG alone.

## Limits

Inspection is a developer tool, not a portable capture format or a sandbox for
hostile files. It submits the prefix of a frame, reads the remaining ring
payload, and exports used attachments through temporary staging buffers.
Indirect draws remain single events. Shader-invocation stepping, storage-buffer
viewers, mip selection and GPU crash recovery are outside this change.

Whole-attachment raw comparisons can expose uninitialized pixels outside a
rendered viewport. Pacing measurements during capture include diagnostic
overhead; ordinary performance runs should omit inspection and PNG output.
