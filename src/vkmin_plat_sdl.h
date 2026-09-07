/* vkmin_plat_sdl.h -- the pure lookup tables shared by the SDL2 and SDL3 backends.
 *
 * vkmin_inputs reports GLFW's key and gamepad-button numbering (see the comment
 * on VKMIN_KEY_COUNT in vkmin.h), so an SDL backend has to translate. The two
 * tables below are the whole translation: no SDL types appear in them, only
 * SDL_SCANCODE_* names, which are spelled and numbered identically in SDL2 and
 * SDL3. Keeping them here rather than in each backend means a key that is wrong
 * is wrong in one place.
 *
 * ---- read this before editing either table ---------------------------------
 *
 * These are DATA, and they are the one part of the platform layer with no
 * failure mode. A wrong entry does not crash, warn, or fail to build. It
 * silently reports the wrong key, or reports nothing at all, and the symptom
 * surfaces as "the game does not respond to F5" long after the edit. Neither
 * the compiler nor the Vulkan validation layer can see any of it.
 *
 * The three invariants an edit must preserve:
 *
 *   - Every value must be a GLFW key code, not an SDL one. The two agree by
 *     accident on letters, digits and space (all ASCII) and on nothing else.
 *   - Every value must be < VKMIN_KEY_COUNT (352). plat_input indexes
 *     out->down[key / 32] with it and does not range-check, because the table
 *     is the check. GLFW's highest code is 348 (MENU), so a correct entry is
 *     always in range and an out-of-range entry is always a typo.
 *   - 0 must keep meaning "not carried". plat_input tests `if (key && ...)`, so
 *     an entry of 0 is a drop and cannot be a real key -- which is fine, since
 *     GLFW has no key 0.
 *
 * The way to check an entry is to read GLFW's glfw3.h GLFW_KEY_* list against
 * SDL's SDL_scancode.h, not to reason from the character on the keycap: SDL
 * names its punctuation scancodes after US-layout positions, and so does GLFW,
 * but they disagree about which ones exist.
 *
 * Include after the SDL headers.
 */
#ifndef VKMIN_PLAT_SDL_H
#define VKMIN_PLAT_SDL_H

/* Both SDL versions cap scancodes below this; the backends additionally clamp
 * to the count SDL_GetKeyboardState reports, so the table can never be indexed
 * past what SDL actually filled. */
#define PLAT_SDL_SCANCODE_CAP 512

/* SDL scancode -> GLFW key code. 0 means "vkmin does not carry this key", which
 * is every scancode not named below: media keys, international keys, F13 and up.
 * Designated initialisers, so the rows are in GLFW-code order for reading and
 * the SDL scancode order does not matter. */
static const short plat_sdl_key[PLAT_SDL_SCANCODE_CAP] = {
    [SDL_SCANCODE_SPACE] = 32,       [SDL_SCANCODE_APOSTROPHE] = 39,
    [SDL_SCANCODE_COMMA] = 44,       [SDL_SCANCODE_MINUS] = 45,
    [SDL_SCANCODE_PERIOD] = 46,      [SDL_SCANCODE_SLASH] = 47,
    [SDL_SCANCODE_0] = 48,           [SDL_SCANCODE_1] = 49,
    [SDL_SCANCODE_2] = 50,           [SDL_SCANCODE_3] = 51,
    [SDL_SCANCODE_4] = 52,           [SDL_SCANCODE_5] = 53,
    [SDL_SCANCODE_6] = 54,           [SDL_SCANCODE_7] = 55,
    [SDL_SCANCODE_8] = 56,           [SDL_SCANCODE_9] = 57,
    [SDL_SCANCODE_SEMICOLON] = 59,   [SDL_SCANCODE_EQUALS] = 61,
    [SDL_SCANCODE_A] = 65,           [SDL_SCANCODE_B] = 66,
    [SDL_SCANCODE_C] = 67,           [SDL_SCANCODE_D] = 68,
    [SDL_SCANCODE_E] = 69,           [SDL_SCANCODE_F] = 70,
    [SDL_SCANCODE_G] = 71,           [SDL_SCANCODE_H] = 72,
    [SDL_SCANCODE_I] = 73,           [SDL_SCANCODE_J] = 74,
    [SDL_SCANCODE_K] = 75,           [SDL_SCANCODE_L] = 76,
    [SDL_SCANCODE_M] = 77,           [SDL_SCANCODE_N] = 78,
    [SDL_SCANCODE_O] = 79,           [SDL_SCANCODE_P] = 80,
    [SDL_SCANCODE_Q] = 81,           [SDL_SCANCODE_R] = 82,
    [SDL_SCANCODE_S] = 83,           [SDL_SCANCODE_T] = 84,
    [SDL_SCANCODE_U] = 85,           [SDL_SCANCODE_V] = 86,
    [SDL_SCANCODE_W] = 87,           [SDL_SCANCODE_X] = 88,
    [SDL_SCANCODE_Y] = 89,           [SDL_SCANCODE_Z] = 90,
    [SDL_SCANCODE_LEFTBRACKET] = 91, [SDL_SCANCODE_BACKSLASH] = 92,
    [SDL_SCANCODE_RIGHTBRACKET] = 93,[SDL_SCANCODE_GRAVE] = 96,
    [SDL_SCANCODE_ESCAPE] = 256,     [SDL_SCANCODE_RETURN] = 257,
    [SDL_SCANCODE_TAB] = 258,        [SDL_SCANCODE_BACKSPACE] = 259,
    [SDL_SCANCODE_INSERT] = 260,     [SDL_SCANCODE_DELETE] = 261,
    [SDL_SCANCODE_RIGHT] = 262,      [SDL_SCANCODE_LEFT] = 263,
    [SDL_SCANCODE_DOWN] = 264,       [SDL_SCANCODE_UP] = 265,
    [SDL_SCANCODE_PAGEUP] = 266,     [SDL_SCANCODE_PAGEDOWN] = 267,
    [SDL_SCANCODE_HOME] = 268,       [SDL_SCANCODE_END] = 269,
    [SDL_SCANCODE_CAPSLOCK] = 280,   [SDL_SCANCODE_SCROLLLOCK] = 281,
    [SDL_SCANCODE_NUMLOCKCLEAR] = 282, [SDL_SCANCODE_PRINTSCREEN] = 283,
    [SDL_SCANCODE_PAUSE] = 284,
    [SDL_SCANCODE_F1] = 290,  [SDL_SCANCODE_F2] = 291,  [SDL_SCANCODE_F3] = 292,
    [SDL_SCANCODE_F4] = 293,  [SDL_SCANCODE_F5] = 294,  [SDL_SCANCODE_F6] = 295,
    [SDL_SCANCODE_F7] = 296,  [SDL_SCANCODE_F8] = 297,  [SDL_SCANCODE_F9] = 298,
    [SDL_SCANCODE_F10] = 299, [SDL_SCANCODE_F11] = 300, [SDL_SCANCODE_F12] = 301,
    [SDL_SCANCODE_KP_0] = 320, [SDL_SCANCODE_KP_1] = 321, [SDL_SCANCODE_KP_2] = 322,
    [SDL_SCANCODE_KP_3] = 323, [SDL_SCANCODE_KP_4] = 324, [SDL_SCANCODE_KP_5] = 325,
    [SDL_SCANCODE_KP_6] = 326, [SDL_SCANCODE_KP_7] = 327, [SDL_SCANCODE_KP_8] = 328,
    [SDL_SCANCODE_KP_9] = 329,
    [SDL_SCANCODE_KP_PERIOD] = 330,   [SDL_SCANCODE_KP_DIVIDE] = 331,
    [SDL_SCANCODE_KP_MULTIPLY] = 332, [SDL_SCANCODE_KP_MINUS] = 333,
    [SDL_SCANCODE_KP_PLUS] = 334,     [SDL_SCANCODE_KP_ENTER] = 335,
    [SDL_SCANCODE_KP_EQUALS] = 336,
    [SDL_SCANCODE_LSHIFT] = 340, [SDL_SCANCODE_LCTRL] = 341,
    [SDL_SCANCODE_LALT] = 342,   [SDL_SCANCODE_LGUI] = 343,
    [SDL_SCANCODE_RSHIFT] = 344, [SDL_SCANCODE_RCTRL] = 345,
    [SDL_SCANCODE_RALT] = 346,   [SDL_SCANCODE_RGUI] = 347,
    [SDL_SCANCODE_APPLICATION] = 348,
};

