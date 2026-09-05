# sndmin v0.1 verification — 5 September 2026

Implemented and exercised on Windows x64, GCC 16.2.0 from w64devkit,
Intel Core Ultra 7 265. This is a report of observed results, not a
certification of the untested operating systems or every possible workload.

| Check | Observed result |
|---|---|
| Standalone `make -f Makefile.sndmin -j4 sndmin-test` | Passed, warnings as errors, GCC analyzer and cppcheck |
| Five example WAVs | Direct render = current journal replay = frozen journal replay = golden, byte for byte (fifth uses a SHA256 golden) |
| Callback buffer partitions | Float32 output identical for 4,096 samples mixed together or in varying small buffers |
| Integral PCM optimization | Null against four-point scalar reference, including loop seams and fractional pitch |
| Vorbis streaming | Null against whole-file decode, stereo, looping and pitch 1.37 |
| Output resampling | 44.1 kHz output identical across callback partitions |
| Compiler optimization | `-O3` replay matches the `-O2` WAV goldens for all four examples |
| Second compiler and memory sanitizers | Windows LLVM-MinGW Clang 22.1.8, `-O1 -fsanitize=address,undefined -fno-sanitize-recover=all`: full tests and all five fresh example renders/replays pass against existing GCC goldens |
| Two simultaneous owners | Ten seconds of 7.1 PCM, four-unison synth, looping Vorbis, moving-source acoustics and fades: threaded float output equals serial; unsigned ring counters wrap; zero late/drop/starvation |
| Spatial reference | 1,080 angle/layout energy cases, distance/occlusion/reflection checks |
| Bounds/contracts | Queue saturation, exact sample 800 start, invalid resources/numeric input, four corrupt journal cases |
| Song control | Muting affects current and future notes; stopping cancels future notes and fades active notes |
| Callback debug instrumentation | Deliberate allocation, file access and clock reads abort; ordinary renders pass |
| Callback call graph | 23 reachable functions; only memcpy/memset and lock-free atomic operations leave the core translation unit |
| Shared graphics/audio journal | Two valley frames (0, 3), WAV and spectrogram identical on modern record → legacy replay under lavapipe |
| Vulkan amalgamation / repository cppcheck | Passed |
| Line budgets | 1,510 core lines including audio headers/platform wrappers/journal; public header 105; tracker 47 |
| Mixer capacity | Simple PCM passes 2%; spatial PCM and maximum-unison synth exceed it substantially (measurements below) |
| Windows live playback | Completed 10 seconds: 480,480 mixed samples, zero engine underruns, zero late commands |
| Ten-minute live playback | Started, then **stopped at the user's request**. No completed result |

The expanded benchmark is `make -f Makefile.sndmin sndmin-bench`, default
GCC `-O2`. Each workload renders exactly 60 seconds through 512/288-sample
buffers at 48 kHz, retaining 64 voices. All four buses receive reverb sends.
Spatial cases include six room walls, a moving listener, fractional pitch,
Doppler, four reflection taps, 7.1 output and music delay. Synth cases add
four-way unison, two oscillators, sub, filter envelopes, LFO and chorus.

| Workload | Mixer thread CPU | Fraction of one core | Full harness |
|---|---:|---:|---:|
| 64 mono PCM, unity pitch, stereo | 1.109 s | 1.85% | 1.90% |
| 64 spatial PCM, fractional pitch, 7.1 | 4.328 s | 7.21% | 7.58% |
| 64 spatial synths, four-way unison, 7.1 | 27.688 s | 46.15% | 46.77% |
| 56 spatial synths + 8 looping Vorbis streams, 7.1 | 25.750 s | 42.92% | 44.45% |

