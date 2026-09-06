# sndmin v0.1

The v0.6 toolkit integration adds `sndmin_ok` and `sndmin_bus_set` while
retaining the existing native command layout and audio goldens. Use
`sndmin_bus_set(ctx, SNDMIN_MUSIC, 0)` to mute persistently; set one to restore.
See [ownership, failure rules, timing and examples](v0.6-integration.md).

Build the audio library, examples and tests without Vulkan or a device:

```
make -f Makefile.sndmin -j4 sndmin-test
make -f Makefile.sndmin sndmin-live
```

The root `make test` includes audio tests. `sndmin-live` builds a player;
it does not play automatically. Run `build/sndmin/live 40` explicitly to
listen. Apple Clang uses `CC=clang ANALYZER=`; cppcheck remains required.
On Windows, put `w64devkit/bin` and cppcheck on PATH, or use MSYS2 UCRT64.

Link `libsndmin.a` with exactly one of `sndmin_null.o` or
`sndmin_miniaudio.o`. The latter is the miniaudio device layer alone.
The archive has no dependency on Vulkan, GLFW, the renderer or its platform
code. Its public header includes only `min_types.h` and standard C headers;
the implementation uses the independent `min_math.h` helpers.
Third-party source revisions and SHA256s are in `third_party/sndmin-sources.txt`.

```
sndmin_ctx *s = sndmin_init(&(sndmin_desc){.offline=true});
sndmin_patch p = sndmin_make_patch(s, &(sndmin_patch_desc){0});
sndmin_frame(s, &(sndmin_frame_desc){.index=0});
sndmin_play(s, &(sndmin_play_desc){.patch=p, .note=69, .duration=0.5f});
bool ok = sndmin_render(s, 120, "beep.wav", "beep.png");
sndmin_shutdown(s);
```

Check every returned resource handle and render result. Load sounds, streams,
patches and songs before the first frame; the first live frame opens/starts
the device. Zero means defaults in creation descriptors. `sndmin_set` uses
literal gain, so zero mutes. Stream handles have one consumer/playback per
context; use `.loop=true` for music/ambience. PCM sounds and patches support
concurrent playback. The song order list is finite; `.loop` applies to PCM
and streams, not songs. A stop releases synth notes, while a positive fade
sets a linear fade for ordinary voices.

Call frame first, then issue that frame's commands, from one game thread.
Frame indices increase strictly. Simulation is 60 Hz; frame N is sample
800*N. Acoustics are refreshed at 10 Hz and interpolated over 4,800 samples.
Geometry is copied during the frame call, so caller arrays can be temporary.
Spatial units are metres, velocity metres/second, default forward -Z/up +Y.
Layouts use WAVE channel order FL FR [FC LFE BL BR [SL SR]].

Live playback has a fixed three-frame scheduling offset, configurable at init.
Commands for the preceding frame are flushed on the next frame call. A late
game can still miss this deadline; no claim that arbitrary lateness is inaudible
is made. Late commands execute at the next sample and increment a counter.
Queue overflow fails explicitly through `dropped_commands` and render failure.
The same counter reports an overflow of the stopped-group list, the mixer's
record of stopped groups whose late plays must be ignored, so a bookkeeping
limit is never exceeded silently.

The game thread decodes Vorbis ahead into inline PCM command packets. The mixer
owns its stream buffers, voice state and effects. Only two mutable structures
cross threads: an acquire/release SPSC command ring and a snapshot ring. Immutable
resources are retained until shutdown joins the device. The callback reads no
clock; `cpu_percent=-1` means unavailable. `underruns` counts engine stream
starvation, not a hardware xrun counter, which miniaudio does not expose here.

Offline output always runs at 48 kHz, PCM16 with deterministic hash dither.
Float mixing, fixed polynomial transcendental functions, no fast-math and
disabled FP contraction define the reference path. The spectrogram is a
512-sample Hann STFT: time left-to-right, frequency 0–24 kHz bottom-to-top.
A context renders once; reconstruct it or replay its journal for another render.
Offline commands remain on the game side until rendering feeds the bounded ring.

`jrnl_open(path,true)` returns a file both init descriptors can borrow through
`.journal`. Both libraries record tagged packets; neither closes a borrowed
stream. Audio journals embed PCM and patch values, not asset paths or pointers.
Stream PCM is captured while rendering/pumping. `sndmin_replay` ignores video/game
packets; `vkmin_replay` extracts video packets through the shared header.
Legacy Vulkan journals continue to work. Payloads are version-1 C layouts for
little-endian IEEE-float, 64-bit builds; they are not an arbitrary-ABI wire format.

The optional `make HEADLESS=1 sndmin-valley` builds the existing graphical valley
with audio. `--shared-journal FILE` captures both; `--audio-out FILE.wav`,
`--audio-png FILE.png`, and replay's `--audio-frames N` control the audio artifact.
Its CPU weather function mirrors the shader model and hash; its terrain columns
are deliberately coarse acoustic proxies. The standalone `04_valley` is the
small audio-only reference, not a replacement renderer.

Tests compare current examples with their frozen journals and WAV goldens.
`sndmin-golden` deliberately replaces those baselines. The C tests compare
buffer partitions, integral PCM optimization against four-point reference,
streamed against decoded Vorbis through loop seams, and output resampling.
GCC builds also run CRT-interposed callback tests and a fail-closed call-graph
audit. This covers the library callback, not the OS/driver's internal work.

`sndmin-test` also runs a silent pthread producer/mixer comparison with PCM,
synth, streamed Vorbis, changing acoustics and unsigned ring-counter rollover.
Use `sndmin-threads` to run that test alone and `sndmin-bench` to measure four
64-voice workloads. The capacity benchmark reports results; it does not assert
that every machine or workload meets the 2% target.

Sanitizer builds must use a separate directory. With a Clang toolchain and
sanitizer runtimes on PATH, run from the repository root:

```sh
make -f Makefile.sndmin CC=clang AR=llvm-ar ANALYZER= SND_BUILD=build/sndmin-asan \
  OPT='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
  -j4 sndmin-render-test sndmin-threads
make -f Makefile.sndmin CC=clang ANALYZER= SND_BUILD=build/sndmin-tsan \
  OPT='-O1 -g -fsanitize=thread -fno-omit-frame-pointer' -j4 sndmin-threads
```

The first command was exercised locally with LLVM-MinGW UCRT on Windows;
the second is configured for Linux CI. Neither opens a sound device.
Use `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` on Linux and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` for diagnostics. Sanitizer
targets regenerate the examples before checking their frozen goldens.

See [tracker format](sndmin-tracker.md) and [verification report](sndmin-verification.md)
for the exact tested scope and open acceptance items.

The fifth example, `05_ambience`, renders **Morning water**, an original
two-minute ambient score plus a four-second effects tail. Run
`build/sndmin/05_ambience tests/out/sndmin/05_ambience` from the repository root
after building `sndmin`. Its three patches and integer-tick arrangement live
in `demo/valley_score.h` and use only the public API. The graphical valley's
audio adapter uses the same score at half gain. The v0.6 adapter reconstructs
every intervening camera/audio tick when graphics frames are skipped. Other
games must likewise call the score on every simulation tick. See the
[v0.6 integration review](v0.6-review.md).
