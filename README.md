# vkmin

An ultra-minimal Vulkan 1.3 layer in C11, whose entire purpose is to get pixels on screen and
pixels into a PNG file with as little ceremony and as little hidden state as possible. The
deliverable is a spinning textured cube: real vertex and index buffers, a sampled texture, and a
per-frame transform driven by an integer frame index.

It is not a renderer, an engine, or a framework.

```
make            # build
make test       # golden-image harness, headless, works with no GPU
make golden     # regenerate tests/golden
make analyze    # second static analyser (cppcheck)
```

```
./build/cube --headless --frame 60 --out shot.png
./build/cube --headless --frames 0,30,60,90 --out-dir shots/
./build/cube                                    # windowed; F12 saves a numbered PNG, Esc quits
./build/cube --scene clear|tri|cube|tex         # the four milestones
./build/cube --sync-naive                       # the reference synchronisation path
```

## What it is made of

| file | lines | what |
| --- | --- | --- |
| `src/vkmin.h` | 112 | the whole API |
| `src/vkmin.c` | 1579 (1338 without comments and blanks) | the whole implementation |
| `src/plat.h`, `src/plat_glfw.c` | 28 + 74 | platform surface and its GLFW backend |
| `demo/cube.c`, `demo/mat4.h` | 307 + 95 | the demo and its (pure) maths |
| `tools/`, `tests/` | ~300 | texture generator, image diff, solid-colour check, unit test |

## The decisions worth knowing about

**Vulkan 1.3 core, required rather than probed.** Dynamic rendering and `synchronization2` are
demanded at init; a device without them fails loudly with a named reason. There is no
`VkRenderPass` and no `VkFramebuffer` anywhere in the codebase, and no `if (extension_supported)`
branch either. Every such branch avoided is a region of untested code that never exists.

**Handles, not pointers.** Resources are 32-bit handles: a slot index plus a generation counter,
with zero always invalid. A stale or garbage handle aborts with a message instead of aliasing a
recycled slot. This deletes the null-pointer defect class from the API surface.

**One descriptor set layout, one pipeline layout, and no per-frame descriptor churn.** Each image
gets a descriptor set written once at creation. Pipelines that do not sample anything still bind
a 1×1 white image, so there is no "does this pipeline have a texture" branch anywhere in the
frame loop. The MVP travels in push constants; there is no uniform buffer.

**Errors abort.** One `VK_CHECK` macro prints file, line, expression, and the stringified
`VkResult`, then calls `abort()`. Nothing propagates error codes upward: a demo layer that cannot
create a device has nothing useful to do with that information, and the plumbing would be a third
of the code.

**Validation is fatal, in debug builds, and it is on by default.** Core validation *and*
synchronization validation are enabled, and the debug callback aborts on anything at warning
severity or above. Every Vulkan object is named, so a message reads `vkmin.cmd[0]` and
`tests/assets/grid.png` rather than two hex handles.

**Determinism.** Nothing in the render path reads the wall clock or calls `rand()`. The animation
is a pure function of an integer frame index at a fixed timestep, so `--frame 60` renders exactly
frame 60 without simulating the fifty-nine before it, and re-running it produces bit-identical
output. `make test` asserts that.

**The reference path stays.** `--sync-naive` uses one frame in flight and calls
`vkDeviceWaitIdle` after every submit. It is slow on purpose and it is kept permanently: when
something flickers, one flag says in ten seconds whether it is a synchronisation bug or a logic
bug. `make test` renders the same frames both ways and requires bit-identical results.

**No allocator, no runtime shader compilation.** One `VkDeviceMemory` per resource; the cube demo
needs about eight and Vulkan guarantees 4096. (A suballocator would be a later parallel
implementation, not a starting condition.) GLSL is compiled to SPIR-V by the build and embedded
as `static const uint32_t[]`, so the binary is self-contained and has no file-path failure mode
for shaders.

## The readback

Every frame copies its colour target into a host-visible, host-coherent buffer inside its own
command buffer, before presenting, whether or not anyone will ask for a PNG. `vkmin_save_png`
then waits on that frame's fence and writes the mapping. This is the execute-then-inhibit
pattern: it costs a full-target copy per frame and buys one execution path for headless and
windowed alike, plus — decisively — a capture that never reads a swapchain image after it has
been handed to the presentation engine.

That last point was not a design insight, it was a bug report. The first version copied at save
time under a `vkDeviceWaitIdle`, which does not cover the presentation engine; synchronization
validation reported `SYNC-HAZARD-WRITE-AFTER-PRESENT`. The fix then produced
`SYNC-HAZARD-WRITE-AFTER-WRITE` between frames sharing one readback buffer, which is why there is
now one buffer per frame in flight and an explicit barrier ordering each overwrite against the
host's read of it. Both were found by a tool, in the normal course of running the tests, which is
the entire argument for having the tool switched on.

Swapchain images are usually BGRA (lavapipe hands us `B8G8R8A8_UNORM`); the swizzle happens on
the CPU during the copy out rather than in a blit pipeline.

## Deviations from the brief, stated plainly

- **The platform layer is eight functions, not five.** Surface creation, framebuffer size and key
  edges cannot be expressed in terms of the other five and every backend has to provide them.
  No backend types appear in `vkmin.h` or `plat.h`.
- **The readback does not call `vkGetImageSubresourceLayout`.** That function describes a linear
  image's own memory; the copy here goes image → buffer, where `bufferRowLength = 0` means
  tightly packed at the image width. The pitch is still computed in one named variable and passed
  explicitly to the PNG writer rather than assumed, because a wrong pitch here is the classic
  silent shear.
- **`--exit-after N` was added to the demo** so the windowed swapchain path can be run under a
  virtual display in CI instead of only believed to work.
- **`vkmin_size` was added to the API.** Without it the demo computes its aspect ratio from the
  size it requested at init, which is wrong the moment a window is resized.

## Things deliberately not done

No suballocator. No resource destruction API — everything a demo makes lives until
`vkmin_shutdown`; when a free path arrives it must bump the slot's generation or handles to freed
slots will silently alias their replacements (there is a comment where that goes). No MSAA, no
mipmaps, no blending, no second window, no compute. Removing any of them would not make the cube
demo impossible, and adding them would.
