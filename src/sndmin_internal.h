#ifndef SNDMIN_INTERNAL_H
#define SNDMIN_INTERNAL_H
#include "sndmin.h"
#include "sndmin_plat.h"
#include "sndmin_io.h"
#include "sndmin_dsp.h"
#include "jrnl.h"
#include <stdatomic.h>
enum { SND_RES=128, SND_STREAMS=8, SND_COMMAND_CAP=2048, SND_SNAP=16,
    SND_BLOCK=512, SND_STREAM_BUFFER=65536, SND_ECHO=24000, SND_FDN=32768 };
enum { CMD_PLAY=1,CMD_SET,CMD_STOP,CMD_ACOUSTIC,CMD_FRAME,CMD_PCM,CMD_GROUP_SET };
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
typedef struct {
    uint32_t id; sndmin_play_desc play; uint64_t expires;
} snd_game_voice;
typedef struct {
    uint32_t id,group; sndmin_play_desc play;
    double cursor; uint64_t age,release_at; float release_level,noise_filter,source_gain;
    float env,filter_env,phase[4][3],ladder[4],filter,peak,gain,target_gain,gain_step;
    uint32_t gain_left,stop_left; bool released,stopping;
    sndmin_acoustic acoustic,target; uint32_t acoustic_left;
    float echo[SND_ECHO],tap_filter[4]; uint32_t echo_head;
    float chorus_phase;
} snd_mix_voice;
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
    /* Only shared mutable state: two bounded SPSC rings. */
    _Atomic uint32_t command_read,command_write,snapshot_read,snapshot_write;
    snd_command commands[SND_COMMAND_CAP]; snd_snapshot snapshots[SND_SNAP];
    /* Game-thread owner. */
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
    uint32_t stopped_groups[128],stopped_count;
    snd_command group_updates[SND_RES]; uint32_t group_update_count;
    bool reference_pcm; /* test-only scalar interpolation comparison */
    float resample_a[8],resample_b[8],lfe;
    uint64_t output_phase; bool output_primed;
};
void sndmin_submit(sndmin_ctx *,snd_command);
void sndmin_song_schedule(sndmin_ctx *,const sndmin_play_desc *,uint32_t voice);
bool sndmin_write_output(sndmin_ctx *,uint32_t,const char *,const char *);
bool sndmin_feed(sndmin_ctx *,uint64_t until,size_t *index);
void sndmin_pump_streams(sndmin_ctx *,uint64_t when);
bool sndmin_command_valid(const sndmin_ctx *,const snd_command *);
#ifdef SNDMIN_TEST_GUARD
_Noreturn void sndmin_test_violation(unsigned kind);
#endif
#endif
