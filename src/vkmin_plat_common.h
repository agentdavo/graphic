/* vkmin_plat_common.h -- the bookkeeping every backend was repeating verbatim.
 *
 * Exactly one backend .c compiles into a binary, so this header can own the
 * shared state outright without any risk of colliding definitions.
 *
 * Include it *after* the backend's own `struct plat_window`, which must carry a
 * `next` link; everything else about the struct is the backend's business.
 */
#ifndef VKMIN_PLAT_COMMON_H
#define VKMIN_PLAT_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static plat_window *window_list;
static unsigned window_count;
#ifdef _WIN32
static DWORD owner_thread;
#else
static pthread_t owner_thread;
#endif

/* Every window call has to come from the thread that opened the first window:
 * Win32 message queues and the GLFW and SDL event queues are all thread-owned,
 * and a cross-thread call fails far away from the mistake. Abort at the door. */
static inline void check_thread(void) {
#ifdef _WIN32
    const bool same_thread = !window_count || owner_thread == GetCurrentThreadId();
#else
    const bool same_thread = !window_count || pthread_equal(owner_thread, pthread_self());
#endif
    if (!same_thread) {
        fprintf(stderr, "plat: call every window operation on the main thread\n"); abort();
    }
}

static inline void claim_thread(void) {
#ifdef _WIN32
    owner_thread = GetCurrentThreadId();
#else
    owner_thread = pthread_self();
#endif
}

/* window_count is the process-wide startup/teardown refcount, so it moves only
 * here: linking and counting cannot drift apart if they are the same call. */
static inline void window_link(plat_window *window) {
    window->next = window_list;
    window_list = window;
    ++window_count;
}

static inline void window_unlink(plat_window *window) {
    for (plat_window **link = &window_list; *link; link = &(*link)->next) {
        if (*link == window) { *link = window->next; break; }
    }
    --window_count;
}

#endif /* VKMIN_PLAT_COMMON_H */
