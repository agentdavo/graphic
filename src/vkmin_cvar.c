/* vkmin_cvar.c -- see cvar.h. Deliberately a fixed table, not a hash map: there are
 * a few dozen of these, lookups by name happen at parse time only, and a table
 * you can read top to bottom is worth more than O(1). */
#include "vkmin_cvar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>

typedef struct {
    const char *name;
    float def;
    const char *help;
} cvar;

static const cvar table[CV_COUNT] = {
#define VKMIN_CVAR_INIT(n, d, h) {#n, d, h},
    VKMIN_CVAR_LIST(VKMIN_CVAR_INIT)
#undef VKMIN_CVAR_INIT
};

void cvar_init(cvar_state *state) {
    *state = (cvar_state){0};
    for (int i = 0; i < CV_COUNT; ++i) state->values[i] = table[i].def;
}

static bool valid_value(cvar_id id, float value) {
    if (id < 0 || id >= CV_COUNT || !isfinite(value) || (double)value < INT_MIN || (double)value > INT_MAX) return false;
    switch (id) {
    case CV_r_width: case CV_r_height: case CV_r_shadow_atlas: return value >= 1 && value <= 32768;
    case CV_r_path: return value >= 0 && value <= 2;
    case CV_d_frame_step: case CV_r_max_lights: case CV_r_shadow_lights: return value >= 0;
    /* A reservation below a megabyte cannot hold even one backbuffer, and the
     * ceiling keeps a typo from asking a driver for a terabyte. */
    case CV_r_arena_mb: case CV_r_image_arena_mb: case CV_r_ring_mb: return value >= 1 && value <= 16384;
    case CV_r_omega_shadow: return value >= 64 && value <= 8192;
    default: return true;
    }
}

static bool init_only(cvar_id id) {
    return id == CV_r_width || id == CV_r_height || id == CV_r_path || id == CV_r_host_layouts ||
        id == CV_r_sync_naive || id == CV_r_readback || id == CV_r_vsync || id == CV_r_shadow_atlas ||
        id == CV_r_arena_mb || id == CV_r_image_arena_mb || id == CV_r_ring_mb || id == CV_r_default_depth ||
        id == CV_r_hdr_packed || id == CV_r_omega_shadow;
}

float cvar_get(const cvar_state *state, cvar_id id) {
    if (!state || id < 0 || id >= CV_COUNT || !valid_value(id, state->values[id])) abort();
    return state->values[id];
}
bool cvar_get_bool(const cvar_state *state, cvar_id id) { return cvar_get(state, id) != 0.0f; }
int cvar_get_int(const cvar_state *state, cvar_id id) { return (int)cvar_get(state, id); }
void cvar_set(cvar_state *state, cvar_id id, float value) {
    if (!state || !valid_value(id, value) || (state->locked && init_only(id) && value != state->values[id])) {
        fprintf(stderr, "cvar: invalid value or edit to an init-only setting\n"); abort();
    }
    state->values[id] = value;
}
bool cvar_is_overridden(const cvar_state *state, cvar_id id) { return cvar_get(state, id) != table[id].def; }
bool cvar_was_set(const cvar_state *state, cvar_id id) { (void)cvar_get(state, id); return state->assigned[id]; }
const char *cvar_name(cvar_id id) { return table[id].name; }

bool cvar_parse_assignment(cvar_state *state, const char *text) {
    const char *eq = strchr(text, '=');
    if (!eq || eq == text) {
        fprintf(stderr, "cvar: expected name=value, got '%s'\n", text);
        return false;
    }
    const size_t name_len = (size_t)(eq - text);
    for (int i = 0; i < CV_COUNT; ++i) {
        if (strlen(table[i].name) != name_len || strncmp(table[i].name, text, name_len) != 0) {
            continue;
        }
        char *end = NULL;
        errno = 0;
        const float value = strtof(eq + 1, &end);
        if (end == eq + 1 || *end != '\0' || errno == ERANGE || !valid_value((cvar_id)i, value) ||
            (state->locked && init_only((cvar_id)i) && value != state->values[i])) {
            fprintf(stderr, "cvar: invalid value '%s' or init-only edit for %s\n", eq + 1, table[i].name);
            return false;
        }
        state->values[i] = value;
        state->assigned[i] = true;
        return true;
    }
    fprintf(stderr, "cvar: unknown cvar '%.*s' (try --cvars)\n", (int)name_len, text);
    return false;
}

void cvar_print_all(const cvar_state *state) {
    for (int i = 0; i < CV_COUNT; ++i) {
        printf("  %-18s %-10g %s%s\n", table[i].name, (double)state->values[i], table[i].help,
               state->values[i] != table[i].def ? "  [overridden]" : "");
    }
}

int cvar_format_overrides(const cvar_state *state, char *buf, int cap) {
    int n = 0;
    for (int i = 0; i < CV_COUNT && n < cap; ++i) {
        if (state->values[i] == table[i].def && !state->assigned[i]) continue;
        n += snprintf(buf + n, (size_t)(cap - n), "%s%s=%g", n ? " " : "", table[i].name,
                      (double)state->values[i]);
    }
    return n;
}
