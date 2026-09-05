/* plat_glfw.c -- GLFW backend for plat.h. The only file that knows GLFW exists. */
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/* One window per process. A second one would need a handle in the API, and
 * nothing in this codebase wants a second one. */
static GLFWwindow *g_window;
static double g_wheel; /* accumulated between polls */

static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w;
    (void)dx;
    g_wheel += dy;
}

bool plat_window_open(int w, int h, const char *title) {
    if (g_window) return false;
    if (!glfwInit()) return false;
    if (!glfwVulkanSupported()) {
        fprintf(stderr, "plat: GLFW reports no Vulkan loader\n");
        glfwTerminate();
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    g_window = glfwCreateWindow(w, h, title ? title : "vkmin", NULL, NULL);
    if (!g_window) {
        glfwTerminate();
        return false;
    }
    glfwSetScrollCallback(g_window, on_scroll);
    return true;
}

void plat_poll(void) {
    if (g_window) glfwPollEvents();
}

bool plat_should_close(void) {
    return g_window ? glfwWindowShouldClose(g_window) != 0 : true;
}

void plat_close(void) {
    if (!g_window) return;
    glfwDestroyWindow(g_window);
    g_window = NULL;
    glfwTerminate();
}

const char **plat_required_instance_extensions(uint32_t *count) {
    *count = 0;
    if (!glfwInit()) return NULL;
    return glfwGetRequiredInstanceExtensions(count);
}

VkSurfaceKHR plat_create_surface(VkInstance instance) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!g_window) return VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, g_window, NULL, &surface) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return surface;
}

void plat_framebuffer_size(int *w, int *h) {
    *w = 0;
    *h = 0;
    if (g_window) glfwGetFramebufferSize(g_window, w, h);
}

void plat_input(vkmin_inputs *out) {
    *out = (vkmin_inputs){0};
    if (!g_window) return;
    for (int key = 32; key < GLFW_KEY_LAST && key < (int)VKMIN_KEY_COUNT; ++key) {
        if (glfwGetKey(g_window, key) == GLFW_PRESS) out->down[key / 32] |= 1u << (key % 32);
    }
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(g_window, &mx, &my);
    out->mouse_x = (float)mx;
    out->mouse_y = (float)my;
    out->wheel = (float)g_wheel;
    g_wheel = 0.0;
    for (int b = 0; b < 3; ++b) {
        if (glfwGetMouseButton(g_window, b) == GLFW_PRESS) out->buttons |= 1u << b;
    }
    GLFWgamepadstate pad;
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, &pad)) {
        for (int a = 0; a < 6; ++a) out->axes[a] = pad.axes[a];
        for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
            if (pad.buttons[b] == GLFW_PRESS) out->pad_buttons |= 1u << b;
        }
    }
}
