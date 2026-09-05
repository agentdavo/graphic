/* mkdemo -- writes the example games' demo files (tests/journals, .vkd):
 * scripted input, one vkmin_inputs snapshot per frame, in the format
 * --demo records and --play reads. The scripts are written here rather than
 * recorded by hand because CI has no hands; a demo recorded in a window is
 * the same file. Sizes are the lavapipe profile's 320x180, since mouse
 * positions are in pixels.
 *   mkdemo <out-dir> */
#include "vkmin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 320, H = 180, MAX_FRAMES = 1200 };

typedef struct { uint32_t frame_index; vkmin_inputs input; } demo_record;

static vkmin_inputs frames[MAX_FRAMES];
static int frame_count;

static void begin(int count) {
    frame_count = count;
    memset(frames, 0, sizeof frames);
    for (int f = 0; f < count; ++f) { frames[f].mouse_x = W / 2.0f; frames[f].mouse_y = H / 2.0f; }
}
static void hold(int key, int f0, int f1) {
    for (int f = f0; f < f1 && f < frame_count; ++f) frames[f].down[key / 32] |= 1u << (key % 32);
}
static void button(uint32_t bit, int f0, int f1) {
    for (int f = f0; f < f1 && f < frame_count; ++f) frames[f].buttons |= bit;
}
static void mouse(int f0, int f1, float x0, float y0, float x1, float y1) {
    for (int f = f0; f <= f1 && f < frame_count; ++f) {
        const float t = f1 > f0 ? (float)(f - f0) / (float)(f1 - f0) : 1.0f;
        frames[f].mouse_x = x0 + (x1 - x0) * t;
        frames[f].mouse_y = y0 + (y1 - y0) * t;
    }
    for (int f = f1 + 1; f < frame_count; ++f) { frames[f].mouse_x = x1; frames[f].mouse_y = y1; }
}
static void wheel(int f, float amount) { if (f < frame_count) frames[f].wheel = amount; }

static void write_demo(const char *dir, const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.vkd", dir, name);
    FILE *out = fopen(path, "wb");
    if (!out) { fprintf(stderr, "mkdemo: cannot write %s\n", path); exit(1); }
    const uint32_t header[4] = {0x444d4b56u, 1, W, H}; /* magic "VKMD", version, width, height */
    fwrite(header, sizeof header, 1, out);
    vkmin_inputs prev = {0};
    for (int f = 0; f < frame_count; ++f) {
        demo_record dr = {.frame_index = (uint32_t)f, .input = frames[f]};
        for (size_t k = 0; k < sizeof dr.input.down / sizeof dr.input.down[0]; ++k) dr.input.pressed[k] = dr.input.down[k] & ~prev.down[k];
        dr.input.buttons_pressed = dr.input.buttons & ~prev.buttons;
        prev = frames[f];
        fwrite(&dr, sizeof dr, 1, out);
    }
    fclose(out);
    printf("mkdemo: %s (%d frames)\n", path, frame_count);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: mkdemo <out-dir>\n"); return 2; }
    const char *dir = argv[1];

    /* 10_shooter: walk in, look around, fire twice, strafe. Short, because
     * Sponza under lavapipe is the slow one. */
    begin(170);
    hold('W', 10, 110);
    mouse(0, 90, W / 2.0f, H / 2.0f, W / 2.0f + 140.0f, H / 2.0f - 10.0f);
    button(VKMIN_MOUSE_LEFT, 100, 104);
    button(VKMIN_MOUSE_LEFT, 130, 134);
    hold('A', 120, 160);
    mouse(120, 165, W / 2.0f + 140.0f, H / 2.0f - 10.0f, W / 2.0f + 60.0f, H / 2.0f + 6.0f);
    write_demo(dir, "10_shooter");

    /* 11_rts: box-select the blue army, order it east, pan and zoom to watch. */
    begin(420);
    mouse(0, 20, 40.0f, 30.0f, 40.0f, 30.0f);
    button(VKMIN_MOUSE_LEFT, 20, 70);
    mouse(20, 70, 40.0f, 30.0f, 150.0f, 150.0f);
    mouse(80, 100, 150.0f, 150.0f, 250.0f, 100.0f);
    button(VKMIN_MOUSE_RIGHT, 100, 103);
    hold('D', 110, 170);
    wheel(180, 3.0f);
    wheel(200, 3.0f);
    mouse(200, 260, 250.0f, 100.0f, 180.0f, 90.0f);
    hold('W', 260, 300);
    write_demo(dir, "11_rts");

    /* 12_topdown: wander, click a prop, wander on. */
    begin(360);
    hold('D', 10, 80);
    hold('W', 40, 120);
    mouse(0, 60, W / 2.0f, H / 2.0f, 125.0f, 100.0f); /* onto a prop at frame 90 */
    button(VKMIN_MOUSE_LEFT, 90, 93);
    hold('S', 140, 200);
    hold('A', 160, 240);
    mouse(200, 260, 125.0f, 100.0f, 120.0f, 120.0f);
    button(VKMIN_MOUSE_LEFT, 270, 273);
    hold('D', 280, 340);
    write_demo(dir, "12_topdown");

    /* 13_platformer: run right, jump the gaps, ride a platform. */
    begin(420);
    hold('D', 10, 110);
    hold(VKMIN_KEY_SPACE, 60, 64);
    hold('D', 130, 230);
    hold(VKMIN_KEY_SPACE, 150, 154);
    hold(VKMIN_KEY_SPACE, 215, 219);
    hold('D', 260, 330);
    hold(VKMIN_KEY_SPACE, 300, 304);
    hold('A', 340, 380);
    write_demo(dir, "13_platformer");

    /* 14_anime: the camera orbits on its own; the mouse nudges the orbit. */
    begin(300);
    mouse(0, 150, W / 2.0f, H / 2.0f, W / 2.0f + 60.0f, H / 2.0f - 20.0f);
    button(VKMIN_MOUSE_LEFT, 40, 150);
    write_demo(dir, "14_anime");
    return 0;
}
