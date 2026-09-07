/* vkmin_plat_win32.c -- raw Win32 backend for vkmin_plat.h, with no library
 * between vkmin and the OS. The only file that knows Win32 windowing exists.
 *
 * A parallel implementation of vkmin_plat_glfw.c; exactly one plat backend links
 * into a binary, chosen by -DVKMIN_PLATFORM=win32 at CMake time. Behaviour is
 * matched to the GLFW backend deliberately -- the main-thread abort, the
 * per-window wheel accumulator, the GLFW key and gamepad numbering vkmin_inputs
 * carries -- so switching backends changes no vkmin code.
 *
 * Nothing here is linked that a plain mingw/MSVC C program does not already
 * link: the window is user32, the surface comes from the Vulkan loader the rest
 * of vkmin already needs, and XInput is loaded by name at runtime, so a machine
 * without it simply reports no gamepad.
 *
 * Two deliberate divergences from GLFW, both consequences of reading key state
 * from GetKeyboardState rather than tracking WM_KEYDOWN scancodes:
 *   - keypad Enter reports VKMIN_KEY_ENTER, not GLFW's KP_ENTER, and keypad '='
 *     is not reported at all; Windows gives both the same virtual-key code as
 *     their main-keyboard twins, and only the scancode separates them.
 *   - an unfocused window reports every key released, which is what GLFW does
 *     too, but here it falls out of the focus gate rather than out of WM_KILLFOCUS.
 * Neither key is named in vkmin.h's VKMIN_KEY_* set.
 */
#ifndef _WIN32
#error "vkmin_plat_win32.c is the Windows backend; use -DVKMIN_PLATFORM=glfw, sdl2 or sdl3 elsewhere"
#endif

#define WIN32_LEAN_AND_MEAN
/* Every Win32 call here is the explicit -W form; UNICODE makes the resource
 * macros (IDC_ARROW and friends) agree with them instead of expanding to -A. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <xinput.h>

#define VK_USE_PLATFORM_WIN32_KHR /* before vkmin_plat.h: it includes vulkan.h */
#include "vkmin_plat.h"


/* Win32 virtual key -> GLFW key code, which is what vkmin_inputs carries (see
 * the comment on VKMIN_KEY_COUNT in vkmin.h). 0 means "vkmin does not carry it".
 * Letters, digits and space are identity: their VK codes are already ASCII.
 *
 * DATA, with the same silent failure mode as the SDL tables -- a wrong entry
 * builds clean and reports the wrong key forever. The invariants are the same:
 * every value is a GLFW code, every value is < VKMIN_KEY_COUNT (352), and 0 is
 * reserved for "dropped" because plat_input tests `if (key && ...)`. The long
 * version of the warning is in vkmin_plat_sdl.h.
 *
 * The VK_OEM_* rows are the ones to distrust: Windows names them by position on
 * a US layout and their meaning changes with the keyboard layout, so VK_OEM_1
 * is semicolon here and something else on a German keyboard. GLFW has the same
 * problem and resolves it the same way, which is why the two agree in practice. */