These are single measurements on this CPU, not worst-case execution bounds.
Windows thread CPU time surrounds each frame's two mixer calls; full harness
also includes command preparation, acoustic evaluation, decoding and finite
sample/headroom checks. All cases finished with finite output, 64 active
voices and zero engine starvation, late or dropped commands. **The 2% target
is not met for the heavier workloads.** No output-changing optimization or
golden replacement was made during this hardening pass. The older simple
`test --bench` measurement was 1.95%; it never established synth capacity.

No clock is read inside the mixer. Public `cpu_percent` is consequently -1
(unavailable). The underrun counter covers engine stream starvation. Hardware
xruns are not exposed by this wrapper, so zero engine underruns does not prove
zero hardware xruns. Callback auditing covers sndmin's call tree, not the
operating system or miniaudio's own device plumbing.

The root graphics harness is **not green**: the last complete run was
**107 checks, 14 failures**. Six are Corridor/overlay/debug golden mismatches
already described in the repository's earlier verification. Eight compare
the current valley with its frozen journal: four current-versus-frozen and
four frozen-legacy-versus-current-modern comparisons. Inspection found 589
current records versus 573 frozen records, five different shader payloads,
and 37 image creations versus 36. Those graphics goldens were not replaced.
The newly added shared-journal test passes. The first full run also caught
invalid inverted acoustic proxy bounds; those were corrected and retested.

Local logs are in `tests/out/sndmin/`: `hardening-tests.log`, `asan-build.log`,
`asan-tests.log`, `asan-parser.log`, `benchmark-capacity.log`, plus the earlier
`graphics-final.log` and `shared/`. Outputs and temporary journals are ignored
by Git; frozen audio journals and WAV goldens are under `tests/journals/sndmin`
and `tests/golden/sndmin`.

The user played `02_song` and answered: **“1984 electronic soundtrack - yes it
does.”** This is the human listening result, not an inferred assessment. A
subsequent removal of a redundant unity filter changed 29 individual PCM16
samples in the forty-second file by one unit; notes, patches and arrangement
were unchanged. The current golden SHA256 is
`3c20e6b95b2a809bb9de3d6299e9a289501c63975530d59eff7a65bf22227dbf`.

The portable sanitizer toolchain was unpacked only under ignored `build/deps`:
[LLVM-MinGW 20260826 UCRT x64](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260826),
archive SHA256 `ae601f4e0f72bbdf441ad2df8bb16f037e2e9251559ea6b37b4057aef39c06c3`,
verified against GitHub's asset digest. W64devkit GCC itself lacks those runtimes.
Windows ASan/UBSan execution passed; LeakSanitizer and ThreadSanitizer were not
run locally. Clang also caught two misleadingly indented returns, now corrected
without changing output. Malformed-journal tests now require ordinary parser
rejection so a sanitizer crash cannot count as a passing negative test.

CI retains the Linux/macOS/Windows golden matrix and adds independent Linux
ASan/UBSan and ThreadSanitizer jobs. ASan regenerates all five examples rather
than accidentally reusing another build's output. ThreadSanitizer runs the
overlapping two-owner test; its test-only pacing uses relaxed atomics so it
does not substitute an extra happens-before edge for the engine rings.
The workflow has a manual trigger and timeouts, but was not dispatched here.

Still unverified: Linux/macOS execution, bit equality across those machines,
ten-minute live stability on all three platforms, hardware underrun counts,
native 5.1/7.1 device playback, Linux leak detection and ThreadSanitizer.
All work in this hardening pass was silent. Surround panning
is covered numerically; raw channel sums cannot be bit-identical across
constant-power layouts because constant power does not preserve amplitude sum.
HRTF is outside v0.1, as requested. No human assessment of the room or valley
acoustics is claimed.

The acoustic model remains deliberately coarse: AABBs, fixed rays, four taps
per source and one four-line FDN per bus. Source parameters interpolate at
10 Hz; reverb delay lengths change at the acoustic update. Stream handles
are single-consumer within a context, and songs have a finite order list.
Those limits are documented in the usage guide rather than hidden behind
the example's successful render.
