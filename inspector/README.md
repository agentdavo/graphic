# vkmin Inspector

Offline capture workbench. Open `inspector/index.html` in a browser.
**Open capture A** selects a directory containing `capture.json`, `events.tsv`,
and checkpoint folders.

> The tooling that produced those capture directories — `inspect_frame.py`,
> `build_inspector.py` and the replay driver they invoked — lived in `tools/`
> and `examples/` and is not currently in the tree. vkmin still writes the raw
> halves itself (`--events FILE` and `--inspect-dir DIR`, both from the replay
> path); what is missing is the step that packages them into a capture this
> viewer can open. The rest of this file describes the viewer as built.
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
it is not a perceptual or numeric-tolerance comparison.

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
consumer schema matches `tests/package_gpu/consumer.c`; it is not an Omega
schema. Raw push bytes do not describe their own types. GPU addresses are
process-specific and referenced ring/buffer contents are not decoded here.

## Verification

```powershell
python tools/test_inspector.py
python -m unittest discover -s tools -p test_inspection.py
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
