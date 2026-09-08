/* test_units -- vkmin's pure logic and journal framing, with no device.
 *
 * Core units, chosen because each one is wrong quietly rather than loudly:
 *
 *  - the suballocator, which hands out ranges inside one VkDeviceMemory. A
 *    mistake does not fault and no validation layer reports it; two live
 *    resources are simply given overlapping bytes and corrupt each other. The
 *    invariant that matters is the one the arena cannot state for itself, so
 *    this keeps a shadow list of live spans and checks every range against it.
 *  - choose_path, which decides the 1.4-or-1.3 question. Silently upgrading a
 *    forced --path=legacy would leave every cross-path comparison in CI
 *    passing while comparing a path against itself.
 *  - format_lookup and mip_bytes, which size every upload. Block rounding is
 *    the trap: block_dim is 4 for BC and 1 everywhere else, so a slip shows
 *    only on compressed textures and only as a wrong picture.
 *
 * Range arithmetic, decimal parsing and journal framing are also checked.
 * None of it touches a Vulkan entry point: no instance, device or driver.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* VKMIN_FAIL is documented as redefinable before the include, and taking that
 * option is what keeps this program standalone: the real one lives in vkmin.c,
 * so using it would mean linking the whole GPU layer to test arithmetic that
 * touches no GPU. Every case below is a legal call, so a fire here is a genuine
 * defect and aborting is right; a future test of the rejection paths would set
 * a flag instead of aborting, through this same hook.
 *
 * vulkan_core.h is included explicitly because vkmin.h exposes no Vulkan type
 * at all -- the wrapper being thin, not an omission. The arena wants only
 * VkDeviceSize, which is a uint64_t. */
#include <vulkan/vulkan_core.h>

_Noreturn static void arena_fail(const char *file, int line, const char *fmt, ...);
#define VKMIN_FAIL(...) arena_fail(__FILE__, __LINE__, __VA_ARGS__)
#include "vkmin.h"
#include "min_jrnl.h"
#include <stdarg.h>

/* _Noreturn like the real vkmin_fail, or format_lookup appears to fall off
 * the end of a non-void function and -Werror=return-type rejects it. */
_Noreturn static void arena_fail(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s:%d: arena invariant broken: ", file, line);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    abort();
}

/* The arena types are private to vkmin.c, so restate the shapes this exercises.
 * If these drift from vkmin.c the header stops compiling here, which is the
 * point: it is a second reader of the same declarations. */
enum { VKMIN_MAX_RANGES = 2 * (VKMIN_MAX_BUFFERS + VKMIN_MAX_IMAGES + 64 + 8) };
typedef struct { VkDeviceSize offset, size; } arena_range;
typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    VkDeviceSize cap, used, live;
    uint32_t free_count;
    arena_range free_ranges[VKMIN_MAX_RANGES];
    uint32_t type;
} arena;

#include "vkmin_arena.h"

/* choose_path takes a path_caps; restate the two fields it reads. The other
 * three exist in vkmin.c and never reach this decision. */
typedef struct {
    bool host_image_copy, maintenance5, push_descriptor, pipeline_robustness, robust_buffer_access2;
} path_caps;

#include "vkmin_pure.h"

/* ------------------------------------------------------------- harness -- */

static int checks, errors;
static void check(bool ok, const char *what) {
    ++checks;
    if (!ok) { ++errors; printf("  FAIL %s\n", what); }
}

typedef struct { VkDeviceSize offset, size; bool live; } span;

/* The invariant the arena cannot state for itself. */
static bool overlaps(const span *s, uint32_t n, VkDeviceSize offset, VkDeviceSize size) {
    for (uint32_t i = 0; i < n; ++i) {
        if (!s[i].live) continue;
        if (offset < s[i].offset + s[i].size && s[i].offset < offset + size) return true;
    }
    return false;
}

/* Free ranges must stay sorted, disjoint and never adjacent: a hole appears
 * exactly once, or the allocator will hand the same bytes out twice. */
static bool ranges_sane(const arena *a) {
    for (uint32_t i = 0; i < a->free_count; ++i) {
        if (!a->free_ranges[i].size) return false;
        if (a->free_ranges[i].offset + a->free_ranges[i].size > a->used) return false;
        if (i + 1 < a->free_count) {
            const VkDeviceSize end = a->free_ranges[i].offset + a->free_ranges[i].size;
            if (end >= a->free_ranges[i+1].offset) return false;   /* touching counts: coalesce */
        }
    }
    return true;
}

