/* vkmin_plat_glfw.c -- GLFW backend for plat.h. The only file that knows GLFW exists. */
#include "vkmin_plat.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

struct plat_window { GLFWwindow *handle; struct plat_window *next; double wheel; };

#include "vkmin_plat_common.h"

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
    for (int key = 32; key < GLFW_KEY_LAST && key < (int)VKMIN_KEY_COUNT; ++key) {
        if (glfwGetKey(window->handle, key) == GLFW_PRESS) out->down[key / 32] |= 1u << (key % 32);
    }
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window->handle, &mx, &my);
    out->mouse_x = (float)mx;
    out->mouse_y = (float)my;
    out->wheel = (float)window->wheel;
    window->wheel = 0.0;
    for (int b = 0; b < 3; ++b) {
        if (glfwGetMouseButton(window->handle, b) == GLFW_PRESS) out->buttons |= 1u << b;
    }
    GLFWgamepadstate pad;
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, &pad)) {
        for (int a = 0; a < 6; ++a) out->axes[a] = pad.axes[a];
        for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
            if (pad.buttons[b] == GLFW_PRESS) out->pad_buttons |= 1u << b;
        }
    }
}
