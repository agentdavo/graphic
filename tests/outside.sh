# Sourced by run_tests.sh: uses its counters, failure reporting and ICD.
begin "== outside: committed journal, current renderer, both Vulkan paths =="
VALLEY="./$BUILD/ex_20_valley --profile=lavapipe"
VALLEY_FRAMES="0,2400,4800,7199"
mkdir -p "$OUT/valley-live" "$OUT/valley-legacy" "$OUT/valley-modern"
checks=$((checks + 1))
gzip -dc tests/journals/20_valley.vkj.gz > "$OUT/valley-frozen.vkj" || fail "unpack valley journal"
checks=$((checks + 1))
$VALLEY --frames "$VALLEY_FRAMES" --record "$OUT/valley-current.vkj" --out-dir "$OUT/valley-live" >"$OUT/valley-live.log" 2>&1 || fail "valley current renderer"
checks=$((checks + 1))
$VALLEY --frames "$VALLEY_FRAMES" --path=legacy --replay "$OUT/valley-frozen.vkj" --out-dir "$OUT/valley-legacy" >"$OUT/valley-legacy.log" 2>&1 || fail "valley frozen journal legacy replay"
for frame in 0000 2400 4800 7199; do
    same "valley-live/valley_$frame" "valley-legacy/valley_$frame" 0 "valley $frame: current renderer == committed journal"
    cp "$OUT/valley-legacy/valley_$frame.png" "$OUT/valley_$frame.png" || fail "valley replay golden output missing"
    old_tolerance=$TOLERANCE; TOLERANCE=0
    compare "valley_$frame"
    TOLERANCE=$old_tolerance
done
if [ "$MODERN" = "1" ]; then
    checks=$((checks + 1))
    $VALLEY --frames "$VALLEY_FRAMES" --path=modern --replay "$OUT/valley-current.vkj" --out-dir "$OUT/valley-modern" >"$OUT/valley-modern.log" 2>&1 || fail "valley current journal modern replay"
    for frame in 0000 2400 4800 7199; do
        same "valley-legacy/valley_$frame" "valley-modern/valley_$frame" 0 "valley $frame: journal replay legacy == modern"
    done
fi

begin "== temporal history: warm-up, replay, sync-naive, disabled isolation =="
TEMPORAL="./$BUILD/ex_20_valley --headless --size 160 90 r_shadow_atlas=256 r_overlay=0"
checks=$((checks + 1))
$TEMPORAL --frame 3 --record "$OUT/valley-taa.vkj" --out "$OUT/valley-taa.png" >"$OUT/valley-taa.log" 2>&1 || fail "TAA warm-up render"
checks=$((checks + 1))
grep -q 'simulating preceding frames from 0' "$OUT/valley-taa.log" || fail "TAA failed to disclose warm-up"
checks=$((checks + 1))
$TEMPORAL --frame 3 --path=legacy --replay "$OUT/valley-taa.vkj" --out "$OUT/valley-taa-replay.png" >"$OUT/valley-taa-replay.log" 2>&1 || fail "TAA journal replay"
same valley-taa valley-taa-replay 0 "TAA including history replays exactly on legacy"
checks=$((checks + 1))
$TEMPORAL --frame 3 --sync-naive --out "$OUT/valley-taa-naive.png" >"$OUT/valley-taa-naive.log" 2>&1 || fail "TAA sync-naive render"
same valley-taa valley-taa-naive 0 "TAA history independent of frames-in-flight"
checks=$((checks + 1))
$TEMPORAL --frame 3 +taa 0 --out "$OUT/valley-isolated.png" >"$OUT/valley-isolated.log" 2>&1 || fail "isolated frame with TAA disabled"
checks=$((checks + 1))
if grep -q 'simulating preceding' "$OUT/valley-isolated.log"; then fail "+taa 0 did not restore isolation"; fi

checks=$((checks + 1))
$TEMPORAL --frame 3 +r_host_layouts 0 --out "$OUT/valley-layout-reference.png" >"$OUT/valley-layout-reference.log" 2>&1 || fail "GPU image-layout reference"
same valley-taa valley-layout-reference 0 "host and GPU image transitions produce identical pixels"

# Enforce the release's physical-line budgets, not a hand-edited README count.
begin "== v0.5 line budgets =="
checks=$((checks + 1))
core_lines=$(cat src/vkmin.c src/vkmin.h src/render.c src/render.h src/shared.h src/cvar.c src/cvar.h src/ktx2.c src/ktx2.h src/scene.c src/scene.h src/plat_glfw.c src/plat.h src/spirv.h src/vkmin_math.h src/pack.h src/vkm_format.h src/stb_bridge.c src/stb_bridge.h src/font.h src/jrnl.h | wc -l)
shader_lines=$(cat shaders/*.vert shaders/*.frag shaders/*.comp shaders/*.glsl shaders/lib/*.glsl | wc -l)
valley_lines=$(wc -l < examples/20_valley.c)
note "core $core_lines / 10000; all shaders $shader_lines / 2500; valley $valley_lines / 600"
[ "$core_lines" -lt 10000 ] && [ "$shader_lines" -lt 2500 ] && [ "$valley_lines" -lt 600 ] || fail "v0.5 line budget"
