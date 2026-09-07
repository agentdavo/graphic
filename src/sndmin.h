/* sndmin v0.1 -- game thread API. No device/decoder types or game callbacks.
 * Zero descriptors select stereo, 48 kHz, unity gain/pitch, effects bus.
 * Frame first, then issue that frame's commands. Resources are immutable after
 * first frame; shutdown joins the device before freeing anything.
 * Handles belong to one context and expire at shutdown; voices may be stolen.
 * Check resource/play handles and render results. sndmin_ok distinguishes a
 * recoverable rejection from a terminal command/journal failure. On terminal
 * failure, shut down and recreate the context. No operation calls user code.
 *
 * Threads. Every function declared here is game-thread-only, and none of them
 * waits on the device. A live context owns a second thread inside the audio
 * backend that the caller never names: these calls append to a game-side list,
 * sndmin_frame hands the due part of that list to the mixer through a
 * lock-free ring, and the mixer publishes its counters back the same way. So a
 * command issued while building frame N is not heard at frame N -- it is
 * restamped latency_frames later, which is the price of never making the
 * callback block on the game. An offline context has no second thread at all:
 * sndmin_render pulls the mixer forward inline, on the calling thread.
 *
 * Determinism. The mixer always runs at SNDMIN_RATE whatever the device rate
 * is, in scalar float, and sndmin's sources are compiled with FP contraction
 * and fast-math off. The same commands therefore yield bit-identical PCM from
 * one build, which is what makes a journal replay checkable against a golden
 * WAV. Time is samples at SNDMIN_RATE everywhere; a frame's commands are
 * stamped at index * SNDMIN_FRAME_SAMPLES. */
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
    uint32_t latency_frames;      /* live scheduling offset, in SNDMIN_FRAME_SAMPLES
                                   * frames; 0 = 3. Unused offline. */
    FILE *journal;               /* borrowed shared jrnl stream, already opened */
} sndmin_desc;
/* Classic ADSR. attack/decay/release are seconds; sustain is the level held
 * after decay, 0..1. An all-zero adsr means "unset" and picks a default. */
typedef struct { float attack, decay, sustain, release; } sndmin_adsr;
typedef struct {
    sndmin_wave wave[2]; sndmin_instrument instrument;
    float detune, sub, pulse_width; /* detune cents; width 0 = .5 */
    float cutoff, resonance, filter_env; /* Hz, [0,1), octaves */
    sndmin_adsr amp, filter;      /* amp shapes level; filter scales filter_env */
    /* One LFO per voice, a sine of lfo_hz scaled by lfo_depth. The unit of the
     * depth follows the route: for PITCH one is one semitone, for WIDTH one is
     * 0.4 of the pulse duty cycle, for CUTOFF one is three octaves. */
    float lfo_hz, lfo_depth; sndmin_lfo_route lfo_route;
    uint32_t unison; float unison_cents, chorus; /* 1..4 detuned copies; cents apart */
    /* Orchestral articulation. Appended, and every one zero by default, because
     * zero is exactly the behaviour that existed before they did: an untouched
     * patch sounds as it always has, and the frozen journals -- whose 0x202
     * records are 92 bytes -- keep replaying. What these buy is what separates
     * a section from a synth playing the same notes. */
    float bow;      /* attack curvature: 0 the original ramp, 1 a bowed swell */
    float ensemble; /* cents of slow independent drift per unison voice; a
                     * fixed spread chorusses, drift makes players */
    float dynamic;  /* octaves the filter closes as the voice's gain falls,
                     * so playing softer changes timbre and not only level */
} sndmin_patch_desc;
enum { SNDMIN_PATCH_BYTES_V1 = 92 }; /* the record older journals wrote */
typedef struct {
    vec3 position, velocity;
    float gain, pitch;            /* set(): zero gain mutes; pitch 1/1024..8, 0 = 1 */
    /* Metres. Full gain inside min_radius, silence past max_radius. play()
     * substitutes 1 for a non-positive min_radius and min_radius + 100 for a
     * max_radius that does not exceed it; sndmin_acoustics, which anyone may
     * call on a raw descriptor, substitutes a flat 100 for the latter. Fill
     * both in if you care which. */
    float min_radius, max_radius;
    float reverb_send, lfe_send;  /* share of the voice sent on, 0..1 */
    float fade_seconds;           /* ramp to the new gain, and stop()'s fade out */
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
/* One early reflection: delay in seconds behind the direct sound, gain linear,
 * lowpass the coefficient of a one-pole smoother (1 = open, 0 = fully damped). */
typedef struct { float delay, gain, lowpass; } sndmin_tap;
/* What a listener/source pair sounds like, evaluated on the game thread and
 * sent to the mixer, which then ramps to it over ~0.1 s rather than jumping.
 * The last three describe the room around the listener rather than this
 * source, and drive the shared reverb; see sndmin_acoustics for the model. */
typedef struct {
    float gain, lowpass, doppler, pan[8]; /* linear; one-pole coeff; pitch ratio; per-channel gains */
    sndmin_tap taps[4];           /* the four loudest early reflections */
    /* Metres; fraction of probe rays that escaped, 0..1; reverberation time in
     * seconds -- the tail loses about 18 dB per `decay`, not the usual 60. */
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
 * Recreate/replay to render again. Failure returns false, never partial success.
 * Offline contexts only: this drives the mixer on the calling thread, so the
 * whole engine collapses to one thread and the result is reproducible. */
bool sndmin_render(sndmin_ctx *, uint32_t frames, const char *wav, const char *spectrogram_png); /* io */
bool sndmin_replay(sndmin_ctx *, const char *journal);              /* before frame: io */
/* Pure game-thread acoustic reference, also useful in tests/tools. */
sndmin_acoustic sndmin_acoustics(const sndmin_frame_desc *, const sndmin_voice_desc *, sndmin_layout); /* pure */
#endif
