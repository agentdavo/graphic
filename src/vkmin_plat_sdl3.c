/* vkmin_plat_sdl3.c -- SDL3 backend for vkmin_plat.h. The only file that knows SDL3 exists.
 *
 * A parallel implementation of vkmin_plat_glfw.c and vkmin_plat_sdl2.c; exactly
 * one plat backend links into a binary, chosen by PLAT= in the Makefile. It is a
 * separate file rather than an #if inside the SDL2 backend on purpose: SDL3
 * renamed or reshaped nearly every call used here, and interleaving the two
 * would compromise both.
 *
 * Where SDL is global and GLFW is per-window -- the event queue, the keyboard,
 * the mouse -- the open windows are kept on a list and events are routed by SDL
 * window id, and keyboard/button state is gated on input focus so that an
 * unfocused window reports everything released, as GLFW does.
 */
#define SDL_MAIN_HANDLED /* vkmin owns main(); do not let SDL rename it */
#include "vkmin_plat.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> /* SDL3 does not pull this in from SDL.h; SDL_SetMainReady lives here */
#include <SDL3/SDL_vulkan.h>

#include "vkmin_plat_sdl.h"

_Static_assert(SDL_GAMEPAD_BUTTON_SOUTH == 0 && SDL_GAMEPAD_BUTTON_BACK == 4 &&
               SDL_GAMEPAD_BUTTON_LEFT_SHOULDER == 9 &&
               SDL_GAMEPAD_BUTTON_DPAD_RIGHT == PLAT_SDL_PAD_BUTTONS - 1,
               "SDL3 renumbered its gamepad buttons; plat_sdl_pad_button is now wrong");

struct plat_window {
    SDL_Window *handle;
    struct plat_window *next;
    SDL_WindowID id;
    float wheel;
    bool should_close;
};

#include "vkmin_plat_common.h"

static SDL_Gamepad *pad;

static plat_window *window_by_id(SDL_WindowID id) {
    for (plat_window *w = window_list; w; w = w->next) {
        if (w->id == id) return w;
    }
    return NULL;
}

/* Only reached after a pad is unplugged: SDL emits SDL_EVENT_GAMEPAD_ADDED for
 * pads already attached at init, so the first plat_poll picks one up for free. */
static void open_first_pad(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; !pad && i < count; ++i) pad = SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
}

static void shutdown_sdl(void) {
    if (pad) { SDL_CloseGamepad(pad); pad = NULL; }
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

plat_window *plat_window_open(int w, int h, const char *title) {
    check_thread();
    const bool first = !window_count;
    if (first) {
        SDL_SetMainReady();
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return NULL;
        if (!SDL_Vulkan_LoadLibrary(NULL)) { SDL_Quit(); return NULL; }
    }
    plat_window *window = calloc(1, sizeof *window);
    if (!window) { if (first) shutdown_sdl(); return NULL; }
    window->handle = SDL_CreateWindow(title ? title : "vkmin", w, h,
                                      SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window->handle) {
        free(window);
        if (first) shutdown_sdl();
        return NULL;
    }
    if (first) claim_thread();
    window->id = SDL_GetWindowID(window->handle);
    window_link(window);
    return window;
}

void plat_poll(void) {
    check_thread();
    if (!window_count) return;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            for (plat_window *w = window_list; w; w = w->next) w->should_close = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            plat_window *w = window_by_id(event.window.windowID);
            if (w) w->should_close = true;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            plat_window *w = window_by_id(event.wheel.windowID);
            if (w) w->wheel += event.wheel.y;
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!pad) pad = SDL_OpenGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (pad && SDL_GetGamepadID(pad) == event.gdevice.which) {
                SDL_CloseGamepad(pad);
                pad = NULL;
                open_first_pad();
            }
            break;
        default:
            break;
        }
    }
}

bool plat_should_close(const plat_window *window) {
    check_thread();
    return !window || window->should_close;
}

void plat_close(plat_window *window) {
    check_thread();
    if (!window) return;
    window_unlink(window);
    SDL_DestroyWindow(window->handle);
    free(window);
    if (!window_count) shutdown_sdl();
}

const char **plat_required_instance_extensions(uint32_t *count) {
    check_thread();
    /* SDL3 returns its own const array and needs no window. It is copied rather
     * than cast so the const on SDL's array is not laundered away; the name
     * strings themselves are owned by SDL and outlive the call. */
    static const char *names[16];
    *count = 0;
    if (!window_count) return NULL;
    Uint32 n = 0;
    const char *const *sdl_names = SDL_Vulkan_GetInstanceExtensions(&n);
    if (!sdl_names || n > sizeof names / sizeof *names) return NULL;
    for (Uint32 i = 0; i < n; ++i) names[i] = sdl_names[i];
    *count = n;
    return names;
}

VkSurfaceKHR plat_create_surface(const plat_window *window, VkInstance instance) {
    check_thread();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!window) return VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window->handle, instance, NULL, &surface)) return VK_NULL_HANDLE;
    return surface;
}

void plat_framebuffer_size(plat_window *window, int *w, int *h) {
    check_thread();
    *w = 0;
    *h = 0;
    if (window) SDL_GetWindowSizeInPixels(window->handle, w, h);
}

void plat_input(plat_window *window, vkmin_inputs *out) {
    check_thread();
    *out = (vkmin_inputs){0};
    if (!window) return;

    /* SDL's keyboard and mouse are process-wide; GLFW's are per-window and read
     * released when the window is not focused. Gate on focus to match. */
    const bool focused = SDL_GetKeyboardFocus() == window->handle;
    int key_count = 0;
    const bool *keys = SDL_GetKeyboardState(&key_count);
    if (focused && keys) {
        if (key_count > PLAT_SDL_SCANCODE_CAP) key_count = PLAT_SDL_SCANCODE_CAP;
        for (int sc = 0; sc < key_count; ++sc) {
            const int key = plat_sdl_key[sc];
            if (key && keys[sc]) out->down[key / 32] |= 1u << (key % 32);
        }
    }

    /* Position comes from the global cursor so it stays meaningful while a drag
     * leaves the window, as GLFW's does; buttons stay gated on focus. */
    float gx = 0.0f, gy = 0.0f;
    int wx = 0, wy = 0;
    const SDL_MouseButtonFlags mask = SDL_GetGlobalMouseState(&gx, &gy);
    SDL_GetWindowPosition(window->handle, &wx, &wy);
    out->mouse_x = gx - (float)wx;
    out->mouse_y = gy - (float)wy;
    out->wheel = window->wheel;
    window->wheel = 0.0f;
    if (focused) {
        if (mask & SDL_BUTTON_LMASK) out->buttons |= VKMIN_MOUSE_LEFT;
        if (mask & SDL_BUTTON_RMASK) out->buttons |= VKMIN_MOUSE_RIGHT;
        if (mask & SDL_BUTTON_MMASK) out->buttons |= VKMIN_MOUSE_MIDDLE;
    }

    if (pad) {
        out->axes[0] = plat_sdl_stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
        out->axes[1] = plat_sdl_stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
        out->axes[2] = plat_sdl_stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
        out->axes[3] = plat_sdl_stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
        out->axes[4] = plat_sdl_trigger(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        out->axes[5] = plat_sdl_trigger(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        for (int b = 0; b < PLAT_SDL_PAD_BUTTONS; ++b) {
            if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)b)) {
                out->pad_buttons |= 1u << plat_sdl_pad_button[b];
            }
        }
    }
}