static const short plat_win32_key[256] = {
    [VK_BACK] = 259,     [VK_TAB] = 258,      [VK_RETURN] = 257,
    [VK_PAUSE] = 284,    [VK_CAPITAL] = 280,  [VK_ESCAPE] = 256,
    [VK_SPACE] = 32,     [VK_PRIOR] = 266,    [VK_NEXT] = 267,
    [VK_END] = 269,      [VK_HOME] = 268,     [VK_LEFT] = 263,
    [VK_UP] = 265,       [VK_RIGHT] = 262,    [VK_DOWN] = 264,
    [VK_SNAPSHOT] = 283, [VK_INSERT] = 260,   [VK_DELETE] = 261,
    ['0'] = 48, ['1'] = 49, ['2'] = 50, ['3'] = 51, ['4'] = 52,
    ['5'] = 53, ['6'] = 54, ['7'] = 55, ['8'] = 56, ['9'] = 57,
    ['A'] = 65, ['B'] = 66, ['C'] = 67, ['D'] = 68, ['E'] = 69, ['F'] = 70,
    ['G'] = 71, ['H'] = 72, ['I'] = 73, ['J'] = 74, ['K'] = 75, ['L'] = 76,
    ['M'] = 77, ['N'] = 78, ['O'] = 79, ['P'] = 80, ['Q'] = 81, ['R'] = 82,
    ['S'] = 83, ['T'] = 84, ['U'] = 85, ['V'] = 86, ['W'] = 87, ['X'] = 88,
    ['Y'] = 89, ['Z'] = 90,
    [VK_LWIN] = 343, [VK_RWIN] = 347, [VK_APPS] = 348,
    [VK_NUMPAD0] = 320, [VK_NUMPAD1] = 321, [VK_NUMPAD2] = 322,
    [VK_NUMPAD3] = 323, [VK_NUMPAD4] = 324, [VK_NUMPAD5] = 325,
    [VK_NUMPAD6] = 326, [VK_NUMPAD7] = 327, [VK_NUMPAD8] = 328,
    [VK_NUMPAD9] = 329,
    [VK_MULTIPLY] = 332, [VK_ADD] = 334, [VK_SUBTRACT] = 333,
    [VK_DECIMAL] = 330,  [VK_DIVIDE] = 331,
    [VK_F1] = 290,  [VK_F2] = 291,  [VK_F3] = 292,  [VK_F4] = 293,
    [VK_F5] = 294,  [VK_F6] = 295,  [VK_F7] = 296,  [VK_F8] = 297,
    [VK_F9] = 298,  [VK_F10] = 299, [VK_F11] = 300, [VK_F12] = 301,
    [VK_NUMLOCK] = 282, [VK_SCROLL] = 281,
    [VK_LSHIFT] = 340,   [VK_RSHIFT] = 344,
    [VK_LCONTROL] = 341, [VK_RCONTROL] = 345,
    [VK_LMENU] = 342,    [VK_RMENU] = 346,
    [VK_OEM_1] = 59,     [VK_OEM_PLUS] = 61,  [VK_OEM_COMMA] = 44,
    [VK_OEM_MINUS] = 45, [VK_OEM_PERIOD] = 46, [VK_OEM_2] = 47,
    [VK_OEM_3] = 96,     [VK_OEM_4] = 91,     [VK_OEM_5] = 92,
    [VK_OEM_6] = 93,     [VK_OEM_7] = 39,
};

/* XInput button flag -> GLFW gamepad button index. Flag-to-index rather than the
 * SDL tables' index-to-index, because XInput packs its buttons as a bitfield in
 * an order that is not GLFW's; pairing each flag with its destination means the
 * row order here carries no meaning and a reordered row is harmless.
 *
 * GLFW's index 8 is GUIDE, which public XInput does not report, so it is never
 * set here -- a genuine capability gap, not a missing row. */
static const struct { WORD flag; unsigned char glfw; } plat_win32_pad[] = {
    { XINPUT_GAMEPAD_A, 0 },              { XINPUT_GAMEPAD_B, 1 },
    { XINPUT_GAMEPAD_X, 2 },              { XINPUT_GAMEPAD_Y, 3 },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, 4 },  { XINPUT_GAMEPAD_RIGHT_SHOULDER, 5 },
    { XINPUT_GAMEPAD_BACK, 6 },           { XINPUT_GAMEPAD_START, 7 },
    { XINPUT_GAMEPAD_LEFT_THUMB, 9 },     { XINPUT_GAMEPAD_RIGHT_THUMB, 10 },
    { XINPUT_GAMEPAD_DPAD_UP, 11 },       { XINPUT_GAMEPAD_DPAD_RIGHT, 12 },
    { XINPUT_GAMEPAD_DPAD_DOWN, 13 },     { XINPUT_GAMEPAD_DPAD_LEFT, 14 },
};

struct plat_window {
    HWND handle;
    struct plat_window *next;
    float wheel;
    bool should_close;
};

#include "vkmin_plat_common.h"

static const WCHAR class_name[] = L"vkmin_window";

static DWORD (WINAPI *xinput_get_state)(DWORD, XINPUT_STATE *);
static int pad_slot = -1;    /* XInput user index, or -1 when none is connected */
static unsigned pad_rescan;  /* frames to wait before looking again: polling an
                              * empty XInput slot costs about a millisecond */

/* GetProcAddress hands back a FARPROC. Casting that straight to the real
 * signature trips -Wcast-function-type and routing it through void * trips
 * -Wpedantic; the union is the conversion ISO C actually permits here. */
