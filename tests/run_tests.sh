#!/bin/sh
# The golden-image harness for vkmin v0.1. Runs headless under lavapipe with
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

begin "== GPU layer smoke test: compute, device addresses, bindless, timestamps =="
checks=$((checks + 1))
./$BUILD/smoke "$OUT/smoke.png" >/dev/null 2>"$OUT/smoke.log" || { fail "smoke"; sed -n '1,12p' "$OUT/smoke.log"; }
./$BUILD/smoke "$OUT/smoke_bc1.png" tests/assets/grid_bc1.ktx2 >/dev/null 2>&1 || fail "smoke with a BC1 KTX2"
compare smoke
compare smoke_bc1

begin "== The Corridor: golden frames (each frame in its own process) =="
for f in $FRAMES; do
    render "corridor_$f" --frame "$f"
    compare "corridor_$f"
done

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
    if DISPLAY=$DISPLAY_RUN ./$BUILD/corridor --profile lavapipe --exit-after 10 d_frame_step=0 r_overlay=0 \
            --out "$OUT/windowed.png" >/dev/null 2>"$OUT/windowed.log"; then
        render headless_for_windowed --frame 0 r_overlay=0
        same headless_for_windowed windowed "$TOLERANCE" "swapchain capture == offscreen render"
    else
        fail "windowed run"; sed -n '1,12p' "$OUT/windowed.log"
    fi
    [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
fi

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ]