/* SDL gamepad button index -> GLFW gamepad button index. Unlike the key table
 * this one is positional: row N is SDL button N, so a row inserted in the wrong
 * place shifts every button after it. The two APIs agree on A/B/X/Y and diverge
 * from BACK onwards -- SDL orders the stick clicks before the shoulders, GLFW
 * after, and they disagree about the D-pad's last three.
 *
 * SDL2 and SDL3 number their own buttons the same way, so this table serves
 * both. Each backend then static-asserts four positions of the SDL enum it
 * actually compiled against, which is the only automated check any of this has:
 * it catches a renumbered SDL, not a mistyped row. */
#define PLAT_SDL_PAD_BUTTONS 15
static const unsigned char plat_sdl_pad_button[PLAT_SDL_PAD_BUTTONS] = {
    0,  /* SDL A/SOUTH        -> GLFW A */
    1,  /* SDL B/EAST         -> GLFW B */
    2,  /* SDL X/WEST         -> GLFW X */
    3,  /* SDL Y/NORTH        -> GLFW Y */
    6,  /* SDL BACK           -> GLFW BACK */
    8,  /* SDL GUIDE          -> GLFW GUIDE */
    7,  /* SDL START          -> GLFW START */
    9,  /* SDL LEFT STICK     -> GLFW LEFT THUMB */
    10, /* SDL RIGHT STICK    -> GLFW RIGHT THUMB */
    4,  /* SDL LEFT SHOULDER  -> GLFW LEFT BUMPER */
    5,  /* SDL RIGHT SHOULDER -> GLFW RIGHT BUMPER */
    11, /* SDL DPAD UP        -> GLFW DPAD UP */
    13, /* SDL DPAD DOWN      -> GLFW DPAD DOWN */
    14, /* SDL DPAD LEFT      -> GLFW DPAD LEFT */
    12, /* SDL DPAD RIGHT     -> GLFW DPAD RIGHT */
};

/* SDL reports sticks over the full Sint16 range and triggers over 0..32767;
 * GLFW reports both in -1..1, with a released trigger reading -1. Pure.
 *
 * The clamps are not defensive padding. Sint16 is asymmetric, so a stick pushed
 * fully negative reads -32768 and divides to -1.000031 -- just outside the range
 * vkmin_inputs documents, and enough to make a caller's `axis * axis` or an
 * acosf() misbehave. Doing the divide and then clamping (rather than branching
 * on the raw value first) is the execute-then-inhibit shape from CLAUDE.md
 * section 3: one path, no skipped work. */
static inline float plat_sdl_stick(int raw) {
    const float v = (float)raw / 32767.0f;
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}
static inline float plat_sdl_trigger(int raw) {
    const float v = (float)raw / 32767.0f * 2.0f - 1.0f;
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

#endif /* VKMIN_PLAT_SDL_H */
