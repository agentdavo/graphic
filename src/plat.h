/* plat.h -- the entire platform surface vkmin depends on.
 *
 * Deliberately tiny so a raw XCB or Win32 backend can be dropped in beside
 * plat_glfw.c as a parallel implementation without touching vkmin.c. No
 * backend types appear here or in vkmin.h.
 *
 * The brief asked for five functions. It is six plus two queries, because
 * surface creation, framebuffer size and key edges cannot be expressed in
 * terms of the other five and every backend has to provide them anyway.
 */
#ifndef VKMIN_PLAT_H
#define VKMIN_PLAT_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

bool plat_window_open(int w, int h, const char *title);
void plat_poll(void);
bool plat_should_close(void);
void plat_close(void);
const char **plat_required_instance_extensions(uint32_t *count);

VkSurfaceKHR plat_create_surface(VkInstance instance);
void plat_framebuffer_size(int *w, int *h);
bool plat_key_hit(int key); /* true once per press */

#endif /* VKMIN_PLAT_H */
