/* sndmin v0.1 -- game thread API. No device/decoder types or game callbacks.
 * Zero descriptors select stereo, 48 kHz, unity gain/pitch, effects bus.
 * Frame first, then issue that frame's commands. Resources are immutable after
 * first frame; shutdown joins the device before freeing anything.
 * Handles belong to one context and expire at shutdown; voices may be stolen.
 * Check resource/play handles and render results. sndmin_ok distinguishes a
 * recoverable rejection from a terminal command/journal failure. On terminal
 * failure, shut down and recreate the context. No operation calls user code. */
#ifndef SNDMIN_H
#define SNDMIN_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "min_types.h"
#ifndef SNDMIN_MAX_VOICES
#define SNDMIN_MAX_VOICES 64
#endif
#define SNDMIN_RATE 48000u
#define SNDMIN_FRAME_SAMPLES 800u
typedef struct sndmin_ctx sndmin_ctx;
typedef struct { uint32_t id; } sndmin_sound;
typedef struct { uint32_t id; } sndmin_stream;
typedef struct { uint32_t id; } sndmin_patch;
typedef struct { uint32_t id; } sndmin_song;
typedef struct { uint32_t id; } sndmin_voice;
typedef struct { const void *data; size_t size; } sndmin_bytes;
#define SNDMIN_BYTES(a) ((sndmin_bytes){(a), sizeof(a)})
typedef enum { SNDMIN_STEREO, SNDMIN_51, SNDMIN_71 } sndmin_layout;
typedef enum { SNDMIN_EFFECTS, SNDMIN_MUSIC, SNDMIN_VOICE, SNDMIN_MASTER } sndmin_bus;
typedef enum { SNDMIN_SAW, SNDMIN_PULSE, SNDMIN_TRIANGLE, SNDMIN_NOISE } sndmin_wave;
typedef enum { SNDMIN_MELODIC, SNDMIN_KICK, SNDMIN_SNARE, SNDMIN_HAT, SNDMIN_TOM, SNDMIN_GATED_SNARE } sndmin_instrument;
typedef enum { SNDMIN_LFO_PITCH, SNDMIN_LFO_WIDTH, SNDMIN_LFO_CUTOFF } sndmin_lfo_route;
typedef struct {
    sndmin_layout layout;
    uint32_t rate;                /* device rate; mixer always 48000; 0 = 48000 */
    bool offline;
    int argc; char **argv;        /* --offline, --record FILE */
    uint32_t latency_frames;      /* live scheduling offset; 0 = 3 */
    FILE *journal;               /* borrowed shared jrnl stream, already opened */
} sndmin_desc;
typedef struct { float attack, decay, sustain, release; } sndmin_adsr;
typedef struct {
    sndmin_wave wave[2]; sndmin_instrument instrument;
    float detune, sub, pulse_width; /* detune cents; width 0 = .5 */
    float cutoff, resonance, filter_env; /* Hz, [0,1), octaves */
    sndmin_adsr amp, filter;
    float lfo_hz, lfo_depth; sndmin_lfo_route lfo_route;
    uint32_t unison; float unison_cents, chorus;
} sndmin_patch_desc;
typedef struct {
    vec3 position, velocity;
    float gain, pitch;            /* set(): zero gain mutes; pitch 1/1024..8, 0 = 1 */
    float min_radius, max_radius; /* defaults 1, 100 metres */
    float reverb_send, lfe_send;
    float fade_seconds;
} sndmin_voice_desc;
typedef struct {
    sndmin_sound sound; sndmin_stream stream; sndmin_patch patch; sndmin_song song;
    sndmin_voice_desc voice;
    sndmin_bus bus; int priority;
    bool spatial, loop;
    uint32_t note;                /* MIDI; 0 = 69 */
    float duration;               /* synth gate seconds; 0 = until stop */
} sndmin_play_desc;
typedef struct { vec3 min, max; uint32_t material; } sndmin_box;
typedef struct { float absorption, transmission; } sndmin_material;
typedef struct {
    uint32_t index;
    vec3 listener, velocity, forward, up; /* default forward -Z, up +Y */
    const sndmin_box *boxes; uint32_t box_count;
    const sndmin_material *materials; uint32_t material_count;
    float bus_gain[4];            /* legacy frame defaults: zero=unity, negative=mute.
                                  * Prefer sndmin_bus_set: literal, persistent gains. */
    float delay_seconds, delay_feedback; /* music bus; default off */
} sndmin_frame_desc;
typedef struct { float delay, gain, lowpass; } sndmin_tap;
typedef struct {
    float gain, lowpass, doppler, pan[8];
    sndmin_tap taps[4];
    float mean_free_path, openness, decay;
} sndmin_acoustic;
typedef struct {
    uint64_t samples;
    uint32_t voices, underruns, late_commands, stolen, dropped_commands;
    float cpu_percent;           /* -1 = unavailable (no callback clock reads) */
    float mean_free_path, openness, decay;
} sndmin_stats;

sndmin_ctx *sndmin_init(const sndmin_desc *);                       /* allocates, io */
void sndmin_shutdown(sndmin_ctx *);                                /* joins, frees, io */
sndmin_sound sndmin_load(sndmin_ctx *, const char *);               /* before frame: io */
/* Interleaved float PCM, copied and resampled at load; 1 or 2 channels. */
sndmin_sound sndmin_make_sound(sndmin_ctx *, sndmin_bytes, uint32_t channels, uint32_t rate); /* allocates */
sndmin_stream sndmin_open_stream(sndmin_ctx *, const char *);        /* before frame: io */
sndmin_patch sndmin_make_patch(sndmin_ctx *, const sndmin_patch_desc *); /* before frame */
sndmin_song sndmin_load_song(sndmin_ctx *, const char *);            /* before frame: io */
sndmin_voice sndmin_play(sndmin_ctx *, const sndmin_play_desc *);    /* queues */
/* After frame: setters use literal gain (0=mute, 1=unity). Bus values persist
 * across frames and override that bus's legacy frame gain until shutdown. */
bool sndmin_bus_set(sndmin_ctx *, sndmin_bus, float gain);          /* queues, writes ctx */
bool sndmin_ok(const sndmin_ctx *);                               /* reads ctx; false after terminal failure */
void sndmin_set(sndmin_ctx *, sndmin_voice, const sndmin_voice_desc *); /* queues */
void sndmin_stop(sndmin_ctx *, sndmin_voice, float fade_seconds);    /* queues */
void sndmin_frame(sndmin_ctx *, const sndmin_frame_desc *);          /* game thread: io, queues */
sndmin_stats sndmin_stats_get(sndmin_ctx *);                        /* consumes snapshots */
void sndmin_dump(sndmin_ctx *, FILE *);                            /* io */
/* Single offline render per context, starting at sample zero. Frames include tails.
 * Recreate/replay to render again. Failure returns false, never partial success. */
bool sndmin_render(sndmin_ctx *, uint32_t frames, const char *wav, const char *spectrogram_png); /* io */
bool sndmin_replay(sndmin_ctx *, const char *journal);              /* before frame: io */
/* Pure game-thread acoustic reference, also useful in tests/tools. */
sndmin_acoustic sndmin_acoustics(const sndmin_frame_desc *, const sndmin_voice_desc *, sndmin_layout); /* pure */
#endif
