/* vkmin_plat.h -- the entire platform surface vkmin depends on: eight functions
 * and one opaque handle. No backend type appears here or in vkmin.h, so adding
 * a backend (XCB, Wayland, Cocoa) is a new .c file and nothing else.
 *
 * Four implementations exist today and are parallel implementations in the
 * CLAUDE.md sense, not a lineage: vkmin_plat_glfw.c, vkmin_plat_sdl2.c,
 * vkmin_plat_sdl3.c, vkmin_plat_win32.c. Exactly one links into a binary,
 * chosen by -DVKMIN_PLATFORM= at CMake time; -DVKMIN_HEADLESS=ON links none and
 * vkmin.c supplies stubs for all eight instead. GLFW's numbering is the
 * reference the other three translate into, because vkmin_inputs carries GLFW
 * key and gamepad-button codes (see VKMIN_KEY_COUNT in vkmin.h).
 *
 * ---- what a backend must guarantee, in the order vkmin relies on it --------
 *
 * 1. Threading. Every function below runs on one thread: whichever thread
 *    opened the first window. Win32 message queues and the GLFW and SDL event
 *    queues are all thread-owned, and a cross-thread call fails far away from
 *    the mistake, so the backends abort at the door instead (check_thread in
 *    vkmin_plat_common.h). With no window open the ownership is unclaimed, so a
 *    process may hand windowing to a different thread between sessions.
 *
 * 2. Call order. vkmin_init opens the window BEFORE it creates the VkInstance,
 *    because it needs the surface extensions the window implies. So
 *    plat_required_instance_extensions is specified to return NULL while no
 *    window is open, and every backend enforces that even where the underlying
 *    API (SDL3, Win32) could answer without one. vkmin asserts on the NULL.
 *
 * 3. Null tolerance. Every function accepts a NULL window and does something
 *    harmless: should_close reports true, the surface is VK_NULL_HANDLE, sizes
 *    are 0, inputs are all-zero. This is the one place vkmin checks; nothing
 *    below re-checks (CLAUDE.md section 7).
 *
 * 4. Out parameters are always written. plat_framebuffer_size and plat_input
 *    zero their outputs first and then fill what they can, so a caller never
 *    reads a stale frame's state after a failure.
 *
 * 5. Process lifetime is refcounted across open windows, not global: the first
 *    plat_window_open starts the underlying library and the last plat_close
 *    stops it. A failed open must leave the count, and the library, exactly as
 *    it found them.
 *
 * 6. Input is per-window and edge-free. plat_input reports level state only --
 *    down, mouse position, wheel, axes, pad_buttons -- and vkmin derives the
 *    `pressed` edges by differencing frames, so a backend never has to. An
 *    unfocused window reports every key and button released. The wheel is an
 *    accumulator drained by the read, so no scroll notch is lost between frames
 *    and none is counted twice.
 */
#ifndef VKMIN_PLAT_H
#define VKMIN_PLAT_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vkmin.h"

typedef struct plat_window plat_window;
plat_window *plat_window_open(int w, int h, const char *title); /* w,h are the client area; NULL title means "vkmin" */
void plat_poll(void);                                  /* pumps the queue for every open window, not just one */
bool plat_should_close(const plat_window *window);     /* sticky once set; the backend never closes a window itself */
void plat_close(plat_window *window);
const char **plat_required_instance_extensions(uint32_t *count); /* backend-owned, valid until plat_close; NULL if no window is open */

VkSurfaceKHR plat_create_surface(const plat_window *window, VkInstance instance); /* caller owns it; vkDestroySurfaceKHR before plat_close */
void plat_framebuffer_size(plat_window *window, int *w, int *h); /* pixels, not points: 0x0 while minimised */
void plat_input(plat_window *window, vkmin_inputs *out); /* fills down, mouse, axes, pad_buttons; the edges are vkmin's */

#endif /* VKMIN_PLAT_H */
