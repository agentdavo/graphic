# Source ownership

Two C11 libraries, with an optional renderer built on vkmin. Single-header
distribution is a packaging choice for the GPU layer; it does not define the
architecture of the whole repository.

Every file's name says which of the four owners it belongs to, so the grouping
survives an `ls`: `min_` is common to both libraries, `vkmin_` is the GPU layer
and the single-header subject, `render_` is the optional renderer that
links as its own `librender.a`, and `sndmin_` is audio. A file that would need
two of those prefixes belongs in the common layer instead.

| Owner | Files | Responsibility |
| --- | --- | --- |
| Common values | `min_types.h`, `min_math.h` | Plain numeric types and pure math; no device, scene or audio dependency. |
| GPU API | `vkmin.h`, `vkmin_gpu.h` | Context, handles, resources, command recording, frame snapshots, metrics and the C/GLSL transport contract. |
| GPU implementation | `vkmin.c`, `vkmin_inspect.h`, `vkmin_spirv.h` | Vulkan execution, private journal inspection/replay helpers, SPIR-V validation. |
| Platform | `vkmin_plat.h`, `vkmin_plat_glfw.c`, `vkmin_plat_sdl2.c`, `vkmin_plat_sdl3.c`, `vkmin_plat_sdl.h`, `vkmin_plat_win32.c` | Window/input boundary. Four parallel backends; `-DVKMIN_PLATFORM=glfw`, `sdl2`, `sdl3` or `win32` picks one (`auto` takes the first it finds), and `-DVKMIN_HEADLESS=ON` omits all of them. `win32` is the no-dependency path: user32 plus runtime-loaded XInput. `vkmin_plat_sdl.h` holds the scancode and gamepad-button tables the two SDL backends share. |
| Configuration | `vkmin_cvar.h`, `vkmin_cvar.c` | Current process-wide GPU/render/demo tunables and reference-path switches. |
| Image bridge | `vkmin_stb.h`, `vkmin_stb.c` | Quarantined third-party image loading/writing. |
| Renderer | `render.h`, `render.c`, `render_shared.h` | Ordered frame passes and renderer-owned CPU/GPU layouts. |
| Geometry | `render_geometry.h`, `render_geometry.c`, `render_pack.h` | Caller-owned terrain arrays and packed vertex encoding. |
| Scene assets | `render_scene.h`, `render_scene.c`, `render_format.h`, `render_ktx2.h`, `render_ktx2.c` | Cooked scene and texture loading. |
| Generated renderer data | `render_font.h` | Embedded font; regenerate through the existing font tooling. |
| Audio API | `sndmin.h` | Game-thread commands, immutable resources, offline rendering and acoustic queries. |
| Audio implementation | `sndmin.c`, `sndmin_internal.h`, `sndmin_dsp.h`, `sndmin_acoustics.c`, `sndmin_song.c`, `sndmin_output.c` | Resource ownership, command queues, deterministic mixer/synth/acoustics, songs and output scheduling. |
| Audio IO/backends | `sndmin_io.h`, `sndmin_io.c`, `sndmin_plat.h`, `sndmin_null.c`, `sndmin_miniaudio.c` | Decoder boundary and interchangeable null/live device implementations. |
| Journal transport | `min_jrnl.h` | Shared packet framing; GPU and audio own their payloads. |
| Compatibility | `vkmin_math.h` | `min_math.h` under vkmin's prefix, so one math implementation can serve both libraries without either owning the other's namespace. 1866 call sites; not indirection to delete. The `shared.h` shim is gone: its users name `render_shared.h`. |

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

The CMake targets `vkmin`, `render` and `sndmin` produce `libvkmin.a`,
`librender.a` and `libsndmin.a`; `sndmin` and `sndmin_null` build without the
Vulkan SDK. Link renderer consumers with `librender.a` before `libvkmin.a`,
followed by the platform/Vulkan libraries. Audio consumers link `libsndmin.a`
and exactly one backend, `sndmin_null` or `sndmin_miniaudio`.

Third-party headers are exposed as a system include path so their diagnostics
stay separate from warnings in the library itself. The package export and the
consumer tests that checked it are not currently in the tree.

Code that relied on `vkmin.h` to expose renderer layouts must explicitly
include `render_shared.h`. `render_shared.h` remains a compatibility include for those
layouts, including existing shaders. `vkmin_math.h` maps existing math names
to `min_math.h`; sndmin no longer exposes math functions transitively.

## Resource lifetime and configuration

Freeing a GPU handle now retires its storage on the submission timeline; completed
ranges and texture slots are reusable. Ordinary free does not wait for the whole
device. Version 6 journals resolve buffer identity independently of physical
reuse; historical journals retain their original allocation policy.

Configuration belongs to each context. Include `vkmin_cvar.h`, initialize a
`cvar_state`, and pass it in `vkmin_desc.config` if defaults need changing.
Cvar calls take the state explicitly. `vkmin_config(ctx)` exposes editable state;
`vkmin_frame_config(ctx)` exposes the snapshot taken at frame begin. Platform
windows also have separate ownership and input state, with main-thread calls.

Allocator and retirement helpers remain private; this work adds no public
resource-manager header. Header boundaries above remain intact.

Further optimization should follow measured workloads, retaining reference paths
and image/audio comparisons. These are this project's Carmack-inspired priorities,
not his endorsement or a claim about what he would personally implement.
