/* Shared example presentation and replay helpers. Game state stays in the
 * example; no callbacks, entity registry or hidden game clock. */
#ifndef VKMIN_PLAY_H
#define VKMIN_PLAY_H
#include "gamekit.h"
#include <inttypes.h>

typedef struct { uint32_t ticks, origin; vkmin_inputs pending; } gk_clock;
typedef struct { uint32_t due; float alpha; vkmin_inputs input; } gk_step;

/* Queue edges until a simulation tick exists. Held state is the latest
 * snapshot; an edge is delivered to exactly the first catch-up tick. */
static inline gk_step gk_step_frame(gk_clock *c, uint32_t frame, uint32_t hz, vkmin_inputs input) {
    for (uint32_t k = 0; k < VKMIN_KEY_COUNT / 32; ++k) input.pressed[k] |= c->pending.pressed[k];
    input.buttons_pressed |= c->pending.buttons_pressed;
    input.wheel += c->pending.wheel;
    const vkmin_tick time = vkmin_ticks_for_frame(frame - c->origin, hz);
    if (time.ticks < c->ticks) gk_die("game clock moved backwards");
    const gk_step s = {time.ticks - c->ticks, time.alpha, input};
    c->ticks = time.ticks;
    c->pending = s.due ? (vkmin_inputs){0} : input;
    return s;
}
static inline vkmin_inputs gk_tick_input(gk_step s, uint32_t tick) {
    if (tick) {
        memset(s.input.pressed, 0, sizeof s.input.pressed);
        s.input.buttons_pressed = 0;
        s.input.wheel = 0;
    }
    return s.input;
}
static inline float gk_lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline uint32_t gk_float_bits(float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }

static inline FILE *gk_trace_open(int argc, char **argv, const char *fields) {
    for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "--state-trace")) {
        FILE *f = fopen(argv[i + 1], "w");
        if (!f) gk_die("cannot open state trace");
        fprintf(f, "vkmin-state-v1 frame hash %s\n", fields);
        return f;
    }
    return NULL;
}
/* Hash named scalar words, serialized little-endian, never struct padding
 * or GPU addresses. Text retains the words for finding the first divergence. */
static inline void gk_trace(FILE *f, uint32_t frame, const uint32_t *words, size_t n) {
    if (!f) return;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t k = 0; k < n; ++k) for (unsigned b = 0; b < 4; ++b) {
        hash ^= (words[k] >> (8 * b)) & 255u;
        hash *= UINT64_C(1099511628211);
    }
    fprintf(f, "%u %016" PRIx64, frame, hash);
    for (size_t k = 0; k < n; ++k) fprintf(f, " %u", words[k]);
    fputc('\n', f);
    if (ferror(f)) gk_die("state trace write failed");
}

static inline uint32_t gk_card(const vkr *r, int width, const char *title, const char *status,
                              const char *controls, bool help, Quad *out, uint32_t cap) {
    if (cap < 1) return 0;
    out[0] = (Quad){.pos = {(float)width * 0.5f, help ? 25 : 17, 0, 0},
        .size_uv0 = {(float)width, help ? 50 : 34, 0, 0}, .uv1 = {1, 1, 0, 0},
        .color = 0xd9241810u, .flags = VKMIN_QUAD_SCREEN};
    uint32_t n = 1;
    n += vkr_text(r, title, 8, 5, 14, 0xffb8e9ffu, out + n, cap - n);
    n += vkr_text(r, status, 8, 21, 10, 0xffeeeeeeu, out + n, cap - n);
    if (help) n += vkr_text(r, controls, 8, 36, 9, 0xffc5c5c5u, out + n, cap - n);
    return n;
}

/* A grading strip shared by the platformer and character showcase. */
static inline uint32_t gk_grade(vkmin_ctx *gpu, float warmth, float saturation) {
    uint32_t pixels[256 * 16];
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 256; ++x) {
        const float r = (float)(x % 16) / 15, g = (float)y / 15, b = (float)(x / 16) / 15;
        const float lum = .3f * r + .6f * g + .1f * b, warm = (lum - .5f) * warmth;
        pixels[y * 256 + x] = gk_rgba(lum + (r-lum)*saturation + warm,
            lum + (g-lum)*saturation + .3f*warm, lum + (b-lum)*saturation - warm, 1);
    }
    const vkmin_image image = vkmin_make_image(gpu, &(vkmin_image_desc){.width = 256, .height = 16,
        .pixels = VKMIN_BYTES(pixels), .sampler = VKMIN_SAMPLER_LINEAR_CLAMP, .label = "game.grade"});
    return vkmin_index(gpu, image);
}
#endif
