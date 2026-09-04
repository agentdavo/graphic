/* cvar.c -- see cvar.h. Deliberately a fixed table, not a hash map: there are
 * a few dozen of these, lookups by name happen at parse time only, and a table
 * you can read top to bottom is worth more than O(1). */
#include "cvar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    float value;
    float def;
    const char *help;
} cvar;

/* The table is process-global by design: the demo, the renderer and the
 * command line all need to agree on it, and threading a handle to it through
 * every call would be ceremony without a payoff. It is the one global. */
static cvar table[CV_COUNT] = {
#define VKMIN_CVAR_INIT(n, d, h) {#n, d, d, h},
    VKMIN_CVAR_LIST(VKMIN_CVAR_INIT)
#undef VKMIN_CVAR_INIT
};

float cvar_get(cvar_id id) { return table[id].value; }
bool cvar_get_bool(cvar_id id) { return table[id].value != 0.0f; }
int cvar_get_int(cvar_id id) { return (int)table[id].value; }
void cvar_set(cvar_id id, float value) { table[id].value = value; }
bool cvar_is_overridden(cvar_id id) { return table[id].value != table[id].def; }
const char *cvar_name(cvar_id id) { return table[id].name; }

bool cvar_parse_assignment(const char *text) {
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
        const float value = strtof(eq + 1, &end);
        if (end == eq + 1 || *end != '\0') {
            fprintf(stderr, "cvar: '%s' is not a number for %s\n", eq + 1, table[i].name);
            return false;
        }
        table[i].value = value;
        return true;
    }
    fprintf(stderr, "cvar: unknown cvar '%.*s' (try --cvars)\n", (int)name_len, text);
    return false;
}

void cvar_print_all(void) {
    for (int i = 0; i < CV_COUNT; ++i) {
        printf("  %-18s %-10g %s%s\n", table[i].name, (double)table[i].value, table[i].help,
               table[i].value != table[i].def ? "  [overridden]" : "");
    }
}

int cvar_format_overrides(char *buf, int cap) {
    int n = 0;
    for (int i = 0; i < CV_COUNT && n < cap; ++i) {
        if (table[i].value == table[i].def) continue;
        n += snprintf(buf + n, (size_t)(cap - n), "%s%s=%g", n ? " " : "", table[i].name,
                      (double)table[i].value);
    }
    return n;
}
