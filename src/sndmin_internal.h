/* Private layout shared by the sndmin translation units, and the one place the
 * game/mixer boundary is written down.
 *
 * Two threads touch sndmin_ctx and they divide it by field, not by lock. The
 * game thread owns everything from `device` to `latest`; the mixer -- the
 * device callback when live, sndmin_render's caller when offline -- owns
 * everything from `voices` down. Neither reads the other's half. The only
 * genuinely shared mutable state is the two single-producer/single-consumer
 * rings below, plus the immutable resource tables, which are frozen once
 * `started` is set and are read-only from then until the device is joined.
 *
 * Both rings use free-running uint32 counters rather than wrapped indices: the
 * slot is counter % capacity, and the occupancy is the unsigned difference
 * write - read, which stays correct across the 2^32 wrap because the capacity
 * is far smaller than 2^32. Each index has exactly one writer. The producer
 * fills the slot, then stores its counter with release; the consumer loads
 * that counter with acquire, then reads the slot -- that pair is what publishes
 * the payload, so neither may be relaxed. The producer's load of the far index
 * is acquire because reclaiming a slot must not be reordered before the
 * consumer finished with it; each side's load of its own index is relaxed
 * because nobody else writes it.
 *
 * commands[] runs game -> mixer: the game must not block, so a full ring is a
 * dropped command and a terminal failure, never a wait. snapshots[] runs mixer
 * -> game: a full ring simply skips the update, because the mixer must not
 * block either and stale counters are harmless. Nothing below the boundary
 * allocates, does IO, takes a lock or reads a clock; sndmin.c's
 * callback_active guard exists to make a violation of that testable. */
#ifndef SNDMIN_INTERNAL_H
#define SNDMIN_INTERNAL_H
#include "sndmin.h"
#include "sndmin_plat.h"
#include "sndmin_io.h"
#include "sndmin_dsp.h"
#include "min_jrnl.h"
#include <stdatomic.h>
/* Fixed pools; sndmin allocates nothing per voice or per frame. Counts unless
 * noted, and every "samples" here is at SNDMIN_RATE, so SND_ECHO is half a
 * second of per-voice reflection history and SND_FDN is about 0.68 s, which is
 * what caps the music bus delay time. */
enum { SND_RES=128,             /* sounds, patches or songs per context */
    SND_STREAMS=8, SND_STOPPED_GROUPS=128,
    SND_COMMAND_CAP=2048,       /* game -> mixer ring slots */
    SND_SNAP=16,                /* mixer -> game ring slots */
    SND_BLOCK=512,              /* frames of PCM carried by one CMD_PCM */
    SND_STREAM_BUFFER=65536,    /* frames buffered ahead per stream */
    SND_ECHO=24000,             /* samples of per-voice tap/chorus delay line */
    SND_FDN=32768 };            /* samples per reverb and delay line */
enum { CMD_PLAY=1,CMD_SET,CMD_STOP,CMD_ACOUSTIC,CMD_FRAME,CMD_PCM,CMD_GROUP_SET };
/* The unit that crosses the thread boundary, and the unit that is journalled:
 * flat, self-contained and pointer-free, so a byte copy is a whole command and
 * a replay can hand a recorded one straight back to the mixer. `sample` is
 * when it takes effect, `order` breaks ties for commands stamped at the same
 * sample so the sort before dispatch is stable, and `id` names the voice,
 * stream or group. `count` is overloaded per op: the owning group for
 * CMD_PLAY, group-ness for CMD_STOP/CMD_ACOUSTIC, and PCM frames for CMD_PCM. */
typedef struct {
    uint64_t sample,order;
    uint32_t op,id,count;
    union {
        sndmin_play_desc play;
        sndmin_voice_desc set;
        sndmin_acoustic acoustic;
        struct { float gain[4],delay,feedback; sndmin_acoustic room; } frame;
        float pcm[SND_BLOCK*2];
        struct { sndmin_voice_desc voice; float ratio; sndmin_acoustic acoustic; } group;
        float fade;
    } u;
} snd_command;
typedef struct { float *pcm; uint32_t frames,channels; } snd_sound;
typedef struct {
    uint32_t row,channel,note,patch,volume; int effect;
} snd_note;
typedef struct {
    snd_note notes[2048]; uint32_t count,rows,order[128],orders;
    uint32_t pattern_first[32],pattern_count[32]; float bpm,swing;
    int arp[8][2];
} snd_song;
typedef struct {
    sndmin_reader *reader; uint32_t channels,rate,voice;
    uint64_t written; bool loop,eof,active;
} snd_stream_game;
typedef struct {
    float pcm[SND_STREAM_BUFFER*2]; uint64_t read,written;
    double cursor; uint32_t channels,rate; bool eof;
} snd_stream_mix;
/* The game thread's shadow of a spatial voice. It exists only so sndmin_frame
 * can re-evaluate acoustics for voices that are probably still audible without
 * asking the mixer anything: `expires` is a conservative game-side guess at
 * when the voice is done, and a wrong guess costs a stale entry, never
 * correctness. The mixer's own snd_mix_voice is the truth. */