typedef union {
    FARPROC proc;
    BOOL (WINAPI *set_dpi_context)(DPI_AWARENESS_CONTEXT);
    DWORD (WINAPI *xinput_get_state)(DWORD, XINPUT_STATE *);
} plat_proc;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    plat_window *window = (plat_window *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        break; /* DefWindowProcW still has to do the non-client setup */
    }
    case WM_CLOSE:
        /* Report it and nothing else. plat_close owns the window's lifetime, and
         * destroying it here would leave vkmin holding a surface over dead HWND. */
        if (window) window->should_close = true;
        return 0;
    case WM_MOUSEWHEEL:
        if (window) window->wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        return 0;
    case WM_ERASEBKGND:
        return 1; /* the class has no background brush; the swapchain paints it all */
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Windows scales the client rect of a DPI-unaware process, which would leave
 * plat_framebuffer_size disagreeing with the surface Vulkan actually reports.
 * Loaded by name because the call only exists on Windows 10 1703 and later;
 * GLFW does the same, so both backends see the same client size. */
static void become_dpi_aware(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    plat_proc cast = { .proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext") };
    if (cast.set_dpi_context) cast.set_dpi_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

static void load_xinput(void) {
    static const WCHAR *const names[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i) {
        HMODULE lib = LoadLibraryW(names[i]);
        if (!lib) continue;
        plat_proc cast = { .proc = GetProcAddress(lib, "XInputGetState") };
        if (cast.xinput_get_state) { xinput_get_state = cast.xinput_get_state; return; }
    }
}

plat_window *plat_window_open(int w, int h, const char *title) {
    check_thread();
    const bool first = !window_count;
    const HINSTANCE instance = GetModuleHandleW(NULL);
    if (first) {
        become_dpi_aware();
        load_xinput();
        const WNDCLASSEXW wc = {
            .cbSize = sizeof wc,
            .style = CS_OWNDC,
            .lpfnWndProc = wnd_proc,
            .hInstance = instance,
            .hCursor = LoadCursorW(NULL, IDC_ARROW),
            .lpszClassName = class_name,
        };
        if (!RegisterClassExW(&wc)) return NULL;
    }
    plat_window *window = calloc(1, sizeof *window);
    if (!window) { if (first) UnregisterClassW(class_name, instance); return NULL; }

    WCHAR wide_title[128];
    if (!title || !MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title,
                                       sizeof wide_title / sizeof *wide_title)) {
        wide_title[0] = L'v'; wide_title[1] = L'k'; wide_title[2] = L'm';
        wide_title[3] = L'i'; wide_title[4] = L'n'; wide_title[5] = L'\0';
    }

    /* w and h are the client area, as GLFW's are; the frame goes outside it. */
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT frame = { 0, 0, w, h };
    AdjustWindowRectEx(&frame, style, FALSE, 0);
    window->handle = CreateWindowExW(0, class_name, wide_title, style,
                                     CW_USEDEFAULT, CW_USEDEFAULT,
                                     frame.right - frame.left, frame.bottom - frame.top,
                                     NULL, NULL, instance, window);
    if (!window->handle) {
        free(window);
        if (first) UnregisterClassW(class_name, instance);
        return NULL;
    }
    if (first) claim_thread();
    window_link(window);
    ShowWindow(window->handle, SW_SHOWNORMAL);
    return window;
}

void plat_poll(void) {
    check_thread();
    if (!window_count) return;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            for (plat_window *w = window_list; w; w = w->next) w->should_close = true;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
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
    DestroyWindow(window->handle);
    free(window);
    if (!window_count) UnregisterClassW(class_name, GetModuleHandleW(NULL));
}

const char **plat_required_instance_extensions(uint32_t *count) {
    check_thread();
    /* Fixed for this backend: there is one surface platform and vkmin needs both
     * halves of it. The loader owns the strings, as it does under GLFW and SDL. */
    static const char *names[] = { VK_KHR_SURFACE_EXTENSION_NAME,
                                   VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    *count = 0;
    if (!window_count) return NULL;
    *count = (uint32_t)(sizeof names / sizeof *names);
    return names;
}

VkSurfaceKHR plat_create_surface(const plat_window *window, VkInstance instance) {
    check_thread();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!window) return VK_NULL_HANDLE;
    const VkWin32SurfaceCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandleW(NULL),
        .hwnd = window->handle,
    };
    if (vkCreateWin32SurfaceKHR(instance, &info, NULL, &surface) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return surface;
}

void plat_framebuffer_size(plat_window *window, int *w, int *h) {
    check_thread();
    *w = 0;
    *h = 0;
    RECT client;
    if (window && GetClientRect(window->handle, &client)) {
        *w = (int)(client.right - client.left);
        *h = (int)(client.bottom - client.top);
    }
}

/* True when a pad was read into state. Sticks to the slot it last found so the
 * common case is one call; a rescan for a newly plugged pad is throttled. */
static bool read_pad(XINPUT_STATE *state) {
    if (!xinput_get_state) return false;
    if (pad_slot >= 0) {
        if (xinput_get_state((DWORD)pad_slot, state) == ERROR_SUCCESS) return true;
        pad_slot = -1;
    }
    if (pad_rescan) { --pad_rescan; return false; }
    pad_rescan = 120; /* about two seconds of frames */
    for (DWORD i = 0; i < 4; ++i) {
        if (xinput_get_state(i, state) == ERROR_SUCCESS) { pad_slot = (int)i; return true; }
    }
    return false;
}

void plat_input(plat_window *window, vkmin_inputs *out) {
    check_thread();
    *out = (vkmin_inputs){0};
    if (!window) return;

    /* Win32 keyboard state belongs to the thread's message queue, not to a
     * window, so gate on focus: GLFW reports an unfocused window's keys as
     * released and vkmin's callers rely on that. Mouse buttons ride along in
     * the same array, already resolved for a swapped-button mouse. */
    const bool focused = GetForegroundWindow() == window->handle;
    BYTE keys[256];
    if (focused && GetKeyboardState(keys)) {
        for (int vk = 0; vk < 256; ++vk) {
            const int key = plat_win32_key[vk];
            if (key && (keys[vk] & 0x80)) out->down[key / 32] |= 1u << (key % 32);
        }
        if (keys[VK_LBUTTON] & 0x80) out->buttons |= VKMIN_MOUSE_LEFT;
        if (keys[VK_RBUTTON] & 0x80) out->buttons |= VKMIN_MOUSE_RIGHT;
        if (keys[VK_MBUTTON] & 0x80) out->buttons |= VKMIN_MOUSE_MIDDLE;
    }

    /* Screen cursor mapped into the client area, so it stays meaningful while a
     * drag leaves the window, as GLFW's does. */
    POINT cursor = { 0, 0 };
    if (GetCursorPos(&cursor) && ScreenToClient(window->handle, &cursor)) {
        out->mouse_x = (float)cursor.x;
        out->mouse_y = (float)cursor.y;
    }
    out->wheel = window->wheel;
    window->wheel = 0.0f;

    XINPUT_STATE state;
    if (read_pad(&state)) {
        /* GLFW's own XInput normalisation, copied so the two backends agree bit
         * for bit rather than merely closely. Three things are load-bearing:
         * the +0.5 and 32767.5 make Sint16's asymmetric range map onto exactly
         * -1..1 with no clamp needed; the Y negation is because XInput calls up
         * positive and vkmin_inputs calls down positive; and the triggers are
         * bytes (0..255), so 127.5 rescales them to -1..1 with released at -1. */
        out->axes[0] = ((float)state.Gamepad.sThumbLX + 0.5f) / 32767.5f;
        out->axes[1] = -((float)state.Gamepad.sThumbLY + 0.5f) / 32767.5f;
        out->axes[2] = ((float)state.Gamepad.sThumbRX + 0.5f) / 32767.5f;
        out->axes[3] = -((float)state.Gamepad.sThumbRY + 0.5f) / 32767.5f;
        out->axes[4] = (float)state.Gamepad.bLeftTrigger / 127.5f - 1.0f;
        out->axes[5] = (float)state.Gamepad.bRightTrigger / 127.5f - 1.0f;
        for (size_t i = 0; i < sizeof plat_win32_pad / sizeof *plat_win32_pad; ++i) {
            if (state.Gamepad.wButtons & plat_win32_pad[i].flag) {
                out->pad_buttons |= 1u << plat_win32_pad[i].glfw;
            }
        }
    }
}
