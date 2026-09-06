/* cprof: a portable function profiler for measurement builds.
 *
 * gprof's sampler does not run under the MinGW runtime, so this uses the
 * compiler's entry/exit hooks instead. Build the program with
 * -finstrument-functions and link this object; at exit it writes the top
 * functions by self time to the file named by CPROF_OUT (default cprof.txt),
 * one line each: offset-from-image-base, calls, self ms, inclusive ms.
 * tools/cprof_resolve.py turns the offsets into names with nm.
 *
 * Single-threaded by design: it profiles the main thread and ignores the
 * audio callback, whose cost sndmin already reports as cpu_percent. The
 * hooks themselves are excluded from instrumentation.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#define NO_INSTRUMENT __attribute__((no_instrument_function))
enum { CPROF_SLOTS = 8192, CPROF_DEPTH = 512, CPROF_REPORT = 60 };
typedef struct { const void *fn; uint64_t calls, inclusive, self; } cprof_entry;
typedef struct { const void *fn; uint64_t start, children; } cprof_frame;
static cprof_entry cprof_table[CPROF_SLOTS];
static cprof_frame cprof_stack[CPROF_DEPTH];
static int cprof_depth, cprof_overflow;
#ifdef _WIN32
static int cprof_main_thread_only;
static DWORD cprof_thread;
#endif

static NO_INSTRUMENT uint64_t cprof_now(void) {
#ifdef _WIN32
    LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)t.QuadPart;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
#endif
}
static NO_INSTRUMENT double cprof_ms(uint64_t ticks) {
#ifdef _WIN32
    static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    return (double)ticks * 1000.0 / (double)f.QuadPart;
#else
    return (double)ticks / 1e6;
#endif
}
#ifdef _WIN32
/* Only the first thread to arrive is profiled; the audio callback is not. */
static NO_INSTRUMENT int cprof_off_thread(void) {
    if (!cprof_main_thread_only) { cprof_thread = GetCurrentThreadId(); cprof_main_thread_only = 1; }
    return GetCurrentThreadId() != cprof_thread;
}
#else
#define cprof_off_thread() 0 /* single-threaded builds elsewhere */
#endif
static NO_INSTRUMENT cprof_entry *cprof_find(const void *fn) {
    uintptr_t h = (uintptr_t)fn; h ^= h >> 17; h *= 0x9E3779B97F4A7C15ull; h ^= h >> 29;
    for (int probe = 0; probe < CPROF_SLOTS; ++probe) {
        cprof_entry *e = &cprof_table[(h + (uintptr_t)probe) % CPROF_SLOTS];
        if (e->fn == fn) return e;
        if (!e->fn) { e->fn = fn; return e; }
    }
    return NULL; /* table full: the call is counted nowhere, and reported */
}
static NO_INSTRUMENT int cprof_by_self(const void *a, const void *b) {
    const cprof_entry *x = a, *y = b;
    return x->self < y->self ? 1 : x->self > y->self ? -1 : 0;
}
static NO_INSTRUMENT void cprof_report(void) {
    const char *path = getenv("CPROF_OUT");
    FILE *out = fopen(path ? path : "cprof.txt", "w");
    if (!out) return;
#ifdef _WIN32
    const uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
#else
    const uintptr_t base = 0;
#endif
    static cprof_entry sorted[CPROF_SLOTS];
    int n = 0;
    for (int i = 0; i < CPROF_SLOTS; ++i) if (cprof_table[i].fn) sorted[n++] = cprof_table[i];
    qsort(sorted, (size_t)n, sizeof *sorted, cprof_by_self);
    fprintf(out, "# offset calls self_ms inclusive_ms  (%d functions seen, %s)\n", n,
            cprof_overflow ? "STACK OVERFLOWED: nesting deeper than the profiler's stack" : "stack ok");
    for (int i = 0; i < n && i < CPROF_REPORT; ++i)
        fprintf(out, "%#" PRIxPTR " %" PRIu64 " %.3f %.3f\n", (uintptr_t)sorted[i].fn - base, sorted[i].calls,
                cprof_ms(sorted[i].self), cprof_ms(sorted[i].inclusive));
    fclose(out);
}

/* GCC declares both hooks with plain pointers and rejects any other
 * prototype; neither writes through them. */
NO_INSTRUMENT void __cyg_profile_func_enter(void *fn, void *site);
NO_INSTRUMENT void __cyg_profile_func_exit(void *fn, void *site);
// cppcheck-suppress constParameterPointer ; the prototype is GCC's, not ours
void __cyg_profile_func_enter(void *fn, void *site) {
    (void)site;
    if (cprof_off_thread()) return;
    static int registered; if (!registered) { registered = 1; atexit(cprof_report); }
    if (cprof_depth >= CPROF_DEPTH) { cprof_overflow = 1; ++cprof_depth; return; }
    cprof_stack[cprof_depth++] = (cprof_frame){fn, cprof_now(), 0};
}
// cppcheck-suppress constParameterPointer ; the prototype is GCC's, not ours
void __cyg_profile_func_exit(void *fn, void *site) {
    (void)site;
    if (cprof_off_thread()) return;
    if (cprof_depth > CPROF_DEPTH) { --cprof_depth; return; }
    if (cprof_depth <= 0) return;
    const cprof_frame f = cprof_stack[--cprof_depth];
    const uint64_t inclusive = cprof_now() - f.start;
    cprof_entry *e = cprof_find(fn);
    if (e) { ++e->calls; e->inclusive += inclusive; e->self += inclusive - f.children; }
    if (cprof_depth > 0) cprof_stack[cprof_depth - 1].children += inclusive;
}