typedef struct {
    uint32_t id; sndmin_play_desc play; uint64_t expires;
} snd_game_voice;
/* One mixer voice. Entirely mixer-owned, and zeroed by CMD_PLAY, so every
 * filter and delay state below starts from silence on a steal. */
typedef struct {
    uint32_t id,group;            /* 0 = free slot; group = the song voice that owns it */
    sndmin_play_desc play;
    double cursor;                /* PCM read position in source frames, fractional */
    uint64_t age,release_at;      /* samples since the note started / since release */
    float release_level,noise_filter,source_gain; /* env at release; percussion noise state; pre-group gain */
    float env,filter_env,phase[4][3],ladder[4],filter,peak,gain,target_gain,gain_step;
    uint32_t gain_left,stop_left; /* samples remaining in the gain ramp / the stop fade */
    bool released,stopping;
    sndmin_acoustic acoustic,target; uint32_t acoustic_left; /* current, goal, samples left to reach it */
    float echo[SND_ECHO],tap_filter[4]; uint32_t echo_head;  /* reflection ring, one per tap */
    float chorus_phase;
} snd_mix_voice;
/* A mix bus and its shared tail. `lines` is a four-line feedback delay network
 * (the reverb), `damping` the one-pole lowpass in each of its loops, `delay`
 * the separate tempo echo the music bus alone uses. `feedback` and `length`
 * are derived from the room, so they change when the listener moves; `wet` is
 * how much of the network reaches the output and `gain` is the bus fader. */
typedef struct {
    float lines[4][SND_FDN],damping[4],delay[SND_FDN]; uint32_t head;
    float gain,feedback,wet; uint32_t length[4];
} snd_bus;
typedef struct {
    sndmin_stats stats; uint64_t stream_read[SND_STREAMS];
} snd_snapshot;
struct sndmin_ctx {
    /* Immutable after first frame; shared read-only, freed after join. */
    sndmin_desc desc; uint32_t channels;
    snd_sound sounds[SND_RES]; sndmin_patch_desc patches[SND_RES]; snd_song *songs[SND_RES];
    uint32_t sound_count,patch_count,song_count,stream_count;
    /* Only shared mutable state: two bounded SPSC rings; protocol at the top of
     * this file. commands: game writes, mixer reads. snapshots: the reverse. */
    _Atomic uint32_t command_read,command_write,snapshot_read,snapshot_write;
    snd_command commands[SND_COMMAND_CAP]; snd_snapshot snapshots[SND_SNAP];
    /* Game-thread owner. The mixer never reads any of it; `pending` in
     * particular is the game's own staging list, sorted and released into the
     * ring by sndmin_frame, and it is the only thing here that reallocates. */
    sndmin_device *device; bool started,rendered,failed,own_journal,replaying;
    FILE *journal; sndmin_frame_desc frame; uint64_t game_sample,serial;
    snd_command frame_command; uint32_t bus_override;
    sndmin_box *geometry; uint32_t geometry_cap; sndmin_material materials[256];
    snd_command *pending; size_t pending_count,pending_cap;
    uint32_t next_voice; snd_game_voice game_voices[SNDMIN_MAX_VOICES];
    uint32_t song_parents[SND_RES],song_parent_count; float song_gain[SND_RES];
    snd_stream_game streams[SND_STREAMS]; snd_snapshot latest;
    /* Mixer owner. Offline code enters this only through sndmin_mix. */
    snd_mix_voice voices[SNDMIN_MAX_VOICES]; snd_stream_mix stream_mix[SND_STREAMS];
    snd_bus buses[4]; uint64_t sample; sndmin_stats stats;
    uint32_t delay_length; float delay_feedback;
    uint32_t stopped_groups[SND_STOPPED_GROUPS],stopped_count; /* overflow counts in dropped_commands */
    snd_command group_updates[SND_RES]; uint32_t group_update_count;
    bool reference_pcm; /* test-only scalar interpolation comparison */
    float resample_a[8],resample_b[8],lfe;
    uint64_t output_phase; bool output_primed;
};
/* Game thread only; each one may allocate, do IO, or both. The mixer's own
 * entry point is sndmin_mix, declared in sndmin_plat.h, and it does neither. */
void sndmin_submit(sndmin_ctx *,snd_command);   /* stages + journals one command */
void sndmin_song_schedule(sndmin_ctx *,const sndmin_play_desc *,uint32_t voice);
bool sndmin_write_output(sndmin_ctx *,uint32_t,const char *,const char *); /* io */
bool sndmin_feed(sndmin_ctx *,uint64_t until,size_t *index); /* offline: staged -> ring */
void sndmin_pump_streams(sndmin_ctx *,uint64_t when);        /* decodes: io */
bool sndmin_command_valid(const sndmin_ctx *,const snd_command *); /* pure over ctx+cmd */
#ifdef SNDMIN_TEST_GUARD
_Noreturn void sndmin_test_violation(unsigned kind);
#endif
#endif
