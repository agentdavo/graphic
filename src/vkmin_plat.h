/* vkmin_plat.h -- the entire platform surface vkmin depends on.
 *
 * Deliberately tiny so a raw XCB or Win32 backend can be dropped in beside
 * vkmin_plat_glfw.c as a parallel implementation without touching vkmin.c. No
 * backend types appear here or in vkmin.h.
 *
 * All functions run on the main thread. Each window owns its input state;
 * polling pumps events for every open window. The backend balances process
 * startup/teardown across all open windows.
 */
#ifndef VKMIN_PLAT_H
#define VKMIN_PLAT_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vkmin.h"

typedef struct plat_window plat_window;
plat_window *plat_window_open(int w, int h, const char *title);
void plat_poll(void);
bool plat_should_close(const plat_window *window);
void plat_close(plat_window *window);
const char **plat_required_instance_extensions(uint32_t *count);

VkSurfaceKHR plat_create_surface(const plat_window *window, VkInstance instance);
void plat_framebuffer_size(plat_window *window, int *w, int *h);
void plat_input(plat_window *window, vkmin_inputs *out); /* fills down, mouse, axes, pad_buttons; the edges are vkmin's */

#endif /* VKMIN_PLAT_H */
