#include "play.h"
#include <stdio.h>

int main(void) {
    gk_clock c = {0};
    vkmin_inputs input = {0};
    input.pressed[1] = 1; input.buttons_pressed = VKMIN_MOUSE_LEFT;
    if (gk_step_frame(&c, 1, 30, input).due) return 1;
    const gk_step s = gk_step_frame(&c, 6, 30, (vkmin_inputs){0});
    if (s.due != 3 || !vkmin_key_pressed(&s.input, VKMIN_KEY_SPACE)) return 2;
    for (uint32_t k = 0; k < s.due; ++k) {
        const vkmin_inputs tick = gk_tick_input(s, k);
        if ((tick.buttons_pressed != 0) != (k == 0)) return 3;
    }
    const gk_step next = gk_step_frame(&c, 8, 30, (vkmin_inputs){0});
    if (next.input.buttons_pressed || next.input.pressed[1]) return 4;
    if (gk_lerp(10, 20, .5f) != 15) return 5;
    c = (gk_clock){.origin = 80};
    if (gk_step_frame(&c, 80, 60, input).due || gk_step_frame(&c, 81, 60, (vkmin_inputs){0}).due != 1) return 6;
    puts("play: ok (queued edges, catch-up, restart, interpolation)");
    return 0;
}
