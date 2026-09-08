# Journal contracts and isolated replay

Version 9 records explicitly declared GPU-address fields. Ordinary bytes are
never scanned for possible addresses by default. This matters when an integer
happens to equal a device address, or a pointer references the interior of an
allocation or occupies an unaligned field.

## Declare addresses at the producer

```c
typedef struct { uint64_t vertices; uint64_t integer; } Push;
const uint32_t fields[] = {offsetof(Push, vertices)};
vkmin_pipeline_desc desc = {
    /* shaders and other pipeline state */
    .push_size = sizeof(Push),
    .push_addresses = {fields, 1}
};
```

Use the same `vkmin_address_layout` for initial buffer data (`.addresses`),
subsequent `vkmin_buffer_upload_typed` calls, and `vkmin_ring_alloc_typed`
allocations. Offsets are relative to that payload or allocation, in bytes,
strictly increasing and non-overlapping. Each identifies eight bytes containing
a GPU address. Layout metadata is consumed or copied synchronously; pipeline
metadata survives hot reload. Fill ring fields before ending the frame.

Declare nested address fields in their own buffer/ring payload, independently
of the pointer that reaches them. Nonzero addresses must reference a live buffer
or the current frame's allocated ring region. Null stays null. Buffer references
encode the handle and a 32-bit interior offset; ring references are frame-relative.
There are at most 4096 relocations per record/frame and 16 push-address fields.

Existing untyped upload/ring APIs and zero-initialized layouts mean **plain
bytes**, including when recording. Migrate every payload that contains GPU
addresses. `--record-heuristic` retains the previous scanner for comparison or
temporary migration: it recognizes aligned issued bases, cannot distinguish
an equal integer, and cannot represent interior/unaligned addresses. It is not
the default. Omega and the render layer already use explicit metadata.

## Compatibility and admission

Version 9 fingerprints native record sizes, padding offsets, input layout,
endianness and floating-point representation. An incompatible fingerprint is
rejected before replay creates a device. Windows and Linux x64 captures were
replayed in both directions with identical fixture images. This is compatible
native serialization, not a canonical cross-ABI format: shader payloads remain
opaque application bytes. Their shader/C layout contract still belongs to the
caller.

Native replay reads versions 3–8 as well. Unknown legacy ABIs cannot be safely
converted without the original layout/schema. Versions 3–6 also require the
original ring-size setting for multi-frame ring addresses. Retain a compatible
producer/runtime for these captures. Offline admission requires explicit
`--allow-legacy` for a known-compatible old capture; it does not repair one.

```sh
python tools/check_journal.py capture.vkj
python tools/test_journal.py
```

Admission never loads Vulkan. It checks bounded framing, opcode/header shapes,
the ABI fingerprint, relocation identities/ranges, and basic frame/buffer state.
Default budgets are 1 GiB per file, 128 MiB per payload and one million records.
Shared audio/video containers are extracted with the same bounded file budget.
The native reader additionally rejects malformed wrapper state and logical
transport ranges before issuing the affected call. Khronos validation remains
responsible for Vulkan semantics. Passing admission does not make shader execution
safe; direct native replay requires trusted captures.

## Execute an unfamiliar capture in isolation

With a running local Podman engine:

```sh
podman build -f tools/Containerfile.replay -t localhost/vkmin-replay:9 .
python tools/replay_isolated.py capture.vkj --frame 0 --out new-result.png
```

The runner first admits the capture, then uses software Vulkan inside a
restricted container. It exposes only the input file, read-only: no host GPU,
network, home directory or writable host mount. Execution uses an unprivileged
container UID, no capabilities, no privilege escalation, a read-only root,
1 GiB memory, two CPUs, 256 processes, bounded temporary storage/logs, and CPU
and wall deadlines. A completed PNG is copied out while the container is live;
the container is removed on success or failure. Adjacent log and policy JSON
files identify the image ID and actual inspected restrictions.

The default execution deadline is 30 seconds (`--timeout`, at most 300).
The replay arena configuration is deliberately bounded: 64 MiB each for buffers
and images, 8 MiB for the ring. Larger valid captures may fail those limits.
Unsupported device features also fail; exact journals do not silently substitute
sample counts. Containers depend on the runtime and its kernel; isolation does
not claim immunity to vulnerabilities in either.

## Repeat coverage

`ctest --test-dir build --output-on-failure` runs arithmetic, sound, offline
admission and source budgets. `check_relocation.py` verifies pointer/integer
separation, interior and unaligned buffer/ring addresses, multi-frame replay,
different ring sizes and re-recording. `check_replay_errors.py` tests malformed
wrapper commands for clean failure without GPU-validation errors.

`check_msaa.py --require-single --require-samples 1 4` requires actual extension
and sample coverage. Unavailable counts remain explicit skips unless required.
The current software driver covers 1x/4x/8x and the optional extension; the Intel
device covers 1x/2x/4x/8x. Negotiation through 64x has pure tests, but physical
16x/32x/64x rendering still needs hardware supporting those counts.

```sh
podman build -f tools/Containerfile.replay-1.3 -t localhost/vkmin-replay:1.3 .
python tools/check_isolated.py --typed build/typed-check --captures build/msaa-check --out new-13-check --image localhost/vkmin-replay:1.3 --api 1.3 --samples 1 4
```

The 1.3 image combines the actual Mesa 24.0.5 driver with the current loader and
validation layer. It checks the physical API version in execution logs, not an
environment override. This driver supports 1x/4x for the fixture; other captures
are explicitly excluded in the report. The current image tests 8x as well.
`check_isolated.py --limit-test` also verifies termination of an oversized replay.
CI runs these paths plus debugger stepping/tracing and isolated window interaction.
