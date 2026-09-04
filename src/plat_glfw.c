/* plat_glfw.c -- GLFW backend for plat.h. The only file that knows GLFW exists. */
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/* One window per process. A second one would need a handle in the API, and
 * nothing in this codebase wants a second one. */
static GLFWwindow *g_window;
static bool g_key_down[512];

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

bool plat_key_hit(int key) {
    if (!g_window || key < 0 || key >= (int)(sizeof g_key_down / sizeof g_key_down[0])) return false;
    const bool down = glfwGetKey(g_window, key) == GLFW_PRESS;
    const bool hit = down && !g_key_down[key];
    g_key_down[key] = down;
    return hit;
}
