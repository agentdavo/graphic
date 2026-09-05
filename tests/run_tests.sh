#!/bin/sh
# The golden-image harness for vkmin. Runs headless under lavapipe with
# the software profile, compares against tests/golden with a small per-channel
# tolerance, writes a diff image beside any failure, and checks every
# reference path against the fast path it shadows.
#
#   ./tests/run_tests.sh                 compare against the goldens
#   VKMIN_WRITE_GOLDEN=1 ./tests/...     regenerate the goldens instead
set -u

BUILD=${BUILD:-build}
GOLDEN=tests/golden
OUT=tests/out
TOLERANCE=${VKMIN_TOLERANCE:-2}
CORRIDOR="./$BUILD/corridor --profile lavapipe --headless"
FRAMES="0 120 240 360"
failures=0
checks=0

LVP=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json 2>/dev/null | head -1)
if [ -n "$LVP" ]; then
    VK_ICD_FILENAMES=$LVP
    VK_DRIVER_FILES=$LVP
    export VK_ICD_FILENAMES VK_DRIVER_FILES
fi
mkdir -p "$OUT" "$GOLDEN"

note()  { printf '  %s\n' "$*"; }
fail()  { printf '  FAIL: %s\n' "$*"; failures=$((failures + 1)); }
begin() { printf '%s\n' "$*"; }

# compare <name>: tests/out/<name>.png against tests/golden/<name>.png
compare() {
    name=$1
    checks=$((checks + 1))
    if [ "${VKMIN_WRITE_GOLDEN:-0}" = "1" ]; then
        cp "$OUT/$name.png" "$GOLDEN/$name.png" || fail "cannot write golden $name"
        note "golden updated: $name"
        return
    fi
    if [ ! -f "$GOLDEN/$name.png" ]; then
        fail "no golden for $name (run 'make golden' to create one)"
        return
    fi
    if ./$BUILD/imgdiff "$GOLDEN/$name.png" "$OUT/$name.png" "$TOLERANCE" "$OUT/$name.diff.png"; then
        note "match: $name"
    else
        fail "$name differs from its golden"
    fi
}

# same <a> <b> <tolerance> <what>: two outputs that must agree
same() {
    checks=$((checks + 1))
    if ./$BUILD/imgdiff "$OUT/$1.png" "$OUT/$2.png" "$3" "$OUT/$1.vs.$2.diff.png"; then
        note "agree: $4"
    else
        fail "$4"
    fi
}

# Quiet renders: validation aborts loudly on its own, so only failures speak.
render() { # <output name> [args...]
    name=$1; shift
    $CORRIDOR "$@" --out "$OUT/$name.png" >/dev/null 2>"$OUT/$name.log" || {
        fail "render $name"; sed -n '1,12p' "$OUT/$name.log"; }
}

begin "== pure functions =="
checks=$((checks + 1))
./$BUILD/mat4_test || fail "mat4_test"

begin "== handles: a freed slot's old handle aborts, the new one works =="
checks=$((checks + 1))
./$BUILD/handles 2>/dev/null || fail "handles"

begin "== --no-readback on the legacy path: the copy is skipped and save_png says so =="
checks=$((checks + 1))
$CORRIDOR --frame 0 --path=legacy --no-readback --out "$OUT/noreadback.png" >/dev/null 2>"$OUT/noreadback.log"
if [ -f "$OUT/noreadback.png" ]; then
    fail "--no-readback still wrote a PNG"
elif grep -q "readback is disabled" "$OUT/noreadback.log"; then
    note "refused, with the reason"
else
    fail "--no-readback: no PNG but no reason given either"
fi

begin "== GPU layer smoke test: compute, device addresses, bindless, timestamps =="
checks=$((checks + 1))
./$BUILD/smoke "$OUT/smoke.png" >/dev/null 2>"$OUT/smoke.log" || { fail "smoke"; sed -n '1,12p' "$OUT/smoke.log"; }
./$BUILD/smoke "$OUT/smoke_bc1.png" tests/assets/grid_bc1.ktx2 >/dev/null 2>&1 || fail "smoke with a BC1 KTX2"
compare smoke
compare smoke_bc1

begin "== which paths can this device run? =="
./$BUILD/corridor --probe > "$OUT/probe.txt" 2>&1 || fail "probe"
cat "$OUT/probe.txt" | sed 's/^/  /'
MODERN=0
grep -q "hostImageCopy=1 maintenance5=1" "$OUT/probe.txt" && MODERN=1

