# Inspecting a vkmin frame

The journal is the source of truth. Inspection replays its recorded GPU calls,
assigns each record a **one-based global event number**, and can submit just the
prefix ending at a chosen event. Event numbers are stable for the same journal;
they are not stable across different recordings or builds.

## Capture and open a report

From the repository root on this Windows machine:

```powershell
./tools/build-msys2.ps1 -Headless -Target all
$env:VK_DRIVER_FILES = (Resolve-Path build/deps/lavapipe.json).Path
./build/omega-ucrt-headless/corridor.exe --profile lavapipe --frame 240 --record build/corridor.vkj
python tools/inspect_frame.py build/corridor.vkj --replay build/omega-ucrt-headless/ex_07_replay.exe --frame 240 --passes --out build/corridor-inspection
```

Open `build/corridor-inspection/index.html`. Previous/Next and the arrow keys
step through pass-end checkpoints. Each checkpoint shows every attachment used
so far: colour, depth, HDR, normals, IDs and shadow atlas as applicable.
The report is a standalone local HTML file with local PNGs, without a server or
Python package dependencies. The output directory must be new so a failed run
cannot inherit stale captures.

Each checkpoint starts a fresh replay from the beginning, retaining temporal
history. This deliberately favours correctness over capture speed. Large
journals, many passes and large shadow atlases take time and disk space.
The default clear pass is implicit in `frame_begin`; a frame using only that
pass has a complete-frame checkpoint. To inspect its draws use `--event`.

## Step inside a pass

Find an event in `events.tsv`, then capture it:

```powershell
python tools/inspect_frame.py build/corridor.vkj --replay build/omega-ucrt-headless/ex_07_replay.exe --frame 240 --event 812 --out build/corridor-event-812
```

812 is an illustration; use an actual draw/dispatch/pass event from your trace.
The underlying executable also accepts these flags directly:

```text
ex_07_replay --replay FILE --events events.tsv
ex_07_replay --replay FILE --frame N --stop-after-event E --inspect-dir EXISTING_DIRECTORY
```

The chosen event executes. Later GPU operations are omitted; replay still reads
the rest of that frame, recreates later ring allocations, validates the ending
ring payload, and copies its relocated bytes. It then closes any open rendering
pass and submits normally. This matters because ring contents are journalled at
**frame end**, after draws referencing them. A truncated ending payload fails
instead of submitting a capture with missing uploads. Events outside a frame,
missing events and missing capture frames fail explicitly.

The frame number must match the chosen event. Earlier frames execute normally.
The inspection ends after submitting the selected frame, so later records are
not validated. An indirect draw is one event: stepping individual generated
draw commands or shader invocations is not supported.

New journals use version 5 and retain pass labels. Versions 3 and 4 still replay;
their pass attachment IDs are available but their original pass labels were not
recorded. The trace also lists resource creation, sizes, pipeline names, push
sizes, uploads, barriers and draw/dispatch arguments. It is an execution trace,
not a disassembler for push blocks or shader code.

## Buffers and comparison

`images.tsv` describes tightly packed, mip-zero `.raw` files using `vkmin_format`
numbers and native little-endian texels on the supported Windows test host.
`images.json` includes SHA-256 hashes. `resources.txt` dumps live resource slots,
generations, labels, sizes and current image uses. Only attachments used so far
are exported; other textures, storage buffers and unused allocations are not
implicitly read. Raw images include the whole attachment, including pixels
outside a pass viewport; those pixels may not have been initialized.

PNG previews are display transforms: RGBA/BGRA is made opaque, HDR uses Reinhard
and gamma 2.2, D32 shows device depth in 0–1, R32 IDs use hashed colours, and
RG16 normals are octahedrally decoded. Raw files retain exact values and alpha.
Do not use preview colours as numeric measurements.

To locate a divergence across Vulkan paths, create another report with the same
journal/frame and `--path modern`, then:

```powershell
python tools/compare_inspection.py build/corridor-inspection build/corridor-modern
```

The comparison checks layouts and raw texels, lists differing images, and reports
the first differing checkpoint, image and pixel coordinate. Exit 0 means exact
agreement; exit 1 means a difference. Comparing pass boundaries narrows the stage;
capture individual events within that stage to narrow it further.

## Timing the actual work

Every run prints host and GPU totals at shutdown. Add `--metrics build/timing.json`
for structured output. `vkmin_stats_get` exposes the counters during execution;
GPU totals include completed, collected frame slots, and shutdown collects the
last slots too. Existing renderer pass timings remain available through its stats.

| Measurement | Boundary |
|---|---|
| Host frame work | Elapsed time from frame begin through submission/presentation, excluding measured waits and window operations; includes application code between begin/end |
| Timeline wait | Timeline semaphore waits, including uploads, capture and final drain; also the explicit naive-sync device wait |
| Window | Swapchain acquire/recreation and present calls |
| Host readback | Backbuffer host copy/memcpy and opaque-alpha conversion, excluding timeline waits |
| PNG | Encoding and file writing |
| GPU rendering | First frame timestamp through scene work, before backbuffer readback |
| GPU readback copy | Timestamp interval bracketing the legacy backbuffer copy; modern/no-readback has only timestamp overhead |
| Frame interval p99 | Monotonic elapsed time between frame starts; includes work outside begin/end and capture overhead |

CPU and GPU overlap: these are not additive components of a frame interval.
Host elapsed time is not CPU cycle accounting. Initial allocation, asset cooking,
most initialization work, and diagnostic raw-export I/O are outside host frame
work. Inspection adds copies, waits and allocation: its JSON sets `inspection`
to true, and it should not be used as a performance benchmark. To measure normal
pacing, run consecutive frames without image saving or inspection; use existing
`--no-readback` when measuring the legacy path without its capture copy.

## Checks

```powershell
./tools/build-msys2.ps1 -Headless -Target inspection-test
```

This is also a dependency of `make test`. It checks late ring uploads, stopping
between two draws, full-frame equivalence, stable events and pass labels,
legacy/modern agreement, malformed requests, truncated journals and preview
decoding. Run with the repository's lavapipe driver supporting both paths.

The historical Corridor golden issue had already been resolved before this work:
see [the source-commit reproduction and resolution](verification-2026-09-05.md#corridor-golden-resolution--5-september-2026-evening).
The README's older open-failure wording was stale. This tooling provides a way to
localize future failures without replacing their goldens to make a check pass.
