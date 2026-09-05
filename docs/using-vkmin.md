# Reading vkmin as a game developer

Start with `examples/06_cube.c`, `src/vkmin.h`, and the cube's vertex and
fragment shaders. Then read `examples/12_topdown.c` with `src/render.h`.
The first uses the Vulkan layer directly; the second uses the optional scene
renderer. The games own their arrays and simulation. Neither layer owns a
game object hierarchy or calls back into your code.

## What ctx owns

`vkmin_ctx *ctx` is an opaque pointer to the Vulkan device, resource tables,
memory arenas, frame slots, synchronization and recording state. Passing it
first makes that dependency visible. It does not make a draw pure or ctx
serializable. The current cvar and window backends have process-wide state;
this is a single-threaded, single-window design, not a promise that several
independent contexts can run concurrently.

`vkmin_frame_begin` returns the logical frame index, slot, size and input
snapshot together. Read that snapshot once, update game state, prepare arrays,
record draws, end the frame. `demo/play.h` supplies optional example helpers
for fixed simulation steps and previous/current interpolation. It queues
input edges until a tick can consume them and delivers them once during
catch-up. R restarts the examples without rewinding the renderer's frame index.

## The numbers have different jobs

| Value | Meaning | Lifetime |
| --- | --- | --- |
| `vkmin_image`, `vkmin_buffer`, `vkmin_pipeline` | Distinct C struct types wrapping generation-tagged handles | Until destruction; stale handles are rejected |
| `vkmin_index(ctx, image)` | Bindless texture/sampler slot for GLSL | Until that image is destroyed; the numeric slot has no generation protection |
| `vkmin_address(ctx, buffer)` | A real 64-bit GPU address | Buffer lifetime; ring addresses have a shorter frame-slot lifetime |
| `Instance.mesh`, `.material` | Array offsets returned by renderer uploads | Renderer lifetime under its current append-only upload model |
| `Instance.id` | Your game object's picking ID | You assign its meaning; zero means no object |

Do not pass a handle's `.id` as a texture slot. Do not persist GPU addresses
in a savegame. Do not keep a ring address after its frame slot is reused.
Adding an integer typedef would document intent but would not provide C type
safety; the existing resource handle structs do provide that distinction.

The ergonomic win is the draw itself: a pipeline, your push struct, and a
count. `ExPush` in the cube carries a matrix, vertex address and texture slot.
Pipeline creation owns the push size and checks it against SPIR-V reflection.
You still have to understand GPU lifetimes, shared layout and shader indexing.

Use `VKMIN_BYTES(array)` for an actual array. For allocated data use
`(vkmin_bytes){pointer, byte_count}`: the macro applied to a pointer only
measures the pointer. Upload bytes and push data are consumed by the call;
that does not extend the lifetime of resources their contents refer to.

## Two recordings, two debugging questions

An input demo (`.vkd`, `--demo` / `--play`) records snapshots that drive the
game again. It requires the same game logic, assets and settings. Its current
header records format version and dimensions, not a build or asset digest.
Keep those alongside a bug report. It is not a general savegame format.

A renderer journal (`.vkj`, `--record` / `--replay`) records supported GPU
operations, upload and shader bytes, and frame inputs. The replay executable
does not run the game. The journal relocates addresses it recognizes against
the new run's allocations and verifies recorded picks. Arbitrary derived
addresses are not automatically meaningful persistent identifiers. It is a
debug artifact with bounded parsing, not a hostile-file sandbox or a promised
long-term interchange format.

From the repository root, after building (substitute your build directory):

```sh
./build/ex_13_platformer --demo build/jump.vkd
./build/ex_13_platformer --headless --play build/jump.vkd --frame 300 \
    --state-trace build/jump.state --record build/jump.vkj --out build/jump.png
./build/ex_07_replay --replay build/jump.vkj --frame 300 \
    --path=legacy --out build/jump-replay.png
./build/imgdiff build/jump.png build/jump-replay.png 0
```

The first command requires a window build and at least 301 recorded frames.
For headless-only work use `tests/journals/13_platformer.vkd` with
`--profile lavapipe`. The shipped demos are generated inputs, not human
playthroughs. The optional state trace contains named scalar checkpoints and
a padding-independent hash. It helps locate divergence in selected gameplay
values; it is not a hash of every byte of game state. The test harness compares
these traces across Vulkan paths and checks gameplay outcomes separately.

Quake III already had [event journals in common.c](https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/common.c)
and separate [network-message demos in cl_main.c](https://github.com/id-Software/Quake-III-Arena/blob/master/code/client/cl_main.c).
Explicit contexts, indexed resources and deterministic replay are established
techniques. vkmin's useful experiment is combining them into a small Vulkan
layer whose examples can be investigated without a physical GPU.

## What remains inconvenient

The API removes descriptor and synchronization boilerplate, but still asks
the caller to size arenas, cook assets, manage lifetimes and compose shaders.
Failures generally abort instead of returning recoverable errors. The scene
renderer has fixed capacity and render-size limits. Bit-identical results
are tested on one lavapipe implementation across its two Vulkan paths;
arbitrary drivers and platforms can round floating-point operations differently.

That makes vkmin promising for C programmers building custom renderers,
small games and graphics tests. It is not evidence of usability for a broad
game-development audience. The next human acceptance test is still a developer
building a small scene from the header, shader library and one example,
recording what they had to look up and what they could not express.