begin "== the examples are the tests: each renders frame 60 at 256x256 =="
for ex in 01_clear 02_triangle 03_buffer 04_texture 05_compute 06_cube; do
    checks=$((checks + 1))
    if ./$BUILD/ex_$ex --headless --size 256 256 --frame 60 --out "$OUT/ex_$ex.png" >/dev/null 2>"$OUT/ex_$ex.log"; then
        checks=$((checks - 1))
        compare "ex_$ex"
    else
        fail "example $ex"; sed -n '1,8p' "$OUT/ex_$ex.log"
    fi
done

begin "== the ID target: vkmin_pick reads the instance under a texel, both paths =="
for p in legacy modern; do
    [ "$p" = "modern" ] && [ "$MODERN" != "1" ] && continue
    checks=$((checks + 1))
    ./$BUILD/pick --headless --frame 1 --path=$p >/dev/null 2>"$OUT/pick_$p.log" || { fail "pick ($p)"; sed -n '1,6p' "$OUT/pick_$p.log"; }
done

begin "== the five games: each replays its demo file to a golden frame =="
# game <name> <frame> [extra flags]: play tests/journals/<name>.vkd headless
# on the default path, compare the frame to its golden; then the same frame
# on the legacy path must be bit-identical.
game() {
    name=$1; frame=$2; shift 2
    checks=$((checks + 1))
    if ./$BUILD/ex_$name --profile lavapipe --headless --play "tests/journals/$name.vkd" --frame "$frame" \
            --out "$OUT/ex_$name.png" "$@" >/dev/null 2>"$OUT/ex_$name.log"; then
        checks=$((checks - 1))
        compare "ex_$name"
    else
        fail "game $name"; sed -n '1,8p' "$OUT/ex_$name.log"
    fi
    if [ "$MODERN" = "1" ]; then
        checks=$((checks + 1))
        if ./$BUILD/ex_$name --profile lavapipe --headless --play "tests/journals/$name.vkd" --frame "$frame" --path=legacy \
                --out "$OUT/ex_${name}_legacy.png" "$@" >/dev/null 2>"$OUT/ex_${name}_legacy.log"; then
            checks=$((checks - 1))
            same "ex_$name" "ex_${name}_legacy" 0 "$name: legacy path == modern path"
        else
            fail "game $name (legacy)"; sed -n '1,8p' "$OUT/ex_${name}_legacy.log"
        fi
    fi
}
game 10_shooter 150
game 11_rts 300 d_check_cull=1
game 12_topdown 300
game 13_platformer 300
game 14_anime 240

begin "== a game's journal: record 14_anime playing its demo, replay it, same pixels =="
checks=$((checks + 1))
./$BUILD/ex_14_anime --profile lavapipe --headless --play tests/journals/14_anime.vkd --frame 240 --record "$OUT/anime.vkj" >/dev/null 2>"$OUT/record_anime.log" || fail "record 14_anime"
checks=$((checks + 1))
if ./$BUILD/ex_07_replay --replay "$OUT/anime.vkj" --frame 240 --path=legacy --out "$OUT/anime_replay.png" >/dev/null 2>"$OUT/replay_anime.log"; then
    same ex_14_anime anime_replay 0 "14_anime's journal, recorded on one path, replays on the other"
else
    fail "replay 14_anime"; sed -n '1,8p' "$OUT/replay_anime.log"
fi

begin "== the journal: record 06_cube, replay it without the program, same pixels =="
checks=$((checks + 1))
./$BUILD/ex_06_cube --headless --size 256 256 --frame 60 --record "$OUT/cube.vkj" >/dev/null 2>"$OUT/record.log" || { fail "record"; sed -n '1,6p' "$OUT/record.log"; }
checks=$((checks + 1))
if ./$BUILD/ex_07_replay --replay "$OUT/cube.vkj" --frame 60 --out "$OUT/ex_07_replay.png" >/dev/null 2>"$OUT/replay.log"; then
    same ex_06_cube ex_07_replay 0 "replayed journal == the recording program's frame"
else
    fail "replay"; sed -n '1,8p' "$OUT/replay.log"
