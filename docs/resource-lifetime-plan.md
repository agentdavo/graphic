# Resource lifetime and configuration plan

Status: implementation plan, based on the current source. The lifetime and
configuration changes below are not implemented yet.

Keep the developer-facing contract: C11, explicit context, compiled SPIR-V,
fixed-capacity metadata and no allocator or shader-compiler framework dependency.

## What the source establishes

- `arena_alloc` advances a cursor. Buffer slots remember their ranges; image
  slots do not yet retain their memory offset and allocation size.
- `vkmin_free_buffer` and `vkmin_free_image` require a between-frame call,
  invalidate a generation handle, and wait for the whole device. Neither
  returns arena space. Texture registration also advances a monotonic counter;
  an image can have more than one registered descriptor.
- Frames and immediate submissions already share one increasing timeline.
  This is sufficient for conservative retirement without discovering every
  resource a shader might reach through a GPU address.
- Journal relocation currently rebases an arena offset, rather than identifying
  a logical allocation. Replay therefore depends on reproducing allocation
  offsets. Descriptor registration similarly expects identical numeric indices.
- Handle generations wrap after 4095. Sustained reuse must not silently make an
  ancient stale handle valid again.
- Mutable globals include the cvar table, GLFW window/scroll state, journal
  relocation scratch and image-barrier scratch. Moving cvars alone would not
  establish independent contexts.

## 1. Establish the workload and counters

Add a headless resource-lifetime test and a separate benchmark mode. Use small
explicit arenas and alternate differently sized buffers and images. Submit
work that consumes their contents, free between frames, allocate replacements,
and verify GPU readback against a CPU oracle. Run with two frames in flight,
both Vulkan paths, and the existing synchronous reference mode.

Extend existing stats with:

- per-arena live, pending-retirement, reusable and high-water bytes;
- largest reusable range, allocation failures and fragmentation information;
- pending retirement count, live/free/pending texture slots;
- device-idle calls by reason, retirement-pressure waits and elapsed wait time.

Count alignment padding consistently as reserved space; distinguish arena
reservation from requested payload bytes. Do not quietly change the meaning of
the existing `device_used` field. Document its migration or add separate fields.

First capture the current exhaustion and device-idle behavior. Keep timing
outside correctness assertions: compare medians and tail times over repeated
fixed workloads, recording driver, compiler, path and arena sizes.

## 2. Retire physical resources on the timeline

Keep free operations between frames. Free invalidates the public handle
immediately, but moves its physical allocation and Vulkan objects into a
bounded retirement queue tagged with the latest submitted timeline value.
That value conservatively covers all previous uses, including pointer-based
shader accesses. Callers must not submit future work using a freed address or
texture index; generational CPU handles cannot protect raw GPU pointers.

Poll completed timeline values at frame boundaries and before resource
allocation. Only completed records may destroy image views/images or release
memory and descriptor slots. Buffer retirement releases a subrange, never the
shared backing `VkBuffer`. Keep retirement records independent of reused public
handle slots.

If bounded metadata fills, collect first, then wait only for the oldest
relevant retirement value. If live resources occupy all capacity, fail with a
specific capacity diagnostic. Never spin or silently grow metadata.

Use the existing synchronous mode as the reference: it follows the same
retirement path but waits immediately. Routine free must make zero device-idle
calls. Keep shutdown, swapchain/presentation and explicit whole-device waits
separate: a graphics timeline does not by itself prove presentation completion.

Test pending resources with an unsignaled timeline in a small private test
fixture; do not rely on a slow GPU or sleeps to demonstrate deferred destruction.
Also cover never-submitted resources, immediate uploads, queue pressure,
shutdown with pending records, stale handles, and generation exhaustion.
Preserve the current handle encoding initially; an exhausted generation must
retire its logical slot permanently rather than wrap into a stale ID.

## 3. Make replay independent of physical reuse

This is a prerequisite for enabling reclamation in normal recorded runs.
Introduce a new journal version with logical buffer identity plus byte offset
in address relocations. Resolve each reference against the live replay handle,
checking generation, bounds, overflow and resource lifetime. Keep the existing
documented limitation on detecting pointers; arbitrary integer words must not
be silently reinterpreted as pointers.

For texture indices, the current generic payload scanner does not identify
arbitrary 32-bit texture fields. Record descriptor-slot assignment/reuse and
reserve the recorded slot during replay. If replay still has an older use of
that slot in flight, wait for its retirement before overwriting it. Do not
guess which integers in user payloads are texture indices.

Keep version 4/5 journals on their original monotonic allocation and descriptor
assignment policy. New journals may reuse resources while legacy readers are
explicitly rejected. Test record/replay with deliberately different retirement
collection schedules so equal physical offsets are not accidentally required.
Reject malformed, stale, out-of-bounds and cross-lifetime relocations.

