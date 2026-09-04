#!/bin/sh
# The golden-image harness. Renders the fixed frame set headless, compares
# against tests/golden with a small per-channel tolerance, and writes a diff
# image beside any failure.
#
#   ./tests/run_tests.sh                 compare against the goldens
#   VKMIN_WRITE_GOLDEN=1 ./tests/...     regenerate the goldens instead
set -u

BUILD=${BUILD:-build}
GOLDEN=tests/golden
OUT=tests/out
TOLERANCE=${VKMIN_TOLERANCE:-2}
CUBE=./$BUILD/cube
failures=0
checks=0

# Pin the software rasteriser when it is installed, so a machine that also has
# a real GPU still runs the same path CI does.
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
    if ./$BUILD/imgdiff "$GOLDEN/$name.png" "$OUT/$name.png" "$TOLERANCE" \
            "$OUT/$name.diff.png"; then
        note "match: $name"
    else
        fail "$name differs from its golden"
    fi
}

begin "== pure functions =="
checks=$((checks + 1))
./$BUILD/mat4_test || fail "mat4_test"

begin "== milestone 1: clear and readback =="
$CUBE --headless --scene clear --frame 0 --out "$OUT/clear_0000.png" >/dev/null || \
    fail "clear render"
checks=$((checks + 1))
# The whole readback chain, asserted before any rendering exists to confuse it.
./$BUILD/pngsolid "$OUT/clear_0000.png" 255 0 255 255 >/dev/null || \
    fail "cleared image is not exactly the clear colour"
compare clear_0000

begin "== milestones 2-4: triangle, cube, texture =="
$CUBE --headless --scene tri --frames 0 --out-dir "$OUT" >/dev/null || fail "triangle render"
compare tri_0000
for scene in cube tex; do
    $CUBE --headless --scene $scene --frames 0,30,60,90 --out-dir "$OUT" >/dev/null || \
        fail "$scene render"
    for frame in 0000 0030 0060 0090; do compare ${scene}_${frame}; done
done

begin "== determinism: the same frame twice is bit-identical =="
$CUBE --headless --scene tex --frame 60 --out "$OUT/repeat.png" >/dev/null || fail "repeat render"
checks=$((checks + 1))
./$BUILD/imgdiff "$OUT/tex_0060.png" "$OUT/repeat.png" 0 >/dev/null || \
    fail "frame 60 is not reproducible run to run"

begin "== the reference sync path must agree with the fast one =="
mkdir -p "$OUT/naive"
$CUBE --headless --scene tex --sync-naive --frames 0,30,60,90 --out-dir "$OUT/naive" \
    >/dev/null || fail "sync-naive render"
for frame in 0000 0030 0060 0090; do
    checks=$((checks + 1))
    ./$BUILD/imgdiff "$OUT/tex_${frame}.png" "$OUT/naive/tex_${frame}.png" 0 \
        "$OUT/naive_${frame}.diff.png" >/dev/null || \
        fail "--sync-naive differs from the normal path at frame $frame"
done

begin "== milestone 5: swapchain, sharing the headless recording code =="
# Needs a display. Xvfb is enough: lavapipe presents to it fine.
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
    if DISPLAY=$DISPLAY_RUN $CUBE --scene tex --size 256 256 --exit-after 91 \
            --out "$OUT/windowed_0090.png" >/dev/null; then
        checks=$((checks + 1))
        ./$BUILD/imgdiff "$OUT/tex_0090.png" "$OUT/windowed_0090.png" "$TOLERANCE" \
            "$OUT/windowed.diff.png" >/dev/null || \
            fail "the swapchain path does not match the offscreen path"
        note "match: windowed capture against the offscreen render"
    else
        fail "windowed run"
    fi
    [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
fi

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ]
