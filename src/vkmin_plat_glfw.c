/* vkmin_plat_glfw.c -- GLFW backend for vkmin_plat.h. The only file that knows
 * GLFW exists. Built by -DVKMIN_PLATFORM=glfw.
 *
 * The shortest of the four backends, and the reason the other three are longer:
 * vkmin_inputs carries GLFW's key and gamepad-button numbering, so here the
 * translation tables are empty. Every mapping is the identity, which is why
 * there is no equivalent of vkmin_plat_sdl.h or plat_win32_key on this side.
 *
 * Two things this backend does NOT do, which the others must do by hand:
 *   - no focus gate. GLFW already delivers releases on focus loss, so an
 *     unfocused window reads all-released from glfwGetKey without help.
 *   - no per-window event routing. GLFW's input is already per-window; SDL's
 *     and Win32's are process- or thread-wide and have to be narrowed.
 * Their absence here is the contract being met cheaply, not an omission.
 */
#include "vkmin_plat.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

struct plat_window { GLFWwindow *handle; struct plat_window *next; double wheel; };

#include "vkmin_plat_common.h"

/* Scroll arrives as an event, not as pollable state, so it has to be banked
 * between plat_input calls or notches are lost. Horizontal scroll is discarded:
 * vkmin_inputs has one wheel field. */
static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)dx;
    plat_window *window = glfwGetWindowUserPointer(w);
    if (window) window->wheel += dy;
}

plat_window *plat_window_open(int w, int h, const char *title) {
    check_thread();
    if (!window_count && !glfwInit()) return NULL;
    if (!glfwVulkanSupported()) {
        if (!window_count) glfwTerminate();
        return NULL;
    }
    plat_window *window = calloc(1, sizeof *window);
    if (!window) { if (!window_count) glfwTerminate(); return NULL; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window->handle = glfwCreateWindow(w, h, title ? title : "vkmin", NULL, NULL);
    if (!window->handle) {
        free(window);
        if (!window_count) glfwTerminate();
        return NULL;
    }
    if (!window_count) claim_thread();
    window_link(window);
    glfwSetWindowUserPointer(window->handle, window);
    glfwSetScrollCallback(window->handle, on_scroll);
    return window;
}

void plat_poll(void) { check_thread(); if (window_count) glfwPollEvents(); }
bool plat_should_close(const plat_window *window) {
    check_thread();
    return !window || glfwWindowShouldClose(window->handle) != 0;
}
void plat_close(plat_window *window) {
    check_thread();
    if (!window) return;
    glfwDestroyWindow(window->handle);
    window_unlink(window);
    free(window);
    if (!window_count) glfwTerminate();
}
/* GLFW cannot answer before glfwInit, and vkmin_init opens the window before it
 * creates the instance, so gating on window_count costs nothing and keeps this
 * backend's contract identical to the three that could answer without one. */
const char **plat_required_instance_extensions(uint32_t *count) {
    check_thread();
    *count = 0;
    return window_count ? glfwGetRequiredInstanceExtensions(count) : NULL;
}

VkSurfaceKHR plat_create_surface(const plat_window *window, VkInstance instance) {
    check_thread();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!window) return VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, window->handle, NULL, &surface) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return surface;
}

void plat_framebuffer_size(plat_window *window, int *w, int *h) {
    check_thread();
    *w = 0;
    *h = 0;
    if (window) glfwGetFramebufferSize(window->handle, w, h);
}

void plat_input(plat_window *window, vkmin_inputs *out) {
    check_thread();
    *out = (vkmin_inputs){0};
    if (!window) return;
    /* The identity mapping: a GLFW key code IS the bit index in out->down. 32 is
     * GLFW_KEY_SPACE, the lowest code GLFW defines. Both bounds are load-bearing
     * -- GLFW_KEY_LAST stops glfwGetKey returning GLFW_KEY_UNKNOWN's error, and
     * VKMIN_KEY_COUNT stops the shift walking off the end of out->down. */
    for (int key = 32; key < GLFW_KEY_LAST && key < (int)VKMIN_KEY_COUNT; ++key) {
        if (glfwGetKey(window->handle, key) == GLFW_PRESS) out->down[key / 32] |= 1u << (key % 32);
    }
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window->handle, &mx, &my);
    out->mouse_x = (float)mx;
    out->mouse_y = (float)my;
    out->wheel = (float)window->wheel;
    window->wheel = 0.0;
    /* GLFW numbers left/right/middle 0,1,2 and VKMIN_MOUSE_LEFT/RIGHT/MIDDLE are
     * 1,2,4, so the shift is the mapping. The SDL and Win32 backends have to
     * spell the names out because their button orders differ. */
    for (int b = 0; b < 3; ++b) {
        if (glfwGetMouseButton(window->handle, b) == GLFW_PRESS) out->buttons |= 1u << b;
    }
    /* Only joystick 1, and only if GLFW has a gamepad mapping for it: an
     * unmapped stick has no agreed button order to report. axes[] is already in
     * vkmin's layout (left xy, right xy, triggers) and already -1..1. */
    GLFWgamepadstate pad;
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, &pad)) {
        for (int a = 0; a < 6; ++a) out->axes[a] = pad.axes[a];
        for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
            if (pad.buttons[b] == GLFW_PRESS) out->pad_buttons |= 1u << b;
        }
    }
}