## 4. Reclaim and coalesce arena ranges

Use an address-ordered, bounded free-range array per arena. Allocate by a
deterministic first-fit scan; split prefix/suffix ranges for alignment, and
coalesce adjacent ranges on retirement. Keep allocations immovable so issued
GPU addresses remain stable throughout their lifetime. Use checked arithmetic
for alignment and range ends.

Size metadata from the supported live allocations, pending retirements and
permanent internal allocations; establish and test the maximum range count.
Store image allocation offset/size in its ownership record. Account explicitly
for internal targets and externally owned swapchain images; only ranges owned
by this allocator can enter its free list. Check Vulkan memory-type, alignment
and dedicated-allocation requirements before binding.

Track every registered descriptor's owning image, including multiple sampler
registrations of one image. Freeing an image retires all of its descriptor
slots. Reuse or replace entries only after their older GPU uses complete.
Do not introduce unsupported null descriptors as a clearing shortcut.

Tests: alignment gaps, exact fits, neighboring coalescence, fragmentation,
memory-type rejection, metadata exhaustion, several descriptors per image,
and at least 20,000 bounded create/submit/free cycles. Memory consumption must
plateau within the documented live-plus-pending bound, and live allocations
must never overlap. Test generation exhaustion explicitly rather than assuming
20,000 operations stay within one slot's generation space.

## 5. Put configuration and scratch under explicit ownership

Keep the cvar name/default/help table immutable and shared. Put values and
explicitly-set flags in a `cvar_state` declared in the existing `cvar.h`.
Initialize that state from defaults for each context and make parsing,
profiles, queries, edits and serialization operate on it explicitly.

Have the context own its configuration. Expose a borrowed configuration
accessor with lifetime tied to the context; render and demo call sites pass
that state. Distinguish init-only settings from runtime settings, and take a
coherent runtime snapshot at frame begin. Preserve explicit-zero overrides,
profile precedence and journaled configuration behavior. Reject non-finite or
out-of-range values before converting to integers.

Migrate callers in one reviewable change. Do not implement compatibility by
adding a hidden current-context global. Move mutable relocation/barrier scratch
into the context or bounded call-local storage.

Test sequential context creation with fresh defaults, two interleaved headless
contexts with different configurations, independent journals, and no leakage
through profiles or runtime changes. These tests establish state isolation;
they do not promise arbitrary concurrent calls on one context.

Then give `plat.h` an opaque per-window handle and put GLFW scroll/input state
behind it using GLFW's window user pointer. Keep process-level GLFW startup,
event pumping and teardown explicit and confined to the platform layer; enforce
its main-thread requirements. Validate two windows, per-window input routing,
and closing one without terminating the other before claiming multi-window
support. Headless operation must remain independent of that backend.

## Header consolidation

Consolidate by ownership while doing the work:

- Keep allocator/retirement structs and helpers private in `vkmin.c`; do not
  add allocator, resource-manager or deletion-queue public headers.
- Extend existing `cvar.h` and `plat.h`, rather than adding parallel settings
  or window-manager interfaces.
- Retain `min_types.h` separately so audio does not import math implementation
  or GPU records. Retain `vkmin_gpu.h` for C/GLSL transport and
  `render_shared.h` for renderer layouts.
- Keep `render_geometry.h` usable without the GPU API. Folding it into
  `render.h` would add an unwanted dependency for CPU-only geometry consumers.
- Keep audio decoder/device interfaces separate from the large mixer-internal
  header. Their small size does not make that dependency merge beneficial.
- Keep existing compatibility include names during this API migration. Remove
  them only as an explicit compatibility change, not as an incidental tidy.

The inspection found no additional standalone-header merge worth its coupling
cost. The planned consolidation reduces duplicated ownership and avoids new
headers; it does not pursue a lower file count for its own sake.

## Completion gates and change order

Land independently reviewable changes in this order: workload/counters;
timeline retirement; versioned replay; range/descriptor reclamation;
context-owned cvars and scratch; per-window platform ownership.

Each step keeps GCC/Clang C11 builds, exported consumers, shader validation,
GCC fanalyzer and cppcheck clean. Retain existing image/audio goldens and
frame-event inspection comparisons. Allocation tests must demonstrate a bounded
memory plateau; routine free must demonstrate zero device-wide waits; context
tests must demonstrate isolation. Benchmarks report measured costs, including
retirement-pressure waits, without hardware-dependent pass thresholds.

Track the concurrent OMEGA artwork golden separately using the existing
original-source-and-shader control. Do not replace that golden to make this
resource work appear green.
