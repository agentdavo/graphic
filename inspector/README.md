# vkmin Inspector

Offline capture workbench. Open `inspector/index.html` in a browser, or build
the same viewer as a single file with no external references:

```sh
python tools/build_inspector.py --viewer-only --out vkmin-inspector.html
```

**Open capture A** selects a directory containing `capture.json`, `events.tsv`,
and checkpoint folders.

Build a capture with `tools/inspect_frame.py`, which drives `build/replay`
once per checkpoint; see [Determinism](../README.md#determinism).
The directory picker requires a browser supporting `webkitdirectory`.
No server, network requests, dependencies or native application connection are
needed to use the viewer. The capture directory is read only.

To make a single portable HTML file with its raw attachments embedded:

```powershell
python tools/build_inspector.py CAPTURE_DIRECTORY --out REPORT.html
python tools/build_inspector.py GOOD_CAPTURE --compare DEFECT_CAPTURE --schema inspector/consumer-schema.json --out comparison.html
```

The packager uses Python's standard library and deduplicates identical raw
attachments. Large captures can still produce large reports; use the directory
picker to avoid embedding them. Decoded attachments are cached as they are used.
Each attachment is limited to 16 million pixels to bound display allocations.

For a new native capture, add `--workbench` to `tools/inspect_frame.py`. This
retains the existing gallery at `index.html` and adds `inspector.html`:

```powershell
python tools/inspect_frame.py RECORDING.vkj --replay REPLAY_EXECUTABLE --frame 1350 --passes --out NEW_CAPTURE_DIRECTORY --workbench
```

## Working with a capture

- The event tree groups recorded calls by frame and pass. Search matches event
  numbers, operation names and trace details. Dots identify captured events.
- Previous/Next and arrow keys move between actual captured checkpoints.
- Selecting an uncaptured event clears the viewport and supplies a capture
  command. It does not imply that a nearby checkpoint is that event's output.
- Select an attachment; use Fit, 1:1, wheel zoom and drag pan. Hover reads a raw
  pixel; click pins it. The X/Y inputs select exact coordinates. A green outline
  marks the selected pixel. Channel controls affect the display; HDR EV applies
  only to floating-point HDR color previews.
- Pixel values and hex bytes always come from raw data. RGBA/BGRA readouts use
  logical RGBA byte values; RG16 reports the original two uint16 values, while
  its RGB preview decodes octahedral normals. R32 IDs use hashed preview colors.
  Depth is shown in device 0–1 space. Preview transforms are not measurements.
- Resource and metric panels show recorded data, including generation numbers
  when the dump supplies them. Missing device/build metadata remains unknown.
  Inspection metrics include diagnostic overhead and are not normal-frame
  benchmarks; CPU and GPU elapsed times overlap.

## Comparing

Open B, then choose A, B, wipe or raw difference. Image coordinates, zoom,
exposure and channel settings are shared. Raw difference is an exact bytewise
changed-texel mask, including alpha and floating-point bit representations;
it is not a perceptual comparison. Enable **Numeric tolerance** to compare decoded
source components using `abs(a-b) <= absolute + relative * max(abs(a), abs(b))`.
Integer formats use integer source units (RGBA8 is 0–255); floats use their raw
units. Alpha participates. Signed zero matches numerically. Identical bytes
always match; changed texels containing NaNs fail numerical comparison, and
infinities match only equal infinities. Display exposure does not affect either
comparison. Exact bytes remain the default.

**Find first difference** scans checkpoints in recorded order, validates
resource layouts, then selects and centres the first changed pixel. It reports
the count of changed texels in that attachment. To narrow a bad pass to a draw,
capture individual events inside that pass.

Frames and checkpoint selections must match. Comparing different journal
hashes additionally requires explicitly checking **Compare different
recordings**. This establishes permission to compare a controlled change, not
proof that unrelated events or process-specific GPU addresses correspond.

## Declaring push layouts

Load a JSON schema with the toolbar, place `push-schema.json` in the capture
root, or pass `--schema` when packaging. Keys are pipeline labels or recorded
pipeline IDs. Each value is an array of fields:

```json
{"sample texture": [{"name": "gain", "offset": 12, "type": "u32"}]}
```

Supported types are `u32`, `i32`, `f32`, `u64`, `f64`, with optional `count`.
Offsets and lengths are checked against the actual push block. The supplied
consumer schema matched a package-consumer test no longer in the tree; it is
not an Omega schema. Raw push bytes do not describe their own types. GPU addresses are
process-specific; candidate links identify logical resources and byte offsets.
They do not prove that a shader dereferenced those values.

## Verification

```powershell
python -m unittest discover -s tools -p test_inspector.py
```

The new tests require Node for the shared decoder and application-state tests;
Node is not required to package or use reports. The application tests use a
small DOM adapter, not a browser or layout renderer. They cover embedded boot,
folder loading, comparison consent, exact changed pixels, declared gain values,
uncaptured event clearing, malformed images and coordinate bounds. Packaging
tests cover hostile labels, escaped paths and missing or unsupported images.

The implementation was also exercised with the existing real consumer good/
defect captures and a fresh six-checkpoint Omega frame 1350 capture. The
consumer's first divergence is pixel (32, 0), with gain 255 versus 128. Omega's
final replay attachment matches the directly rendered image exactly.

The viewer inspects recorded events and resource snapshots. It does not step
shader invocations, infer all bindless dependencies, or generate snapshots by
itself. The native replay CLI remains responsible for capturing new events.

## Buffers and resource history

New native captures include `buffers.tsv`, `buffers.json` and raw files for live
device buffers and the submitted frame's ring region. GPU work is completed
before copying to temporary coherent staging memory. This runs only during
inspection; ordinary rendering does not allocate these readbacks. Individual
resources larger than 64 MiB are listed as omitted, never truncated. Old captures
remain readable and simply have no buffer snapshots.

Select a buffer, byte offset, row count (1–256), type and optional byte stride,
then **Decode buffer**. Types include u32/i32/f32/u64/f64 and Vulkan indexed
indirect commands, including signed vertex offsets. A and compatible B snapshots
are decoded side by side after comparison consent. Values are little-endian;
64-bit integers are printed without conversion to JavaScript Number.

Schemas can declare `$buffers`, keyed by label or full resource ID:

```json
{"$buffers":{"instances":[
  {"name":"position","offset":0,"type":"f32","count":3},
  {"name":"material","offset":12,"type":"u32"}
]}}
```

Choose **Declared layout** and supply the structure stride if padding extends
past its last field. `omega-schema.json` describes the current Omega push,
24-byte vertex and 128-byte scene layouts; package it using `--schema` or load
it in the toolbar. Push links automatically select that layout when available.
Indirect-draw links select the command/count/index buffer and recorded offset;
the index link displays raw u32 words, not an inferred index format.

Resource history links events by full resource identity, including handle
generation. Uploads, fills, frees, explicit indirect reads and transitions are
recorded evidence. Draw attachments are possible writes: coverage, depth tests
and shader behavior are not inferred. Aligned push values that fall into a live
buffer/ring address range are **address candidates**, not declared dependencies.
Shader access through bindless indices remains unknown. Attachment selection and
buffer decoding select their corresponding history; click a history row to
navigate to its event. Uncaptured events still clear the snapshot.

## Automatic event narrowing

After **Find first difference**, the viewer supplies a PowerShell command for
the native tool (replace `NEW_DIRECTORY`, and capture paths for old captures):

```powershell
python tools/narrow_capture.py GOOD_CAPTURE DEFECT_CAPTURE --out NEW_DIRECTORY
```

The tool finds the first differing supplied checkpoint and replays corresponding
events **sequentially from frame begin** through that checkpoint, stopping at the
first observable attachment difference. Starting earlier than the last matching
checkpoint catches temporary differences that were overwritten. Every probe
starts a fresh replay and retains preceding frames and the frame's host uploads.
It writes paired capture directories, `result.json` and `inspector.html`.

This can be expensive: each event requires two native replays and raw snapshots.
It establishes the first observable differing event, not the causal shader
instruction. It requires matching frame/checkpoint selections and event
numbers/opcodes; corresponding recordings are a caller precondition. Resource
layout/identity differences fail explicitly. If the supplied checkpoints all
match, it reports that limited result without probing intermediate events.

Use `--numeric --absolute 0.001 --relative 0.0001` for numeric comparisons.
`--replay-a` and `--replay-b` select different builds or supply executables for
old captures. Otherwise the captured executable path and hash must still match;
journal hashes are always checked. The offline browser never launches processes.

## Environment metadata

New `capture.json` files identify the replay executable by SHA-256 and path,
OS, UTC capture time, GPU/device/vendor IDs, Vulkan API and raw driver versions,
driver name/info and Debug/Release validation configuration. This describes the
**replay environment**; it does not invent the original recorder's environment.
Image manifests now include full IDs to detect reuse when comparing snapshots.
Timing panels continue to identify inspection overhead.

The automated suite also covers buffer packaging/path rejection, typed layouts,
A/B buffer readouts, tolerance controls, event correspondence and sequential
narrowing with a transient divergence. Native testing additionally exercised an
Omega capture on Intel Vulkan and an in-pass checkpoint with a Debug replay.

## MSAA captures

Version-7 rendering journals record sample counts, alpha-to-coverage, explicit
resolve targets, depth-resolve modes and EXT raster sample counts. Pass history
shows fixed-function resolve writes at pass end. Image metadata includes
`samples`; pipeline and pass details retain the recorded MSAA configuration.

Raw readouts and comparisons cover single-sample images, including explicit
resolve targets and EXT single-sample outputs. Vulkan does not permit direct
image-to-buffer copies of multisample storage. Such images remain listed with
an explicit unavailable message; comparisons exclude their unresolved samples.
No individual-sample equality is claimed. Choose the resolve attachment to
inspect the actual output consumed by later passes.
