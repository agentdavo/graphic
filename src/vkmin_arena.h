/* Internal suballocator: address-ordered free ranges over one device
 * allocation. Extracted from vkmin.c so it can be exercised on its own, not to
 * widen the public surface -- nothing here appears in vkmin.h, and the same
 * reasoning that put vkmin_spirv.h in its own file applies more strongly here.
 *
 * This is the most dangerous arithmetic in the tree. Every buffer and image
 * vkmin owns is a range inside one VkDeviceMemory, so an off-by-one does not
 * fault: it hands out a range that overlaps a live resource, and the two
 * quietly write over each other. No validation layer can see that, because
 * nothing illegal has been asked of Vulkan -- the offsets are vkmin's own
 * invention, which section 7 names as exactly the case we must check ourselves.
 *
 * It touches no Vulkan entry point. `arena` carries the VkBuffer and
 * VkDeviceMemory it describes, but the allocator reads only cap, used, live
 * and the range table, so it can be driven from a test with no device, no
 * instance and no GPU present.
 *
 * The invariants, which the asserts below enforce and a test can check:
 *   - free_ranges is sorted by offset and no two entries touch or overlap;
 *     adjacent ranges are always coalesced, so a hole appears exactly once.
 *   - `used` is a high-water mark and never moves backwards. Old journals
 *     retain their original monotonic offsets and device_used keeps meaning.
 *   - `live` is the sum of the outstanding allocations, so it falls only when
 *     a range is released and never exceeds `used`.
 *   - Every returned offset is aligned, and lies wholly within `cap`.
 *
 * Included by vkmin.c after the context types, in the style of vkmin_spirv.h.
 * A caller that wants it alone needs VkDeviceSize, VKMIN_ASSERT and the arena
 * types in scope first; both are satisfied by including vkmin.h.
 */
#ifndef VKMIN_ARENA_H
#define VKMIN_ARENA_H

static VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) & ~(a - 1); } // pure

/* Address-ordered reusable holes. The high-water cursor never moves backwards:
 * old journals retain their original monotonic offsets and device_used meaning. */
static void arena_release(arena *a, VkDeviceSize offset, VkDeviceSize size) {
    VKMIN_ASSERT(size && offset <= a->used && size <= a->used - offset, "invalid arena release");
    uint32_t at = 0;
    while (at < a->free_count && a->free_ranges[at].offset < offset) ++at;
    VKMIN_ASSERT(at == 0 || a->free_ranges[at-1].offset + a->free_ranges[at-1].size <= offset, "arena overlap");
    VKMIN_ASSERT(at == a->free_count || offset + size <= a->free_ranges[at].offset, "arena overlap");
    if (at && a->free_ranges[at-1].offset + a->free_ranges[at-1].size == offset) {
        --at; a->free_ranges[at].size += size;
    } else {
        VKMIN_ASSERT(a->free_count < VKMIN_MAX_RANGES, "arena metadata exhausted");
        memmove(&a->free_ranges[at+1], &a->free_ranges[at], (a->free_count-at)*sizeof(arena_range));
        a->free_ranges[at] = (arena_range){offset,size}; ++a->free_count;
    }
    if (at+1 < a->free_count && a->free_ranges[at].offset + a->free_ranges[at].size == a->free_ranges[at+1].offset) {
        a->free_ranges[at].size += a->free_ranges[at+1].size;
        memmove(&a->free_ranges[at+1], &a->free_ranges[at+2], (a->free_count-at-2)*sizeof(arena_range));
        --a->free_count;
    }
}

static bool arena_try_alloc(arena *a, VkDeviceSize size, VkDeviceSize alignment, bool reuse, VkDeviceSize *result) {
    VKMIN_ASSERT(size && alignment && !(alignment & (alignment-1)), "invalid arena allocation");
    if (reuse) for (uint32_t i = 0; i < a->free_count; ++i) {
        const arena_range r = a->free_ranges[i];
        if (r.offset > UINT64_MAX-(alignment-1)) continue;
        const VkDeviceSize off = align_up(r.offset, alignment), padding = off-r.offset;
        /* A free tail can grow into virgin space without moving any allocation. */
        const VkDeviceSize available = r.offset+r.size == a->used ? a->cap-r.offset : r.size;
        if (padding > available || size > available-padding) continue;
        memmove(&a->free_ranges[i], &a->free_ranges[i+1], (--a->free_count-i)*sizeof(arena_range));
        if (off+size > a->used) a->used = off+size;
        if (padding) arena_release(a, r.offset, padding);
        if (padding < r.size && size < r.size-padding) arena_release(a, off+size, r.size-padding-size);
        a->live += size; *result = off; return true;
    }
    if (a->used > UINT64_MAX-(alignment-1)) return false;
    const VkDeviceSize off = align_up(a->used, alignment);
    if (off > a->cap || size > a->cap-off) return false;
    const VkDeviceSize previous = a->used;
    a->used = off+size; a->live += size;
    if (reuse && off > previous) arena_release(a, previous, off-previous);
    *result = off; return true;
}

#endif
