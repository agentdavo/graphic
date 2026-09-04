/* Internal SPIR-V push-layout reader. Bounded input, explicit failure, no Vulkan.
 * Decorations, not a guessed C layout, determine matrix and array strides. */
#ifndef VKMIN_SPIRV_H
#define VKMIN_SPIRV_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct { const uint32_t *w, *def; size_t n; uint32_t bound; } vkm_spv;

static inline bool vkm_spv_size(const vkm_spv *v, uint32_t id, uint32_t stride,
                                bool row_major, unsigned depth, uint32_t *out) {
    if (id >= v->bound || !v->def[id] || depth > 32) return false;
    const uint32_t *p = v->w + v->def[id];
    const uint32_t code = p[0] & 65535u, wc = p[0] >> 16;
    uint32_t element = 0;
    uint64_t size = 0;
    switch (code) {
    case 21: case 22: /* scalar */
        if (p[2] != 8 && p[2] != 16 && p[2] != 32 && p[2] != 64) return false;
        size = p[2] / 8; break;
    case 23: /* vector */
        if (!vkm_spv_size(v, p[2], 0, false, depth + 1, &element) || p[3] < 2 || p[3] > 4) return false;
        size = (uint64_t)element * p[3]; break;
    case 24: { /* matrix; MatrixStride decorates the containing member */
        if (!stride || p[2] >= v->bound || !v->def[p[2]]) return false;
        const uint32_t *col = v->w + v->def[p[2]];
        if ((col[0] & 65535u) != 23 || p[3] < 2 || p[3] > 4) return false;
        if (!vkm_spv_size(v, col[2], 0, false, depth + 1, &element)) return false;
        const uint32_t minor = row_major ? p[3] : col[3], major = row_major ? col[3] : p[3];
        if (minor < 2 || minor > 4 || major < 2 || major > 4 || stride < element * minor) return false;
        size = (uint64_t)stride * major; break;
    }
    case 28: { /* fixed array */
        if (p[3] >= v->bound || !v->def[p[3]]) return false;
        const uint32_t *count = v->w + v->def[p[3]];
        if ((count[0] & 65535u) != 43 || (count[0] >> 16) != 4 || !count[3]) return false;
        if (!vkm_spv_size(v, p[2], stride, row_major, depth + 1, &element)) return false;
        uint32_t array_stride = 0;
        for (size_t i = 5; i < v->n; i += v->w[i] >> 16) {
            const uint32_t *d = v->w + i;
            if ((d[0] & 65535u) == 71 && d[1] == id && d[2] == 6) array_stride = d[3];
        }
        if (array_stride < element) return false;
        size = (uint64_t)array_stride * count[3]; break;
    }
    case 30: /* struct */
        for (uint32_t member = 0; member + 2 < wc; ++member) {
            uint32_t offset = UINT32_MAX, matrix_stride = 0;
            bool row = false;
            for (size_t i = 5; i < v->n; i += v->w[i] >> 16) {
                const uint32_t *d = v->w + i;
                if ((d[0] & 65535u) != 72 || d[1] != id || d[2] != member) continue;
                if (d[3] == 35) offset = d[4];
                if (d[3] == 7) matrix_stride = d[4];
                if (d[3] == 4) row = true;
            }
            if (offset == UINT32_MAX || !vkm_spv_size(v, p[2 + member], matrix_stride, row, depth + 1, &element)) return false;
            const uint64_t end = (uint64_t)offset + element;
            if (end > size) size = end;
        }
        break;
    case 32: /* a physical buffer reference stored in a push block */
        if (p[2] != 5349) return false;
        size = 8; break;
    default: return false;
    }
    if (size > UINT32_MAX) return false;
    *out = (uint32_t)size;
    return true;
}

static inline bool vkm_spirv_push_size(const void *data, size_t bytes, uint32_t *out) {
    if (!data || !out || bytes < 20 || bytes % 4 || (uintptr_t)data % 4) return false;
    const uint32_t *w = data;
    const size_t n = bytes / 4;
    if (w[0] != 0x07230203u || !w[3] || w[3] > 65536u || w[4]) return false;
    uint32_t *def = calloc(65536, sizeof *def); /* fixed bound, independent of untrusted size */
    if (!def) return false;
    bool ok = true;
    uint32_t push_type = 0;
    for (size_t i = 5; i < n;) {
        const uint32_t code = w[i] & 65535u, wc = w[i] >> 16;
        if (!wc || wc > n - i) { ok = false; break; }
        uint32_t min = 1, result = 0;
        if (code >= 21 && code <= 32) min = 2;
        if (code == 21) min = 4;
        if (code == 22) min = 3;
        if (code == 23 || code == 24 || code == 28 || code == 32 || code == 43 || code == 59) min = 4;
        if (code == 30) min = 2;
        if (code == 71) min = 3;
        if (code == 72) min = 4;
        if (wc < min) { ok = false; break; }
        if ((code == 71 && w[i + 2] == 6 && wc < 4) ||
            (code == 72 && (w[i + 3] == 7 || w[i + 3] == 35) && wc < 5)) { ok = false; break; }
        if (code >= 21 && code <= 32) result = w[i + 1];
        if (code == 43 || code == 59) result = w[i + 2];
        if (result) {
            if (result >= w[3] || def[result]) { ok = false; break; }
            def[result] = (uint32_t)i;
        }
        if (code == 59 && w[i + 3] == 9) {
            if (push_type) { ok = false; break; }
            push_type = w[i + 1];
        }
        i += wc;
    }
    *out = 0;
    if (ok && push_type) {
        ok = push_type < w[3] && def[push_type] != 0;
        if (ok) {
            const uint32_t *p = w + def[push_type];
            const vkm_spv v = {w, def, n, w[3]};
            ok = (p[0] & 65535u) == 32 && p[2] == 9 && vkm_spv_size(&v, p[3], 0, false, 0, out);
        }
    }
    free(def);
    return ok;
}
#endif
