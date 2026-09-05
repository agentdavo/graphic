/* "Morning water": original 120-second valley score, authored in game code.
 * Public sndmin API only. Create before frame zero; call after sndmin_frame
 * on EVERY 60 Hz simulation tick, including ticks with no rendered image.
 * Eight 15-second phrases, then silence. Recreate context to restart/replay. */
#ifndef VALLEY_SCORE_H
#define VALLEY_SCORE_H
#include "sndmin.h"
enum { VALLEY_SCORE_TICKS = 7200, VALLEY_SCORE_RENDER_TICKS = 7440 };
typedef struct { sndmin_patch pad, bass, bell; } valley_score;
static inline valley_score valley_score_init(sndmin_ctx *ctx) {
    valley_score score = {0};
    score.pad = sndmin_make_patch(ctx, &(sndmin_patch_desc){
            .wave={SNDMIN_TRIANGLE,SNDMIN_SAW}, .cutoff=700, .detune=8,
            .unison=3, .unison_cents=9, .chorus=0.6f, .filter_env=0.7f,
            .amp={2,2,0.65f,4}, .filter={1.5f,4,0.3f,4},
            .lfo_hz=0.07f, .lfo_depth=0.35f, .lfo_route=SNDMIN_LFO_CUTOFF});
    score.bass = sndmin_make_patch(ctx, &(sndmin_patch_desc){
            .wave={SNDMIN_TRIANGLE,SNDMIN_TRIANGLE}, .cutoff=280,
            .amp={1.8f,2,0.55f,3}});
    score.bell = sndmin_make_patch(ctx, &(sndmin_patch_desc){
            .wave={SNDMIN_TRIANGLE,SNDMIN_TRIANGLE}, .cutoff=3200, .detune=3,
            .amp={0.04f,2,0.1f,3}});
    return score;
}
static inline bool valley_score_ready(valley_score score) {
    return score.pad.id && score.bass.id && score.bell.id;
}
static inline bool valley_score_frame(sndmin_ctx *ctx, valley_score score, uint32_t tick, float gain) {
    /* Dm(add9), Fmaj7, Cadd9, Gsus2, Bbmaj7, Fmaj7, Am7, Dsus2.
     * Shared upper notes keep the transitions quiet; no drums or arpeggiator. */
    static const uint32_t chords[8][4] = {
        {50,57,64,65}, {53,60,64,69}, {48,55,62,64}, {55,62,67,69},
        {46,53,57,62}, {53,60,64,69}, {45,52,55,60}, {50,57,62,64}
    };
    static const uint32_t melody[8][2] = {
        {81,76}, {79,76}, {79,74}, {81,79}, {77,74}, {76,72}, {76,79}, {76,74}
    };
    if (tick >= VALLEY_SCORE_TICKS || gain <= 0) return true;
    const uint32_t phrase = tick/900, within = tick%900;
    if (!within) {
        const float duration = phrase == 7 ? 10.0f : 12.5f;
        for (unsigned note = 0; note < 4; ++note) {
            if (!sndmin_play(ctx, &(sndmin_play_desc){.patch=score.pad, .bus=SNDMIN_MUSIC,
                .note=chords[phrase][note], .duration=duration,
                .voice={.gain=0.065f*gain, .reverb_send=0.3f}}).id) return false;
        }
        if (!sndmin_play(ctx, &(sndmin_play_desc){.patch=score.bass, .bus=SNDMIN_MUSIC,
            .note=chords[phrase][0]-12, .duration=duration,
            .voice={.gain=0.09f*gain, .reverb_send=0.1f}}).id) return false;
    }
    if (within == 240 || within == 630) {
        if (!sndmin_play(ctx, &(sndmin_play_desc){.patch=score.bell, .bus=SNDMIN_MUSIC,
            .note=melody[phrase][within == 630], .duration=1.5f,
            .voice={.gain=0.075f*gain, .reverb_send=0.45f}}).id) return false;
    }
    return true;
}
#endif