/* A deterministic hash, so the stress pass is the same run to run. */
static uint32_t hash(uint32_t x) {
    x = x * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28) + 4u)) ^ x) * 277803737u;
    return (x >> 22) ^ x;
}

int main(void) {
    check(vkm_range_fits(16,0,16), "whole logical buffer fits");
    check(vkm_range_fits(16,16,0), "empty span at end fits");
    check(!vkm_range_fits(16,17,0), "offset outside logical buffer fails");
    check(!vkm_range_fits(16,8,9), "span past logical end fails");
    check(!vkm_range_fits(16,UINT64_MAX-3,8), "wrapped offset plus length fails");
    check(!vkm_range_fits(128,64,UINT64_MAX), "wrapped ring allocation fails");
    check(vkm_range_fits(UINT64_MAX,UINT64_MAX-7,7), "largest non-wrapping span fits");
    const vkm_decimal zero=vkm_parse_decimal("0",UINT32_MAX), full=vkm_parse_decimal("4294967295",UINT32_MAX);
    check(zero.valid && zero.value==0 && !*zero.end, "decimal zero");
    check(full.valid && full.value==UINT32_MAX && !*full.end, "decimal maximum");
    check(!vkm_parse_decimal("4294967296",UINT32_MAX).valid, "decimal overflow refused");
    check(!vkm_parse_decimal("-1",INT32_MAX).valid && !vkm_parse_decimal("",INT32_MAX).valid, "negative/empty decimal refused");
    check(!vkm_parse_decimal("16",15).valid, "caller decimal limit");
    const vkm_decimal list=vkm_parse_decimal("12,34",INT32_MAX), junk=vkm_parse_decimal("12junk",INT32_MAX);
    check(list.valid && list.value==12 && *list.end==',', "decimal delimiter preserved");
    check(junk.valid && *junk.end=='j', "decimal trailing junk exposed to caller");
    arena a = {.cap = 1u << 20};
    VkDeviceSize off = 0;

    /* Sequential allocation is monotonic and aligned. */
    check(arena_try_alloc(&a, 100, 256, false, &off) && off == 0, "first allocation at zero");
    check(arena_try_alloc(&a, 100, 256, false, &off) && off == 256, "second is aligned up");
    check(a.live == 200, "live counts both");
    check(a.used == 356, "used is the high-water mark");

    /* A hole is reused only when reuse is asked for. */
    arena_release(&a, 0, 100);
    check(a.free_count == 1, "release makes one hole");
    check(arena_try_alloc(&a, 64, 4, false, &off) && off >= 356, "no reuse without the flag");
    check(arena_try_alloc(&a, 64, 4, true, &off) && off == 0, "reuse takes the hole");

    /* Adjacent releases coalesce into one range rather than two. */
    arena a2 = {.cap = 1u << 20};
    VkDeviceSize x = 0, y = 0, z = 0;
    arena_try_alloc(&a2, 128, 1, false, &x);
    arena_try_alloc(&a2, 128, 1, false, &y);
    arena_try_alloc(&a2, 128, 1, false, &z);
    arena_release(&a2, x, 128);
    arena_release(&a2, z, 128);
    check(a2.free_count == 2, "two disjoint holes stay two");
    arena_release(&a2, y, 128);
    check(a2.free_count == 1, "the middle release coalesces all three");
    check(a2.free_ranges[0].offset == 0 && a2.free_ranges[0].size == 384, "into one 384-byte hole");
    check(ranges_sane(&a2), "ranges sane after coalescing");

    /* Exhaustion is a refusal, not a wrong answer. */
    arena a3 = {.cap = 1024};
    check(arena_try_alloc(&a3, 1024, 1, false, &off), "an exact fit is allowed");
    check(!arena_try_alloc(&a3, 1, 1, false, &off), "one byte past capacity is refused");
    check(a3.live == 1024, "a refusal does not change live");

    arena a4 = {.cap = 1024};
    check(!arena_try_alloc(&a4, 2048, 1, false, &off), "larger than capacity is refused");
    check(a4.used == 0 && a4.live == 0, "a refused allocation leaves nothing behind");

    /* Alignment is honoured even out of a reused hole whose base is odd. */
    arena a5 = {.cap = 1u << 20};
    arena_try_alloc(&a5, 300, 1, false, &x);
    arena_release(&a5, 0, 300);
    check(arena_try_alloc(&a5, 16, 256, true, &off) && (off % 256) == 0, "aligned out of a hole");
    check(ranges_sane(&a5), "ranges sane after an aligned reuse");

    /* The real test: thousands of mixed operations, checking after every one
     * that no live range overlaps another and the table stays well formed. */
    arena s = {.cap = 1u << 22};
    enum { SPANS = 512 };
    span live[SPANS];
    memset(live, 0, sizeof live);
    uint32_t count = 0, overlap_faults = 0, sane_faults = 0;
    for (uint32_t step = 0; step < 20000; ++step) {
        const uint32_t r = hash(step);
        const bool freeing = count && (r & 1u) && (count > SPANS / 2 || (r & 2u));
        if (freeing) {
            uint32_t i = hash(r) % count;
            while (i < count && !live[i].live) ++i;
            if (i >= count) continue;
            arena_release(&s, live[i].offset, live[i].size);
            s.live -= live[i].size;
            live[i].live = false;
        } else {
            if (count >= SPANS) continue;
            const VkDeviceSize size = 16 + (hash(r ^ 0x9e3779b9u) % 4096);
            const VkDeviceSize align = (VkDeviceSize)1 << (hash(r ^ 0x85ebca6bu) % 9);
            if (!arena_try_alloc(&s, size, align, true, &off)) continue;
            if (off % align) ++sane_faults;
            if (off + size > s.cap) ++sane_faults;
            if (overlaps(live, count, off, size)) ++overlap_faults;
            live[count++] = (span){off, size, true};
        }
        if (!ranges_sane(&s)) ++sane_faults;
    }
    check(overlap_faults == 0, "no live allocation ever overlapped another");
    check(sane_faults == 0, "range table stayed sorted, disjoint and in bounds");
    check(s.used <= s.cap, "high-water mark never passed capacity");

    /* ---- choose_path: the 1.4-or-1.3 decision, as a table of outcomes ---- */

    const char *why = NULL;
    const path_caps both = {.host_image_copy = true, .maintenance5 = true};
    const path_caps neither = {0};
    const path_caps no_copy = {.maintenance5 = true};
    const path_caps no_m5 = {.host_image_copy = true};

    check(choose_path(both, VKMIN_PATH_AUTO, &why) == VKMIN_PATH_MODERN, "auto takes modern when it can");
    check(choose_path(neither, VKMIN_PATH_AUTO, &why) == VKMIN_PATH_LEGACY, "auto falls to the 1.3 floor");
    check(choose_path(no_copy, VKMIN_PATH_AUTO, &why) == VKMIN_PATH_LEGACY, "one feature short is still legacy");
    check(choose_path(no_m5, VKMIN_PATH_AUTO, &why) == VKMIN_PATH_LEGACY, "the other feature short too");
    /* A forced legacy must be honoured on a device that could do modern. This
     * is what makes the two paths comparable on one machine, and every
     * cross-path check in CI rests on it. */
    check(choose_path(both, VKMIN_PATH_LEGACY, &why) == VKMIN_PATH_LEGACY, "legacy is honoured on a modern device");
    check(choose_path(both, VKMIN_PATH_MODERN, &why) == VKMIN_PATH_MODERN, "modern is honoured when available");
    /* The reason string is what the device line prints, so a reader can tell a
     * fallback from a choice. Wrong text is a lie in the log, not a crash. */
    check(choose_path(neither, VKMIN_PATH_AUTO, &why) == VKMIN_PATH_LEGACY &&
          strcmp(why, "no hostImageCopy") == 0, "reason names the missing feature");
    check(choose_path(no_m5, VKMIN_PATH_AUTO, &why) == VKMIN_PATH_LEGACY &&
          strcmp(why, "no maintenance5") == 0, "and names the other one");

    /* ---- format sizes: the block-dimension rounding is the trap ---- */

    const format_info rgba8 = format_lookup(VKMIN_FMT_RGBA8_UNORM);
    check(rgba8.block_bytes == 4 && rgba8.block_dim == 1, "rgba8 is one byte-quad per texel");
    check(mip_bytes(rgba8, 16, 16) == 1024, "16x16 rgba8 is 1024 bytes");
    check(mip_bytes(rgba8, 1, 1) == 4, "a 1x1 rgba8 mip is 4 bytes");

    const format_info bc1 = format_lookup(VKMIN_FMT_BC1_UNORM);
    check(bc1.block_dim == 4 && bc1.block_bytes == 8, "bc1 is 8 bytes per 4x4 block");
    check(mip_bytes(bc1, 16, 16) == 128, "16x16 bc1 is 16 blocks of 8");
    /* Rounding up is the point: a 1x1 BC image still costs a whole block, and
     * rounding down would under-size the upload and read past the source. */
    check(mip_bytes(bc1, 1, 1) == 8, "a 1x1 bc1 mip still costs one whole block");
    check(mip_bytes(bc1, 5, 5) == 32, "5x5 bc1 rounds up to 2x2 blocks");

    const format_info bc5 = format_lookup(VKMIN_FMT_BC5_UNORM);
    check(bc5.block_bytes == 16 && bc5.block_dim == 4, "bc5 is 16 bytes per block");
    check(mip_bytes(bc5, 4, 4) == 16, "4x4 bc5 is exactly one block");

    check(format_lookup(VKMIN_FMT_D32_FLOAT).aspect == VK_IMAGE_ASPECT_DEPTH_BIT, "depth carries the depth aspect");
    check(format_lookup(VKMIN_FMT_RGBA8_UNORM).aspect == VK_IMAGE_ASPECT_COLOR_BIT, "colour carries colour");

    /* The switch has no default, so a new enumerator without a case is already
     * a compile error. What that cannot catch is an entry that compiles but
     * maps to nothing usable, so walk every declared format. */
    bool every_format_sized = true;
    for (int f = 0; f < VKMIN_FMT_NONE; ++f) {
        const format_info fi = format_lookup((vkmin_format)f);
        if (fi.vk == VK_FORMAT_UNDEFINED || !fi.block_bytes || !fi.block_dim) every_format_sized = false;
    }
    check(every_format_sized, "every format maps to a real VkFormat with a size");

    bool all_sample_bits=true;
    for (uint32_t n=1;n<=64;n<<=1) all_sample_bits &= vkm_choose_samples(n,n)==n;
    check(all_sample_bits,"every sample bit including 16x/32x/64x is preserved");
    check(vkm_choose_samples(1|16|32,64)==32,"64x falls back to supported 32x");
    check(vkm_choose_samples(1|16|64,32)==16,"32x falls back to supported 16x without upgrading");
    check(vkm_choose_samples(1,64)==1,"unsupported MSAA falls back to single sample");
    check(vkm_choose_samples(0,64)==0,"empty capability mask cannot negotiate");
    check(vkm_choose_samples(127,3)==0 && vkm_choose_samples(127,128)==0 && vkm_choose_samples(127,0)==0,"invalid preferences are refused");

    /* Relocation framing must distinguish absent, complete and truncated data. */
    FILE *journal = tmpfile();
    check(journal != NULL, "journal fixture opens");
    if (journal) {
        const jrnl_reloc source = {0};
        jrnl_reloc decoded;
        jrnl_record record = {0};
        check(jrnl_record_read(journal, &record, NULL, 0, NULL, 0, NULL, 0), "zero relocations need no storage");
        record.reloc_count = 1;
        check(!jrnl_record_read(journal, &record, NULL, 0, NULL, 0, &decoded, 1), "missing relocation is rejected");
        check(fseek(journal, 0, SEEK_SET) == 0 && fwrite(&source, sizeof source, 1, journal) == 1 &&
              fseek(journal, 0, SEEK_SET) == 0 &&
              jrnl_record_read(journal, &record, NULL, 0, NULL, 0, &decoded, 1), "complete relocation is read");
        check(fseek(journal, 1, SEEK_SET) == 0 &&
              !jrnl_record_read(journal, &record, NULL, 0, NULL, 0, &decoded, 1), "partial relocation is rejected");
        fclose(journal);
    }

    printf("pure units: %d checks, %d failures\n", checks, errors);
    return errors ? 1 : 0;
}
