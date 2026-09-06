# Source ownership

Two C11 libraries, with an optional renderer built on vkmin. Single-header
distribution is a packaging choice for the GPU layer; it does not define the
architecture of the whole repository.

| Owner | Files | Responsibility |
| --- | --- | --- |
| Common values | `min_types.h`, `min_math.h` | Plain numeric types and pure math; no device, scene or audio dependency. |
| GPU API | `vkmin.h`, `vkmin_gpu.h` | Context, handles, resources, command recording, frame snapshots, metrics and the C/GLSL transport contract. |
| GPU implementation | `vkmin.c`, `vkmin_inspect.h`, `spirv.h` | Vulkan execution, private journal inspection/replay helpers, SPIR-V validation. |
| Platform | `plat.h`, `plat_glfw.c` | Window/input boundary; headless builds omit GLFW. |
| Configuration | `cvar.h`, `cvar.c` | Current process-wide GPU/render/demo tunables and reference-path switches. |
| Image bridge | `stb_bridge.h`, `stb_bridge.c` | Quarantined third-party image loading/writing. |
| Renderer | `render.h`, `render.c`, `render_shared.h` | Ordered frame passes and renderer-owned CPU/GPU layouts. |
| Geometry | `render_geometry.h`, `render_geometry.c`, `pack.h` | Caller-owned terrain arrays and packed vertex encoding. |
| Scene assets | `scene.h`, `scene.c`, `vkm_format.h`, `ktx2.h`, `ktx2.c` | Cooked scene and texture loading. |
| Generated renderer data | `font.h` | Embedded font; regenerate through the existing font tooling. |
| Audio API | `sndmin.h` | Game-thread commands, immutable resources, offline rendering and acoustic queries. |
| Audio implementation | `sndmin.c`, `sndmin_internal.h`, `sndmin_dsp.h`, `sndmin_acoustics.c`, `sndmin_song.c`, `sndmin_output.c` | Resource ownership, command queues, deterministic mixer/synth/acoustics, songs and output scheduling. |
| Audio IO/backends | `sndmin_io.h`, `sndmin_io.c`, `sndmin_plat.h`, `sndmin_null.c`, `sndmin_miniaudio.c` | Decoder boundary and interchangeable null/live device implementations. |
| Journal transport | `jrnl.h` | Shared packet framing; GPU and audio own their payloads. |
| Compatibility | `shared.h`, `vkmin_math.h` | Old include/name spellings; new code names its actual dependency. |

## Dependency rules

- sndmin's public header needs only `min_types.h` and standard C headers.
  Its implementation uses common math and journal framing, never vkmin or render.
- vkmin knows transport records such as `DrawCmd`, but no `Vertex`, `Material`,
  `Frame` or renderer `Push`. Applications define their own shader layouts.
- Keep the runtime C11 and accept compiled SPIR-V. Shader compilers belong to
  the build pipeline; choosing GLSL or optionally Slang must not introduce a
  runtime compiler, C++ runtime, or shader-language API into vkmin's public surface.
- render depends on vkmin. Geometry helpers retain their existing `vkmin_`
  names, but callers now include `render_geometry.h` (or `render.h`) and link render.
- Shader layout checks stay beside the records they verify. Pure math and DSP
  arithmetic retain their original evaluation order and audio compiler flags.
- Keep command execution and rendering passes readable in execution order.
  Split by ownership or dependency, not by a target line count.

## Builds and migration

`make libraries` produces `libvkmin.a` and `librender.a` in `BUILD`.
`make -f Makefile.sndmin sndmin` builds audio independently of the Vulkan SDK.
Link renderer consumers with `librender.a` before `libvkmin.a`, followed by the
platform/Vulkan libraries. Audio consumers link `libsndmin.a` and one backend.

`make packages` exports three directories under `BUILD/packages`; `make
package-test` compiles and runs consumers using only those exported headers
and libraries. It is included in `make test`. The GPU single header still
requires Vulkan headers/library and the two exported stb headers; GLFW is
required unless `VKMIN_NO_PLATFORM` is defined. Exported stb headers live in
`packages/third_party`; add that directory as a system include path so third-party
diagnostics remain separate from warnings in the library itself.

Code that relied on `vkmin.h` to expose renderer layouts must explicitly
include `render_shared.h`. `shared.h` remains a compatibility include for those
layouts, including existing shaders. `vkmin_math.h` maps existing math names
to `min_math.h`; sndmin no longer exposes math functions transitively.

## Next engineering gates

The [resource lifetime and configuration implementation plan](../docs/resource-lifetime-plan.md)
sets the dependency order, header decisions and completion tests for the next changes.

This consolidation establishes boundaries; it does not change allocation or
threading contracts. The next work should be driven by reproducible failures
and measurements, in this order:

1. Exercise resource churn and exhausted capacities. Document and measure the
   current lifetime arenas before adding reclamation; freeing a GPU handle
   currently waits for the device and does not reclaim its arena allocation.
2. Run the same captured frame through fast/reference paths and compare each
   checkpoint. Use the existing event journal and separate work/wait/readback
   metrics to locate the first divergence or dominant cost.
3. Move configuration into explicit ownership when multiple contexts become a
   supported requirement. The cvar table and platform state are still global.
4. Extend failure injection around audio queue overflow, streaming and shutdown;
   retain the callback allocation/IO guards and deterministic replay checks.
5. Optimize only a demonstrated bottleneck, retaining its reference path and
   recording workload, device, compiler, image/audio equality and timing cost.

These are this project's Carmack-inspired priorities, not his endorsement or
a claim about what he would personally implement.