fi
if [ "$MODERN" = "1" ]; then
    checks=$((checks + 1))
    if ./$BUILD/ex_07_replay --replay "$OUT/cube.vkj" --frame 60 --path=legacy --out "$OUT/ex_07_legacy.png" >/dev/null 2>"$OUT/replay2.log"; then
        same ex_06_cube ex_07_legacy 0 "journal recorded on the modern path replays on the legacy path"
    else
        fail "replay on legacy"; sed -n '1,8p' "$OUT/replay2.log"
    fi
fi
checks=$((checks + 1))
./$BUILD/corridor --profile lavapipe --headless --frame 240 --record "$OUT/corridor.vkj" --out "$OUT/rec_240.png" >/dev/null 2>&1 || fail "record corridor"
checks=$((checks + 1))
if ./$BUILD/ex_07_replay --replay "$OUT/corridor.vkj" --frame 240 --out "$OUT/replay_240.png" >/dev/null 2>"$OUT/replay3.log"; then
    same rec_240 replay_240 0 "The Corridor frame 240 replays from its journal"
else
    fail "replay corridor"; sed -n '1,8p' "$OUT/replay3.log"
fi

begin "== The Corridor: golden frames on the legacy path (each frame in its own process) =="
for f in $FRAMES; do
    render "corridor_$f" --frame "$f" --path=legacy
    compare "corridor_$f"
done

begin "== the same frames on the modern path must be bit-identical =="
if [ "$MODERN" = "1" ]; then
    for f in $FRAMES; do
        render "modern_$f" --frame "$f" --path=modern
        same "corridor_$f" "modern_$f" 0 "frame $f: modern == legacy"
    done
    render modern_naive_240 --frame 240 --path=modern --sync-naive
    same corridor_240 modern_naive_240 0 "frame 240: modern + sync-naive == legacy"
else
    note "SKIP: modern path (hostImageCopy + maintenance5) not available on this device"
fi

begin "== determinism: frame 240 twice, bit-identical =="
render corridor_240_again --frame 240
same corridor_240 corridor_240_again 0 "frame 240 is reproducible run to run"

begin "== reference paths must agree with the fast paths =="
render cull_cpu --frame 240 r_gpu_cull=0
same corridor_240 cull_cpu 0 "GPU cull == CPU reference cull"
render cull_stable --frame 240 r_cull_compact=0
same corridor_240 cull_stable 0 "atomic-append cull == stable-slot cull"
render brute_force --frame 240 r_clustered=0
same corridor_240 brute_force 2 "clustered lights == brute-force lights"
render naive_sync --frame 240 --sync-naive
same corridor_240 naive_sync 0 "--sync-naive == two frames in flight"

begin "== overlay text (deterministic in headless mode: no timings) =="
render overlay --frame 0 r_overlay=1
compare overlay

begin "== debug views render (validation is fatal, so rendering is the test) =="
for d in 1 2 3 4 5 6; do render "debug_$d" --frame 120 "r_debug=$d"; done
compare debug_3
compare debug_6

begin "== windowed swapchain path, shared recording code =="
DISPLAY_RUN=""
if [ -n "${DISPLAY:-}" ]; then
    DISPLAY_RUN=$DISPLAY
elif command -v Xvfb >/dev/null 2>&1; then
    Xvfb :98 -screen 0 1024x768x24 >/dev/null 2>&1 &
    XVFB_PID=$!
    sleep 2
    DISPLAY_RUN=:98
fi
if [ -z "$DISPLAY_RUN" ]; then
    note "SKIP: no display and no Xvfb; the windowed path was not exercised"
else
    checks=$((checks + 1))
    # Two frames in flight, ten frames, then the last one is captured from the
    # swapchain. d_frame_step=0 holds the animation at frame 0 for comparison.
    for p in legacy modern; do
        [ "$p" = "modern" ] && [ "$MODERN" != "1" ] && { note "SKIP: windowed modern path"; continue; }
        checks=$((checks + 1))
        if DISPLAY=$DISPLAY_RUN ./$BUILD/corridor --profile lavapipe --exit-after 10 d_frame_step=0 r_overlay=0 \
                --path=$p --out "$OUT/windowed_$p.png" >/dev/null 2>"$OUT/windowed_$p.log"; then
            render headless_for_windowed --frame 0 r_overlay=0
            same headless_for_windowed "windowed_$p" 0 "$p: windowed backbuffer == headless render"
        else
            fail "windowed run ($p)"; sed -n '1,12p' "$OUT/windowed_$p.log"
        fi
    done
    [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
fi

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ]
