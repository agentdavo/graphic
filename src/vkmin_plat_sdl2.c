/* vkmin_plat_sdl2.c -- SDL2 backend for vkmin_plat.h. The only file that knows SDL2 exists.
 *
 * A parallel implementation of vkmin_plat_glfw.c, not a replacement: exactly one
 * plat backend links into a binary, chosen by -DVKMIN_PLATFORM=sdl2 at CMake
 * time. Behaviour is matched to the GLFW backend deliberately, including the
 * main-thread abort and the per-window wheel accumulator, so switching backends
 * changes no vkmin code.
 *
 * This file and vkmin_plat_sdl3.c are near-identical on purpose; see the header
 * of that file for why they are not one file with #ifs, and for the list of
 * API differences that would have to be interleaved if they were.
 *
 * Where SDL is global and GLFW is per-window -- the event queue, the keyboard,
 * the mouse -- the open windows are kept on a list and events are routed by SDL
 * window id, and keyboard/button state is gated on input focus so that an
 * unfocused window reports everything released, as GLFW does.
 */
#define SDL_MAIN_HANDLED /* vkmin owns main(); do not let SDL rename it */
#include "vkmin_plat.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "vkmin_plat_sdl.h"

_Static_assert(SDL_CONTROLLER_BUTTON_A == 0 && SDL_CONTROLLER_BUTTON_BACK == 4 &&
               SDL_CONTROLLER_BUTTON_LEFTSHOULDER == 9 &&
               SDL_CONTROLLER_BUTTON_DPAD_RIGHT == PLAT_SDL_PAD_BUTTONS - 1,
               "SDL2 renumbered its gamepad buttons; plat_sdl_pad_button is now wrong");

struct plat_window {
    SDL_Window *handle;
    struct plat_window *next;
    Uint32 id;
    float wheel;
    bool should_close;
};

#include "vkmin_plat_common.h"

static SDL_GameController *pad;

static plat_window *window_by_id(Uint32 id) {
    for (plat_window *w = window_list; w; w = w->next) {
        if (w->id == id) return w;
    }
    return NULL;
}

/* Only reached after a pad is unplugged: SDL emits CONTROLLERDEVICEADDED for
 * pads already attached at init, so the first plat_poll picks one up for free. */
static void open_first_pad(void) {
    const int joysticks = SDL_NumJoysticks();
    for (int i = 0; !pad && i < joysticks; ++i) {
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
    }
}

static void shutdown_sdl(void) {
    if (pad) { SDL_GameControllerClose(pad); pad = NULL; }
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

plat_window *plat_window_open(int w, int h, const char *title) {
    check_thread();
    const bool first = !window_count;
    if (first) {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return NULL;
        if (SDL_Vulkan_LoadLibrary(NULL) != 0) { SDL_Quit(); return NULL; }
    }
    plat_window *window = calloc(1, sizeof *window);
    if (!window) { if (first) shutdown_sdl(); return NULL; }
    window->handle = SDL_CreateWindow(title ? title : "vkmin",
                                      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI);
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
        case SDL_QUIT:
            for (plat_window *w = window_list; w; w = w->next) w->should_close = true;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                plat_window *w = window_by_id(event.window.windowID);
                if (w) w->should_close = true;
            }
            break;
        case SDL_MOUSEWHEEL: {
            plat_window *w = window_by_id(event.wheel.windowID);
            if (w) w->wheel += (float)event.wheel.y;
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            if (!pad) pad = SDL_GameControllerOpen(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == event.cdevice.which) {
                SDL_GameControllerClose(pad);
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
    /* SDL2 wants a caller-owned array and, unlike SDL3, a window to ask about.
     * The name strings are owned by SDL and outlive the call, so caching the
     * pointers here matches the static array GLFW hands back. */
    static const char *names[16];
    *count = 0;
    if (!window_count) return NULL;
    unsigned n = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window_list->handle, &n, NULL)) return NULL;
    if (n > sizeof names / sizeof *names) return NULL;
    if (!SDL_Vulkan_GetInstanceExtensions(window_list->handle, &n, names)) return NULL;
    *count = n;
    return names;
}

VkSurfaceKHR plat_create_surface(const plat_window *window, VkInstance instance) {
    check_thread();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!window) return VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window->handle, instance, &surface)) return VK_NULL_HANDLE;
    return surface;
}

void plat_framebuffer_size(plat_window *window, int *w, int *h) {
    check_thread();
    *w = 0;
    *h = 0;
    if (window) SDL_Vulkan_GetDrawableSize(window->handle, w, h);
}

void plat_input(plat_window *window, vkmin_inputs *out) {
    check_thread();
    *out = (vkmin_inputs){0};
    if (!window) return;

    /* SDL's keyboard and mouse are process-wide; GLFW's are per-window and read
     * released when the window is not focused. Gate on focus to match. */
    const bool focused = SDL_GetKeyboardFocus() == window->handle;
    int key_count = 0;
    const Uint8 *keys = SDL_GetKeyboardState(&key_count);
    if (focused && keys) {
        /* Two independent bounds, both needed: the clamp keeps the read inside
         * plat_sdl_key, and the table's own guarantee that no entry reaches
         * VKMIN_KEY_COUNT keeps the shift inside out->down. See vkmin_plat_sdl.h. */
        if (key_count > PLAT_SDL_SCANCODE_CAP) key_count = PLAT_SDL_SCANCODE_CAP;
        for (int sc = 0; sc < key_count; ++sc) {
            const int key = plat_sdl_key[sc];
            if (key && keys[sc]) out->down[key / 32] |= 1u << (key % 32);
        }
    }

    /* Position comes from the global cursor so it stays meaningful while a drag
     * leaves the window, as GLFW's does; buttons stay gated on focus. */
    int gx = 0, gy = 0, wx = 0, wy = 0;
    const Uint32 mask = SDL_GetGlobalMouseState(&gx, &gy);
    SDL_GetWindowPosition(window->handle, &wx, &wy);
    out->mouse_x = (float)(gx - wx);
    out->mouse_y = (float)(gy - wy);
    out->wheel = window->wheel;
    window->wheel = 0.0f;
    if (focused) {
        if (mask & SDL_BUTTON_LMASK) out->buttons |= VKMIN_MOUSE_LEFT;
        if (mask & SDL_BUTTON_RMASK) out->buttons |= VKMIN_MOUSE_RIGHT;
        if (mask & SDL_BUTTON_MMASK) out->buttons |= VKMIN_MOUSE_MIDDLE;
    }

    if (pad) {
        out->axes[0] = plat_sdl_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
        out->axes[1] = plat_sdl_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
        out->axes[2] = plat_sdl_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
        out->axes[3] = plat_sdl_stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
        out->axes[4] = plat_sdl_trigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        out->axes[5] = plat_sdl_trigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        for (int b = 0; b < PLAT_SDL_PAD_BUTTONS; ++b) {
            if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)b)) {
                out->pad_buttons |= 1u << plat_sdl_pad_button[b];
            }
        }
    }
}
