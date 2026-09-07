/* test_arena -- drives vkmin's suballocator directly, with no device.
 *
 * The allocator hands out ranges inside one VkDeviceMemory. A mistake there
 * does not fault and no validation layer reports it: two live resources are
 * simply given overlapping bytes and quietly corrupt each other. That is the
 * "preconditions of our own invention" case, so it is ours to check.
 *
 * It reads only cap, used, live and the range table, so this needs no
 * instance, no device and no GPU -- which is why it can run in CI on a machine
 * with no driver at all, unlike every other check in this tree.
 *
 * The invariant that matters most is the one the arena cannot state for
 * itself: no two live allocations may overlap. So the test keeps its own
 * shadow list and checks every returned range against it.
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

static void arena_fail(const char *file, int line, const char *fmt, ...);
#define VKMIN_FAIL(...) arena_fail(__FILE__, __LINE__, __VA_ARGS__)
#include "vkmin.h"
#include <stdarg.h>

static void arena_fail(const char *file, int line, const char *fmt, ...) {
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

    printf("arena: %d checks, %d failures\n", checks, errors);
    return errors ? 1 : 0;
}
