#include "sndmin_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#ifdef SNDMIN_TEST_GUARD
#include <time.h>
#endif
_Static_assert(sizeof(float)==4 && SNDMIN_MAX_VOICES>0,"32-bit float and nonempty voice pool required");
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"audio requires lock-free atomic indices");
static _Thread_local bool callback_active;
bool sndmin_callback_active(void) { return callback_active; }
#ifdef SNDMIN_TEST_GUARD
_Noreturn void sndmin_test_violation(unsigned kind) {
    callback_active=true;
    if(kind==0) { void *volatile p=malloc(1); if(p) *(volatile unsigned char *)p=0; free(p); }
    if(kind==1) { FILE *volatile f=fopen("forbidden-callback-file","rb"); if(f) fclose(f); }
    if(kind==2) { volatile clock_t tick=clock(); (void)tick; }
    exit(23); /* Reaching here means interposition failed; negative test must fail. */
}
#endif
static bool valid(float f) { return isfinite(f) && snd_abs(f)<=1000000; }
static bool valid_voice(const sndmin_voice_desc *v) {
    return valid(v->position.x)&&valid(v->position.y)&&valid(v->position.z)&&
        valid(v->velocity.x)&&valid(v->velocity.y)&&valid(v->velocity.z)&&
        valid(v->gain)&&valid(v->pitch)&&valid(v->min_radius)&&valid(v->max_radius)&&
        valid(v->reverb_send)&&valid(v->lfe_send)&&valid(v->fade_seconds)&&
        (v->pitch==0||(v->pitch>=1.0f/1024&&v->pitch<=8))&&v->gain>=0&&v->gain<=16&&v->fade_seconds>=0;
}
static bool valid_acoustic(const sndmin_acoustic *a) {
    if(!valid(a->gain)||a->gain<0||a->gain>1||!valid(a->lowpass)||a->lowpass<0||a->lowpass>1||
        !valid(a->doppler)||a->doppler<0||a->doppler>2||!valid(a->mean_free_path)||a->mean_free_path<0||
        a->mean_free_path>250||!valid(a->openness)||a->openness<0||a->openness>1||
        !valid(a->decay)||a->decay<0||a->decay>8) return false;
    for(unsigned k=0;k<8;++k) if(!valid(a->pan[k])||snd_abs(a->pan[k])>1.001f) return false;
    for(unsigned k=0;k<4;++k) {
        const sndmin_tap t=a->taps[k];
        if(!valid(t.delay)||t.delay<0||t.delay>=0.5f||!valid(t.gain)||t.gain<0||t.gain>1||
            !valid(t.lowpass)||t.lowpass<0||t.lowpass>1) return false;
    } return true;
}
bool sndmin_command_valid(const sndmin_ctx *c,const snd_command *cmd) {
    if(cmd->op<CMD_PLAY||cmd->op>CMD_GROUP_SET||cmd->sample/800>UINT32_MAX) return false;
    if(cmd->op==CMD_GROUP_SET) return cmd->id>0&&valid_voice(&cmd->u.group.voice)&&valid(cmd->u.group.ratio)&&
        cmd->u.group.ratio>=0&&valid_acoustic(&cmd->u.group.acoustic);
    if(cmd->op==CMD_PLAY) {
        const sndmin_play_desc *p=&cmd->u.play;
        /* Validate object representations before reading journalled _Bool. */
        unsigned char spatial,loop;
        memcpy(&spatial,&p->spatial,1); memcpy(&loop,&p->loop,1);
        if(spatial>1||loop>1) return false;
        const unsigned sources=(p->sound.id!=0)+(p->patch.id!=0)+(p->stream.id!=0);
        return cmd->id>0&&sources==1&&!p->song.id&&p->sound.id<=c->sound_count&&p->patch.id<=c->patch_count&&
            p->stream.id<=c->stream_count&&(unsigned)p->bus<=SNDMIN_MASTER&&p->note<=127&&valid_voice(&p->voice)&&
            valid(p->duration)&&p->duration>=0;
    }
    if(cmd->op==CMD_SET) return cmd->id>0&&valid_voice(&cmd->u.set);
    if(cmd->op==CMD_STOP) return cmd->id>0&&valid(cmd->u.fade)&&cmd->u.fade>=0;
    if(cmd->op==CMD_ACOUSTIC) return cmd->id>0&&valid_acoustic(&cmd->u.acoustic);
    if(cmd->op==CMD_FRAME) {
        for(unsigned i=0;i<4;++i) if(!valid(cmd->u.frame.gain[i])||cmd->u.frame.gain[i]<0||cmd->u.frame.gain[i]>4) return false;
        return valid(cmd->u.frame.delay)&&cmd->u.frame.delay>=0&&cmd->u.frame.delay<=0.68f&&
            valid(cmd->u.frame.feedback)&&cmd->u.frame.feedback>=0&&cmd->u.frame.feedback<=0.95f&&valid_acoustic(&cmd->u.frame.room);
    }
    if(!cmd->id||cmd->id>c->stream_count||cmd->count>SND_BLOCK) return false;
    for(uint32_t i=0;i<cmd->count*c->stream_mix[cmd->id-1].channels;++i) if(!valid(cmd->u.pcm[i])) return false;
    return true;
}
static sndmin_voice_desc defaults(sndmin_voice_desc v) {
    if(v.gain==0) v.gain=1;
    if(v.pitch==0) v.pitch=1;
    if(v.min_radius<=0) v.min_radius=1;
    if(v.max_radius<=v.min_radius) v.max_radius=v.min_radius+100;
    return v;
}
static bool ring_push(sndmin_ctx *c,const snd_command *cmd) {
    const uint32_t w=atomic_load_explicit(&c->command_write,memory_order_relaxed);
    const uint32_t r=atomic_load_explicit(&c->command_read,memory_order_acquire);
    if(w-r==SND_COMMAND_CAP) return false;
    c->commands[w%SND_COMMAND_CAP]=*cmd;
    atomic_store_explicit(&c->command_write,w+1,memory_order_release); return true;
}
void sndmin_submit(sndmin_ctx *c,snd_command cmd) {
    if(c->failed) return;
    cmd.order=c->serial++;
    if(c->pending_count==c->pending_cap) {
        const size_t cap=c->pending_cap?c->pending_cap*2:256;
        if(cap>SIZE_MAX/sizeof(cmd)) { c->failed=true; return; }
        snd_command *p=realloc(c->pending,cap*sizeof *p);
        if(!p) { c->failed=true; return; } c->pending=p; c->pending_cap=cap;
    }
    c->pending[c->pending_count++]=cmd;
    if(c->journal&&!c->replaying) {
        /* No pointers in commands. PCM packets are also journalled for streams. */
        if(!jrnl_write(c->journal,(jrnl_packet){JRNL_AUDIO,(uint32_t)(cmd.sample/800),sizeof cmd},&cmd)) c->failed=true;
    }
}
static int command_compare(const void *a,const void *b) {
    const snd_command *x=a,*y=b;
    if(x->sample!=y->sample) return x->sample<y->sample?-1:1;
    return x->order<y->order?-1:(x->order>y->order?1:0);
}
sndmin_ctx *sndmin_init(const sndmin_desc *desc) {
    if(!desc || (unsigned)desc->layout>SNDMIN_71 ||
        (desc->rate && (desc->rate<8000||desc->rate>192000))||desc->argc<0||(desc->argc&&!desc->argv)) return NULL;
    sndmin_ctx *c=calloc(1,sizeof *c);
    if(!c) return NULL;
    c->desc=*desc; c->channels=desc->layout==SNDMIN_STEREO?2:(desc->layout==SNDMIN_51?6:8);
    if(!c->desc.rate) c->desc.rate=48000;
    if(!c->desc.latency_frames) c->desc.latency_frames=3;
    c->journal=desc->journal;
    for(int i=1;i<desc->argc;++i) {
        if(strcmp(desc->argv[i],"--offline")==0) c->desc.offline=true;
        if(strcmp(desc->argv[i],"--record")==0 && i+1<desc->argc) {
            if(c->journal) { sndmin_shutdown(c); return NULL; }
            c->journal=jrnl_open(desc->argv[++i],true); c->own_journal=true;
            if(!c->journal) { sndmin_shutdown(c); return NULL; }
        }
    }
    atomic_init(&c->command_read,0); atomic_init(&c->command_write,0);
    atomic_init(&c->snapshot_read,0); atomic_init(&c->snapshot_write,0);
    c->stats.cpu_percent=-1; c->latest.stats.cpu_percent=-1;
    for(unsigned b=0;b<4;++b) {
        c->buses[b].gain=1;
        for(unsigned k=0;k<4;++k) c->buses[b].length[k]=1301+k*631;
        c->buses[b].feedback=0.7f; c->buses[b].wet=0.15f;
    }
    if(c->journal) {
        const uint32_t info[7]={1,SNDMIN_RATE,SNDMIN_FRAME_SAMPLES,c->channels,
            sizeof(snd_command),sizeof(sndmin_patch_desc),0x01020304u};
        if(!jrnl_write(c->journal,(jrnl_packet){JRNL_AUDIO_INFO,0,sizeof info},info)) { sndmin_shutdown(c); return NULL; }
    }
    return c;
}
void sndmin_shutdown(sndmin_ctx *c) {
    if(!c) return;
    if(c->device) { sndmin_device_stop(c->device); sndmin_device_close(c->device); }
    for(uint32_t i=0;i<c->sound_count;++i) free(c->sounds[i].pcm);
    for(uint32_t i=0;i<c->song_count;++i) free(c->songs[i]);
    for(uint32_t i=0;i<c->stream_count;++i) sndmin_reader_close(c->streams[i].reader);
    if(c->own_journal&&c->journal) fclose(c->journal);
    free(c->geometry); free(c->pending); free(c);
}
sndmin_sound sndmin_make_sound(sndmin_ctx *c,sndmin_bytes bytes,uint32_t channels,uint32_t rate) {
    if(!c||c->failed||c->started||c->sound_count==SND_RES||!bytes.data||(channels!=1&&channels!=2)||
        rate<8000||rate>192000||bytes.size%(channels*sizeof(float))) return (sndmin_sound){0};
    const size_t input_frames=bytes.size/(channels*sizeof(float));
    if(!input_frames||input_frames>UINT32_MAX/6) return (sndmin_sound){0};
    const uint32_t frames=(uint32_t)((uint64_t)input_frames*48000/rate);
    float *pcm=calloc((size_t)frames*channels,sizeof(float));
    if(!pcm) return (sndmin_sound){0};
    const float *in=bytes.data;
    for(size_t i=0;i<input_frames*channels;++i) if(!valid(in[i])) { free(pcm); return (sndmin_sound){0}; }
    for(uint32_t i=0;i<frames;++i) {
        const uint64_t pos=(uint64_t)i*rate;
        const size_t at=(size_t)(pos/48000); const float t=(float)(pos%48000)/48000;
        for(uint32_t ch=0;ch<channels;++ch) {
            const size_t a=at>0?at-1:0,b=at<input_frames?at:input_frames-1;
            const size_t d=at+2<input_frames?at+2:input_frames-1,e=at+1<input_frames?at+1:input_frames-1;
            pcm[(size_t)i*channels+ch]=snd_cubic(in[a*channels+ch],in[b*channels+ch],in[e*channels+ch],in[d*channels+ch],t);
        }
    }
    c->sounds[c->sound_count++]=(snd_sound){pcm,frames,channels};
    if(c->journal) {
        const uint32_t meta[3]={c->sound_count,frames,channels};
        if(!jrnl_write(c->journal,(jrnl_packet){0x200,0,sizeof meta},meta)||
            !jrnl_write(c->journal,(jrnl_packet){0x201,0,frames*channels*4},pcm)) c->failed=true;
    }
    return (sndmin_sound){c->failed?0:c->sound_count};
}
sndmin_sound sndmin_load(sndmin_ctx *c,const char *path) {
    if(!c||c->failed||c->started||!path) return (sndmin_sound){0};
    uint64_t frames=0; uint32_t channels=0,rate=0;
    float *pcm=sndmin_decode(path,&frames,&channels,&rate);
    sndmin_sound sound={0};
    if(pcm&&frames<=SIZE_MAX/(sizeof(float)*(channels?channels:1)))
        sound=sndmin_make_sound(c,(sndmin_bytes){pcm,(size_t)frames*channels*sizeof(float)},channels,rate);
    free(pcm); return sound;
}
sndmin_stream sndmin_open_stream(sndmin_ctx *c,const char *path) {
    if(!c||c->failed||c->started||c->stream_count==SND_STREAMS||!path) return (sndmin_stream){0};
    snd_stream_game s={0}; s.reader=sndmin_reader_open(path,&s.channels,&s.rate);
    if(!s.reader) return (sndmin_stream){0};
    if(s.rate<8000||s.rate>192000) { sndmin_reader_close(s.reader); return (sndmin_stream){0}; }
    c->streams[c->stream_count++]=s;
    c->stream_mix[c->stream_count-1].channels=s.channels;
    c->stream_mix[c->stream_count-1].rate=s.rate;
    if(c->journal) {
        const uint32_t meta[3]={c->stream_count,s.channels,s.rate};
        if(!jrnl_write(c->journal,(jrnl_packet){0x203,0,sizeof meta},meta)) c->failed=true;
    }
    return (sndmin_stream){c->failed?0:c->stream_count};
}
sndmin_patch sndmin_make_patch(sndmin_ctx *c,const sndmin_patch_desc *desc) {
    if(!c||c->failed||c->started||!desc||c->patch_count==SND_RES||desc->unison>4||
        (unsigned)desc->instrument>SNDMIN_GATED_SNARE||(unsigned)desc->wave[0]>SNDMIN_NOISE||
        (unsigned)desc->wave[1]>SNDMIN_NOISE||(unsigned)desc->lfo_route>SNDMIN_LFO_CUTOFF) return (sndmin_patch){0};
    sndmin_patch_desc p=*desc;
    const float values[]={p.detune,p.sub,p.pulse_width,p.cutoff,p.resonance,p.filter_env,p.lfo_hz,p.lfo_depth,
        p.unison_cents,p.chorus,p.amp.attack,p.amp.decay,p.amp.sustain,p.amp.release,p.filter.attack,
        p.filter.decay,p.filter.sustain,p.filter.release};
    for(size_t i=0;i<sizeof values/sizeof values[0];++i) if(!valid(values[i])) return (sndmin_patch){0};
    if(p.amp.attack<0||p.amp.decay<0||p.amp.release<0||p.filter.attack<0||p.filter.decay<0||p.filter.release<0) return (sndmin_patch){0};
    if(!p.unison) p.unison=1;
    if(!p.cutoff) p.cutoff=6000;
    if(!p.pulse_width) p.pulse_width=0.5f;
    if(!p.amp.attack&&!p.amp.decay&&!p.amp.sustain&&!p.amp.release) p.amp=(sndmin_adsr){0.005f,0.15f,0.6f,0.2f};
    if(!p.filter.attack&&!p.filter.decay&&!p.filter.sustain&&!p.filter.release) p.filter=(sndmin_adsr){0.005f,0.25f,0.2f,0.15f};
    p.resonance=snd_clamp(p.resonance,0,0.95f); p.sub=snd_clamp(p.sub,0,1);
    p.chorus=snd_clamp(p.chorus,0,1); p.amp.sustain=snd_clamp(p.amp.sustain,0,1);
    p.lfo_hz=snd_clamp(p.lfo_hz,0,100); p.lfo_depth=snd_clamp(p.lfo_depth,0,2);
    c->patches[c->patch_count++]=p;
    if(c->journal&&!jrnl_write(c->journal,(jrnl_packet){0x202,0,sizeof p},&p)) c->failed=true;
    return (sndmin_patch){c->failed?0:c->patch_count};
}
sndmin_voice sndmin_play(sndmin_ctx *c,const sndmin_play_desc *desc) {
    if(!c||c->failed||c->rendered||!desc||!valid_voice(&desc->voice)||!valid(desc->duration)||desc->duration<0||
        (unsigned)desc->bus>SNDMIN_MASTER||desc->note>127||c->next_voice==UINT32_MAX) return (sndmin_voice){0};
    const unsigned sources=(desc->sound.id!=0)+(desc->stream.id!=0)+(desc->patch.id!=0)+(desc->song.id!=0);
    if(sources!=1||desc->sound.id>c->sound_count||desc->stream.id>c->stream_count||
        desc->patch.id>c->patch_count||desc->song.id>c->song_count) return (sndmin_voice){0};
    sndmin_play_desc p=*desc; p.voice=defaults(p.voice); if(!p.note) p.note=69;
    const uint32_t id=++c->next_voice;
    if(p.stream.id) {
        snd_stream_game *s=&c->streams[p.stream.id-1];
        if(s->active) return (sndmin_voice){0};
        s->voice=id; s->loop=p.loop; s->active=true;
    }
    if(p.spatial) {
        unsigned slot=0;
        for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) {
            const snd_game_voice *g=&c->game_voices[i];
            if(!g->id||g->expires<c->game_sample) { slot=i; break; }
            if(g->play.priority<c->game_voices[slot].play.priority||
                (g->play.priority==c->game_voices[slot].play.priority&&g->id<c->game_voices[slot].id)) slot=i;
        }
        uint64_t expires=UINT64_MAX;
        if(p.patch.id) {
            const sndmin_patch_desc patch=c->patches[p.patch.id-1];
            if(patch.instrument!=SNDMIN_MELODIC) expires=c->game_sample+48000;
            else if(p.duration>0) expires=c->game_sample+(uint64_t)((p.duration+patch.amp.release+0.5f)*48000);
        }
        if(p.sound.id&&!p.loop) expires=c->game_sample+(uint64_t)((double)c->sounds[p.sound.id-1].frames*2/(double)p.voice.pitch)+24000;
        c->game_voices[slot]=(snd_game_voice){id,p,expires};
    }
    if(p.song.id) {
        if(c->song_parent_count==SND_RES) return (sndmin_voice){0};
        c->song_gain[c->song_parent_count]=p.voice.gain;
        c->song_parents[c->song_parent_count++]=id;
        snd_command cmd={.sample=c->game_sample,.op=CMD_GROUP_SET,.id=id};
        cmd.u.group.voice=p.voice; cmd.u.group.ratio=1;
        cmd.u.group.acoustic=sndmin_acoustics(&c->frame,&p.voice,c->desc.layout);
        sndmin_submit(c,cmd);
        sndmin_song_schedule(c,&p,id); return (sndmin_voice){c->failed?0:id};
    }
    sndmin_submit(c,(snd_command){.sample=c->game_sample,.op=CMD_PLAY,.id=id,.u.play=p});
    if(p.spatial) sndmin_submit(c,(snd_command){.sample=c->game_sample,.op=CMD_ACOUSTIC,.id=id,
        .u.acoustic=sndmin_acoustics(&c->frame,&p.voice,c->desc.layout)});
    return (sndmin_voice){c->failed?0:id};
}
bool sndmin_ok(const sndmin_ctx *c) { return c&&!c->failed; }
bool sndmin_bus_set(sndmin_ctx *c,sndmin_bus bus,float gain) {
    if(!c||c->failed||!c->started||c->rendered||(unsigned)bus>SNDMIN_MASTER||!valid(gain)||gain<0||gain>4) return false;
    c->bus_override|=1u<<(unsigned)bus;
    c->frame_command.u.frame.gain[bus]=gain;
    sndmin_submit(c,c->frame_command);
    return !c->failed;
}
void sndmin_set(sndmin_ctx *c,sndmin_voice voice,const sndmin_voice_desc *desc) {
    if(!c||!voice.id||!desc||!valid_voice(desc)) return;
    sndmin_voice_desc v=*desc; if(!v.pitch) v.pitch=1;
    for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) if(c->game_voices[i].id==voice.id) {
        c->game_voices[i].play.voice=v;
        /* A retune may extend PCM lifetime; retain until stopped/evicted. */
        if(c->game_voices[i].play.sound.id) c->game_voices[i].expires=UINT64_MAX;
    }
    for(uint32_t i=0;i<c->song_parent_count;++i) if(c->song_parents[i]==voice.id) {
        const float ratio=v.gain/c->song_gain[i];
        if(!valid(ratio)) { c->failed=true; return; }
        snd_command cmd={.sample=c->game_sample,.op=CMD_GROUP_SET,.id=voice.id};
        cmd.u.group.voice=v; cmd.u.group.ratio=ratio;
        cmd.u.group.acoustic=sndmin_acoustics(&c->frame,&v,c->desc.layout);
        sndmin_submit(c,cmd); return;
    }
    sndmin_submit(c,(snd_command){.sample=c->game_sample,.op=CMD_SET,.id=voice.id,.u.set=v});
}
void sndmin_stop(sndmin_ctx *c,sndmin_voice v,float fade) {
    if(!c||!v.id||!valid(fade)||fade<0) return;
    uint32_t group=0;
    for(uint32_t i=0;i<c->song_parent_count;++i) if(c->song_parents[i]==v.id) group=1;
    sndmin_submit(c,(snd_command){.sample=c->game_sample,.op=CMD_STOP,.id=v.id,.count=group,.u.fade=fade});
    for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) if(c->game_voices[i].id==v.id) c->game_voices[i].id=0;
}
sndmin_stats sndmin_stats_get(sndmin_ctx *c) {
    if(!c) return (sndmin_stats){.cpu_percent=-1};
    uint32_t r=atomic_load_explicit(&c->snapshot_read,memory_order_relaxed);
    const uint32_t w=atomic_load_explicit(&c->snapshot_write,memory_order_acquire);
    while(r!=w) c->latest=c->snapshots[r++%SND_SNAP];
    atomic_store_explicit(&c->snapshot_read,r,memory_order_release);
    c->latest.stats.dropped_commands=c->failed?1:0; return c->latest.stats;
}
void sndmin_pump_streams(sndmin_ctx *c,uint64_t when) {
    (void)sndmin_stats_get(c);
    for(uint32_t i=0;i<c->stream_count;++i) {
        snd_stream_game *s=&c->streams[i];
        if(!s->active||s->eof||!s->reader) continue;
        const uint64_t consumed=c->latest.stream_read[i];
        while(s->written-consumed+SND_BLOCK<=SND_STREAM_BUFFER-8) {
            snd_command cmd={.sample=when,.op=CMD_PCM,.id=i+1};
            uint32_t n=sndmin_reader_read(s->reader,cmd.u.pcm,SND_BLOCK);
            if(n==0&&s->loop&&sndmin_reader_rewind(s->reader)) n=sndmin_reader_read(s->reader,cmd.u.pcm,SND_BLOCK);
            cmd.count=n;
            if(!n) s->eof=true;
            /* Inline samples cross in commands; decoder and FILE never cross. */
            if(c->desc.offline) {
                if(!ring_push(c,&cmd)) { c->failed=true; return; }
                if(c->journal&&!jrnl_write(c->journal,(jrnl_packet){JRNL_AUDIO,(uint32_t)(when/800),sizeof cmd},&cmd)) c->failed=true;
            } else sndmin_submit(c,cmd);
            s->written+=n; if(!n) break;
        }
    }
}
void sndmin_frame(sndmin_ctx *c,const sndmin_frame_desc *f) {
    if(!c||!f||c->rendered||f->box_count>65536||f->material_count>256||
        (f->box_count&&!f->boxes)||(f->material_count&&!f->materials)) { if(c)c->failed=true; return; }
    const float inputs[]={f->listener.x,f->listener.y,f->listener.z,f->velocity.x,f->velocity.y,f->velocity.z,
        f->forward.x,f->forward.y,f->forward.z,f->up.x,f->up.y,f->up.z,f->delay_seconds,f->delay_feedback,
        f->bus_gain[0],f->bus_gain[1],f->bus_gain[2],f->bus_gain[3]};
    for(size_t i=0;i<sizeof inputs/sizeof inputs[0];++i) if(!valid(inputs[i])) { c->failed=true; return; }
    for(uint32_t i=0;i<f->box_count;++i) {
        const sndmin_box b=f->boxes[i];
        if(!valid(b.min.x)||!valid(b.min.y)||!valid(b.min.z)||!valid(b.max.x)||!valid(b.max.y)||!valid(b.max.z)||
            b.min.x>b.max.x||b.min.y>b.max.y||b.min.z>b.max.z) { c->failed=true; return; }
    }
    for(uint32_t i=0;i<f->material_count;++i) if(!valid(f->materials[i].absorption)||!valid(f->materials[i].transmission)) { c->failed=true; return; }
    if(c->started&&f->index<=c->frame.index) { c->failed=true; return; }
    if(!c->desc.offline&&c->pending_count) {
        qsort(c->pending,c->pending_count,sizeof *c->pending,command_compare);
        size_t n=0;
        const uint64_t limit=(uint64_t)f->index*800;
        while(n<c->pending_count&&c->pending[n].sample<limit) {
            snd_command cmd=c->pending[n++]; cmd.sample+=(uint64_t)c->desc.latency_frames*800;
            if(!ring_push(c,&cmd)) c->failed=true;
        }
        memmove(c->pending,c->pending+n,(c->pending_count-n)*sizeof *c->pending); c->pending_count-=n;
    }
    if(f->box_count>c->geometry_cap) {
        sndmin_box *geometry=realloc(c->geometry,(size_t)f->box_count*sizeof *geometry);
        if(!geometry) { c->failed=true; return; } c->geometry=geometry; c->geometry_cap=f->box_count;
    }
    if(f->box_count) memcpy(c->geometry,f->boxes,(size_t)f->box_count*sizeof *f->boxes);
    if(f->material_count) memcpy(c->materials,f->materials,(size_t)f->material_count*sizeof *f->materials);
    c->frame=*f; c->frame.boxes=c->geometry; c->frame.materials=c->materials;
    c->game_sample=(uint64_t)f->index*800;
    snd_command cmd={.sample=c->game_sample,.op=CMD_FRAME};
    for(unsigned i=0;i<4;++i) cmd.u.frame.gain[i]=(c->bus_override&(1u<<i)) ?
        c->frame_command.u.frame.gain[i] : (f->bus_gain[i]==0?1:snd_clamp(f->bus_gain[i],0,4));
    cmd.u.frame.delay=snd_clamp(f->delay_seconds,0,0.68f); cmd.u.frame.feedback=snd_clamp(f->delay_feedback,0,0.95f);
    if(f->index%6==0) {
        cmd.u.frame.room=sndmin_acoustics(f,&(sndmin_voice_desc){.position=f->listener},c->desc.layout);
        for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) {
            const snd_game_voice *g=&c->game_voices[i];
            if(g->id&&g->expires>=c->game_sample) sndmin_submit(c,(snd_command){.sample=c->game_sample,.op=CMD_ACOUSTIC,.id=g->id,.count=g->play.song.id!=0,
                .u.acoustic=sndmin_acoustics(f,&g->play.voice,c->desc.layout)});
        }
    }
    c->frame_command=cmd;
    sndmin_submit(c,cmd);
    if(!c->desc.offline) sndmin_pump_streams(c,c->game_sample);
    if(!c->started) {
        c->started=true;
        if(!c->desc.offline) {
            c->device=sndmin_device_open(c,c->channels,c->desc.rate);
            if(!c->device||!sndmin_device_start(c->device)) c->failed=true;
        }
    }
}
void sndmin_dump(sndmin_ctx *c,FILE *f) {
    if(!c||!f) return;
    const sndmin_stats s=sndmin_stats_get(c);
    fprintf(f,"sndmin: samples=%llu voices=%u stolen=%u underruns=%u late=%u dropped=%u cpu=%.2f%% mfp=%.2f openness=%.3f decay=%.2f\n",
        (unsigned long long)s.samples,s.voices,s.stolen,s.underruns,s.late_commands,s.dropped_commands,
        (double)s.cpu_percent,(double)s.mean_free_path,(double)s.openness,(double)s.decay);
}
bool sndmin_feed(sndmin_ctx *c,uint64_t until,size_t *index) {
    while(*index<c->pending_count&&c->pending[*index].sample<=until) {
        if(!ring_push(c,&c->pending[*index])) return false;
        ++*index;
    } return true;
}
bool sndmin_render(sndmin_ctx *c,uint32_t frames,const char *wav,const char *png) {
    if(!c||!c->desc.offline||c->rendered||c->failed||!wav||!frames) return false;
    c->started=true; c->rendered=true;
    /* Offline is always native 48 kHz; device rate is irrelevant to a golden. */
    c->desc.rate=48000;
    qsort(c->pending,c->pending_count,sizeof *c->pending,command_compare);
    if(!sndmin_write_output(c,frames,wav,png)) c->failed=true;
    return !c->failed;
}
/* All functions below this boundary are mixer-only, allocation/IO-free. */
static void release(snd_mix_voice *v) {
    if(!v->released) { v->released=true; v->release_at=v->age; v->release_level=v->env; }
}
static float envelope(sndmin_adsr e,uint64_t age) {
    const float t=(float)age/48000;
    if(e.attack>0&&t<e.attack) return t/e.attack;
    if(e.decay>0&&t<e.attack+e.decay) return 1-(1-e.sustain)*(t-e.attack)/e.decay;
    return e.sustain;
}
static float blep(float t,float dt) {
    if(t<dt) { const float x=t/dt; return x+x-x*x-1; }
    if(t>1-dt) { const float x=(t-1)/dt; return x*x+x+x+1; } return 0;
}
static float oscillator(float t,float dt,sndmin_wave wave,float width,float noise) {
    if(wave==SNDMIN_NOISE) return noise;
    if(wave==SNDMIN_TRIANGLE) return 1-4*snd_abs(t-0.5f);
    if(wave==SNDMIN_PULSE) {
        float shifted=t-width; if(shifted<0) shifted+=1;
        return (t<width?1.0f:-1.0f)+blep(t,dt)-blep(shifted,dt);
    }
    return 2*t-1-blep(t,dt);
}
static float synth(snd_mix_voice *v,const sndmin_patch_desc *p,uint64_t sample) {
    if(v->play.duration>0&&(float)v->age>=v->play.duration*48000) release(v);
    v->env=envelope(p->amp,v->age);
    if(v->released) {
        const float t=(float)(v->age-v->release_at)/48000;
        v->env=p->amp.release>0?v->release_level*snd_clamp(1-t/p->amp.release,0,1):0;
        if(v->env==0) { v->id=0; return 0; }
    }
    const float age=(float)v->age/48000;
    const float lfo=snd_sin(age*p->lfo_hz)*p->lfo_depth;
    float frequency=440*snd_exp2(((float)v->play.note-69)/12)*v->play.voice.pitch*v->acoustic.doppler;
    if(p->lfo_route==SNDMIN_LFO_PITCH) frequency*=snd_exp2(lfo/12);
    const float noise=snd_noise(sample,v->id);
    float out=0;
    if(p->instrument!=SNDMIN_MELODIC) {
        const float decay=p->instrument==SNDMIN_HAT?0.045f:(p->instrument==SNDMIN_KICK?0.4f:0.22f);
        const float amp=snd_exp2(-age*10/decay);
        float hz=frequency;
        if(p->instrument==SNDMIN_KICK) hz=45+140*snd_exp2(-age*55);
        if(p->instrument==SNDMIN_SNARE||p->instrument==SNDMIN_GATED_SNARE) hz=180;
        v->phase[0][0]+=hz/48000; v->phase[0][0]-=(float)(int)v->phase[0][0];
        const float tone=snd_sin(v->phase[0][0]);
        v->noise_filter+=0.18f*(noise-v->noise_filter);
        if(p->instrument==SNDMIN_HAT) out=(noise-v->noise_filter)*amp;
        else if(p->instrument==SNDMIN_SNARE||p->instrument==SNDMIN_GATED_SNARE) {
            out=((noise-v->noise_filter)*0.8f+tone*0.3f)*amp;
            if(p->instrument==SNDMIN_GATED_SNARE&&age<0.16f) out+=noise*0.12f*(1-age/0.16f);
        } else out=tone*amp;
        if(age>decay*2) v->id=0;
    } else {
        const float width=snd_clamp(p->pulse_width+(p->lfo_route==SNDMIN_LFO_WIDTH?lfo*0.4f:0),0.05f,0.95f);
        for(uint32_t u=0;u<p->unison;++u) {
            const float spread=p->unison>1?((float)u/(float)(p->unison-1)-0.5f)*p->unison_cents:0;
            for(unsigned osc=0;osc<3;++osc) {
                const float hz=frequency*snd_exp2((spread+(osc==1?p->detune:0))/1200)*(osc==2?0.5f:1);
                const float step=snd_clamp(hz/48000,0.000001f,0.45f);
                float *phase=&v->phase[u][osc]; *phase+=step; if(*phase>=1) *phase-=1;
                out+=oscillator(*phase,step,osc==2?SNDMIN_TRIANGLE:p->wave[osc],width,noise)*(osc==2?p->sub:0.5f);
            }
        }
        out/=(float)p->unison;
        v->filter_env=envelope(p->filter,v->age);
        if(v->released) v->filter_env*=snd_clamp(1-(float)(v->age-v->release_at)/(48000*(p->filter.release+0.00001f)),0,1);
        const float cutoff=snd_clamp(p->cutoff*snd_exp2(p->filter_env*v->filter_env+(p->lfo_route==SNDMIN_LFO_CUTOFF?lfo*3:0)),20,16000);
        const float coefficient=cutoff/(cutoff+7600);
        float x=snd_soft(out-3.8f*p->resonance*v->ladder[3]);
        for(unsigned k=0;k<4;++k) { v->ladder[k]=snd_zap(v->ladder[k]+coefficient*(x-v->ladder[k])); x=v->ladder[k]; }
        out=x*v->env;
    }
    ++v->age;
    return out;
}
static void set_voice(snd_mix_voice *v,sndmin_voice_desc settings) {
    v->play.voice=settings; v->target_gain=settings.gain;
    v->gain_left=(uint32_t)(snd_clamp(settings.fade_seconds,0,60)*48000);
    if(v->gain_left) v->gain_step=(v->target_gain-v->gain)/(float)v->gain_left;
    else v->gain=v->target_gain;
}
static void apply(sndmin_ctx *c,const snd_command *cmd) {
    if(cmd->sample<c->sample) ++c->stats.late_commands;
    if(cmd->op==CMD_PCM) {
        if(!cmd->id||cmd->id>c->stream_count) return;
        snd_stream_mix *s=&c->stream_mix[cmd->id-1];
        if(cmd->count==0) s->eof=true;
        if(cmd->count>SND_BLOCK||s->written-s->read+cmd->count>SND_STREAM_BUFFER) { ++c->stats.underruns; return; }
        for(uint32_t i=0;i<cmd->count;++i) for(uint32_t ch=0;ch<s->channels;++ch)
            s->pcm[((s->written+i)%SND_STREAM_BUFFER)*2+ch]=cmd->u.pcm[i*s->channels+ch];
        s->written+=cmd->count; return;
    }
    if(cmd->op==CMD_FRAME) {
        for(unsigned b=0;b<4;++b) {
            snd_bus *bus=&c->buses[b]; bus->gain=cmd->u.frame.gain[b];
            if(cmd->u.frame.room.decay>0) {
                const sndmin_acoustic *a=&cmd->u.frame.room;
                for(unsigned k=0;k<4;++k) bus->length[k]=(uint32_t)snd_clamp(701+(float)k*631+a->mean_free_path*41,701,SND_FDN-1);
                bus->feedback=snd_exp2(-3*(float)bus->length[0]/(48000*a->decay));
                bus->wet=0.35f*(1-a->openness*0.85f);
                c->stats.mean_free_path=a->mean_free_path; c->stats.openness=a->openness; c->stats.decay=a->decay;
            }
        }
        c->delay_length=(uint32_t)(cmd->u.frame.delay*48000); c->delay_feedback=cmd->u.frame.feedback; return;
    }
    if(cmd->op==CMD_GROUP_SET) {
        uint32_t slot=0;
        while(slot<c->group_update_count&&c->group_updates[slot].id!=cmd->id) ++slot;
        if(slot==SND_RES) return;
        if(slot==c->group_update_count) ++c->group_update_count;
        c->group_updates[slot]=*cmd;
        for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) if(c->voices[i].id&&c->voices[i].group==cmd->id) {
            sndmin_voice_desc settings=cmd->u.group.voice;
            settings.gain=snd_clamp(c->voices[i].source_gain*cmd->u.group.ratio,0,16);
            set_voice(&c->voices[i],settings);
            if(c->voices[i].play.spatial) { c->voices[i].target=cmd->u.group.acoustic; c->voices[i].acoustic_left=4800; }
        }
        return;
    }
    if(cmd->op==CMD_ACOUSTIC&&cmd->count) {
        for(uint32_t i=0;i<c->group_update_count;++i) if(c->group_updates[i].id==cmd->id) c->group_updates[i].u.group.acoustic=cmd->u.acoustic;
        for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) if(c->voices[i].id&&c->voices[i].group==cmd->id) {
            c->voices[i].target=cmd->u.acoustic; c->voices[i].acoustic_left=4800;
        }
        return;
    }
    snd_mix_voice *v=NULL;
    for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) if(c->voices[i].id==cmd->id) { v=&c->voices[i]; break; }
    if(cmd->op==CMD_PLAY) {
        if(cmd->count) for(uint32_t i=0;i<c->stopped_count;++i) if(c->stopped_groups[i]==cmd->count) return;
        unsigned slot=0;
        for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) {
            if(!c->voices[i].id) { slot=i; break; }
            if(c->voices[i].play.priority<c->voices[slot].play.priority||
                (c->voices[i].play.priority==c->voices[slot].play.priority&&c->voices[i].peak<c->voices[slot].peak)) slot=i;
        }
        v=&c->voices[slot]; if(v->id) ++c->stats.stolen;
        memset(v,0,sizeof *v); v->id=cmd->id; v->group=cmd->count; v->play=cmd->u.play;
        v->gain=v->target_gain=v->play.voice.gain;
        v->source_gain=v->play.voice.gain;
        v->acoustic.gain=1; v->acoustic.lowpass=1; v->acoustic.doppler=1;
        v->acoustic.pan[0]=v->acoustic.pan[1]=0.70710677f;
        v->target=v->acoustic;
        if(v->group) for(uint32_t i=0;i<c->group_update_count;++i) if(c->group_updates[i].id==v->group) {
            sndmin_voice_desc settings=c->group_updates[i].u.group.voice;
            settings.gain=snd_clamp(v->source_gain*c->group_updates[i].u.group.ratio,0,16);
            settings.fade_seconds=0; set_voice(v,settings);
            if(v->play.spatial) v->acoustic=v->target=c->group_updates[i].u.group.acoustic;
        }
        return;
    }
    if(cmd->op==CMD_STOP) {
        bool group=cmd->count!=0;
        for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) if(c->voices[i].id&&c->voices[i].group==cmd->id) {
            if(cmd->u.fade>0) {
                sndmin_voice_desc settings=c->voices[i].play.voice; settings.gain=0; settings.fade_seconds=cmd->u.fade;
                set_voice(&c->voices[i],settings); c->voices[i].stopping=true; c->voices[i].stop_left=c->voices[i].gain_left;
                if(!c->voices[i].stop_left) c->voices[i].id=0;
            } else release(&c->voices[i]);
            group=true;
        }
        bool known=false;
        for(uint32_t i=0;i<c->stopped_count;++i) if(c->stopped_groups[i]==cmd->id) known=true;
        if(group&&!known&&c->stopped_count<128) c->stopped_groups[c->stopped_count++]=cmd->id;
    }
    if(!v) return; /* expired/stolen generation is harmless */
    if(cmd->op==CMD_SET) {
        set_voice(v,cmd->u.set);
    }
    if(cmd->op==CMD_STOP) {
        if(v->play.patch.id&&cmd->u.fade==0) release(v);
        else { v->stopping=true; v->stop_left=(uint32_t)(snd_clamp(cmd->u.fade,0,60)*48000);
            if(!v->stop_left) v->id=0; else { v->gain_left=v->stop_left; v->gain_step=-v->gain/(float)v->stop_left; v->target_gain=0; } }
    }
    if(cmd->op==CMD_ACOUSTIC) {
        v->target=cmd->u.acoustic; v->acoustic_left=4800;
        if(v->age==0&&v->cursor==0) { v->acoustic=v->target; v->acoustic_left=0; }
    }
}
static float pcm_sample(const snd_sound *s,int64_t index,unsigned ch,bool loop) {
    if(loop) { if(index<0||index>=s->frames) { index%=(int64_t)s->frames; if(index<0) index+=s->frames; } }
    else { if(index<0) index=0; if(index>=s->frames) index=s->frames-1; }
    return s->pcm[(size_t)index*s->channels+(ch<s->channels?ch:0)];
}
static void mix_one(sndmin_ctx *c,float *out) {
    uint32_t r=atomic_load_explicit(&c->command_read,memory_order_relaxed);
    const uint32_t w=atomic_load_explicit(&c->command_write,memory_order_acquire);
    while(r!=w&&c->commands[r%SND_COMMAND_CAP].sample<=c->sample) apply(c,&c->commands[r++%SND_COMMAND_CAP]);
    atomic_store_explicit(&c->command_read,r,memory_order_release);
    float bus_samples[4][8]={{0}},sends[4]={0},lfe=0;
    for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) {
        snd_mix_voice *v=&c->voices[i]; if(!v->id) continue;
        if(v->gain_left) { v->gain+=v->gain_step; if(--v->gain_left==0) v->gain=v->target_gain; }
        if(v->stopping&&v->stop_left&&--v->stop_left==0) { v->id=0; continue; }
        if(v->acoustic_left) {
            const float t=1.0f/(float)v->acoustic_left--;
            v->acoustic.gain+=(v->target.gain-v->acoustic.gain)*t;
            v->acoustic.lowpass+=(v->target.lowpass-v->acoustic.lowpass)*t;
            v->acoustic.doppler+=(v->target.doppler-v->acoustic.doppler)*t;
            for(unsigned ch=0;ch<c->channels;++ch) v->acoustic.pan[ch]+=(v->target.pan[ch]-v->acoustic.pan[ch])*t;
            for(unsigned k=0;k<4;++k) {
                v->acoustic.taps[k].gain+=(v->target.taps[k].gain-v->acoustic.taps[k].gain)*t;
                v->acoustic.taps[k].delay+=(v->target.taps[k].delay-v->acoustic.taps[k].delay)*t;
                v->acoustic.taps[k].lowpass+=(v->target.taps[k].lowpass-v->acoustic.taps[k].lowpass)*t;
            }
        }
        float left=0,right=0;
        bool stereo=false;
        if(v->play.patch.id) left=right=synth(v,&c->patches[v->play.patch.id-1],c->sample);
        if(v->play.sound.id) {
            const snd_sound *s=&c->sounds[v->play.sound.id-1];
            if(v->cursor>=s->frames&&!v->play.loop) { v->id=0; continue; }
            const int64_t at=(int64_t)v->cursor; const float t=(float)(v->cursor-(double)at);
            float values[2]={0};
            for(unsigned ch=0;ch<2;++ch) {
                if(ch==1&&s->channels==1) { values[1]=values[0]; break; }
                if(t==0&&!c->reference_pcm) values[ch]=s->pcm[(size_t)at*s->channels+ch];
                else values[ch]=snd_cubic(pcm_sample(s,at-1,ch,v->play.loop),pcm_sample(s,at,ch,v->play.loop),
                    pcm_sample(s,at+1,ch,v->play.loop),pcm_sample(s,at+2,ch,v->play.loop),t);
            }
            left=values[0]; right=values[1]; stereo=s->channels==2;
            v->cursor+=(double)(v->play.voice.pitch*v->acoustic.doppler);
            if(v->play.loop&&v->cursor>=s->frames) v->cursor-=(double)s->frames*(double)(uint64_t)(v->cursor/s->frames);
        }
        if(v->play.stream.id) {
            snd_stream_mix *s=&c->stream_mix[v->play.stream.id-1];
            const uint64_t at=(uint64_t)s->cursor;
            if(at+2>=s->written&&!s->eof) { ++c->stats.underruns; continue; }
            if(at>=s->written&&s->eof) { v->id=0; continue; }
            const float t=(float)(s->cursor-(double)at); float values[2];
            for(unsigned ch=0;ch<2;++ch) {
                float taps[4]; for(unsigned k=0;k<4;++k) {
                    uint64_t n=at+k; n=n>0?n-1:0; if(n>=s->written)n=s->written-1;
                    taps[k]=s->pcm[(n%SND_STREAM_BUFFER)*2+(ch<s->channels?ch:0)];
                } values[ch]=snd_cubic(taps[0],taps[1],taps[2],taps[3],t);
            }
            left=values[0]; right=values[1]; stereo=s->channels==2;
            s->cursor+=(double)(v->play.voice.pitch*v->acoustic.doppler)*(double)s->rate/48000;
            const uint64_t used=(uint64_t)s->cursor; s->read=used>1?used-1:0;
            if(s->read>s->written) s->read=s->written;
        }
        float mono=(left+right)*0.5f;
        if(v->play.spatial) {
            v->filter=snd_zap(v->filter+v->acoustic.lowpass*(mono-v->filter)); mono=v->filter;
        }
        float wet=0;
        if(v->play.spatial||v->play.patch.id) {
        v->echo[v->echo_head]=mono;
        for(unsigned k=0;k<4;++k) if(v->acoustic.taps[k].gain>0) {
            const sndmin_tap tap=v->acoustic.taps[k];
            const uint32_t delay=(uint32_t)(tap.delay*48000);
            const float x=v->echo[(v->echo_head+SND_ECHO-delay%SND_ECHO)%SND_ECHO];
            v->tap_filter[k]=snd_zap(v->tap_filter[k]+tap.lowpass*(x-v->tap_filter[k])); wet+=v->tap_filter[k]*tap.gain;
        }
        if(v->play.patch.id&&c->patches[v->play.patch.id-1].chorus>0) {
            const float depth=c->patches[v->play.patch.id-1].chorus;
            v->chorus_phase+=0.3f/48000; if(v->chorus_phase>=1) v->chorus_phase-=1;
            const uint32_t delay=(uint32_t)(500+150*snd_sin(v->chorus_phase));
            wet+=v->echo[(v->echo_head+SND_ECHO-delay)%SND_ECHO]*depth*0.5f;
        }
        v->echo_head=(v->echo_head+1)%SND_ECHO;
        }
        const float gain=v->gain*(v->play.spatial?v->acoustic.gain:1);
        mono=(mono+wet)*gain; const unsigned bus=(unsigned)v->play.bus;
        v->peak=snd_abs(mono);
        if(stereo&&!v->play.spatial) { bus_samples[bus][0]+=(left+wet)*gain; bus_samples[bus][1]+=(right+wet)*gain; }
        else for(unsigned ch=0;ch<c->channels;++ch) bus_samples[bus][ch]+=mono*v->acoustic.pan[ch];
        sends[bus]+=mono*snd_clamp(v->play.voice.reverb_send,0,1);
        lfe+=mono*snd_clamp(v->play.voice.lfe_send,0,1)*c->buses[bus].gain;
    }
    for(unsigned ch=0;ch<c->channels;++ch) out[ch]=0;
    for(unsigned b=0;b<4;++b) {
        snd_bus *bus=&c->buses[b]; float delayed[4],sum=0;
        for(unsigned k=0;k<4;++k) { delayed[k]=bus->lines[k][(bus->head+SND_FDN-bus->length[k])%SND_FDN]; sum+=delayed[k]; }
        for(unsigned k=0;k<4;++k) {
            const float x=(0.5f*sum-delayed[k])*bus->feedback;
            bus->damping[k]=snd_zap(bus->damping[k]+0.65f*(x-bus->damping[k]));
            bus->lines[k][bus->head]=snd_zap(sends[b]+bus->damping[k]);
        }
        float delay=0;
        if(b==SNDMIN_MUSIC&&c->delay_length) {
            delay=bus->delay[(bus->head+SND_FDN-c->delay_length)%SND_FDN];
            bus->delay[bus->head]=snd_zap((bus_samples[b][0]+bus_samples[b][1])*0.5f+delay*c->delay_feedback);
        }
        for(unsigned ch=0;ch<c->channels;++ch) if(ch!=3) {
            const float reverb=(delayed[ch%4]+delayed[(ch+1)%4])*0.5f*bus->wet;
            const float bus_gain=b==SNDMIN_MASTER?1:bus->gain;
            out[ch]+=(bus_samples[b][ch]+reverb+delay*0.25f)*bus_gain;
        }
        bus->head=(bus->head+1)%SND_FDN;
    }
    c->lfe=snd_zap(c->lfe+0.015465f*(lfe-c->lfe)); if(c->channels>2) out[3]=c->lfe;
    for(unsigned ch=0;ch<c->channels;++ch) out[ch]=snd_clamp(out[ch]*c->buses[SNDMIN_MASTER].gain,-1,1);
    ++c->sample;
}
void sndmin_mix(sndmin_ctx *c,float *out,uint32_t frames) {
    callback_active=true;
    if(c->desc.rate==48000) for(uint32_t i=0;i<frames;++i) mix_one(c,out+(size_t)i*c->channels);
    else {
        if(!c->output_primed) { mix_one(c,c->resample_a); mix_one(c,c->resample_b); c->output_primed=true; }
        for(uint32_t i=0;i<frames;++i) {
            const float t=(float)c->output_phase/(float)c->desc.rate;
            for(unsigned ch=0;ch<c->channels;++ch) out[(size_t)i*c->channels+ch]=c->resample_a[ch]+t*(c->resample_b[ch]-c->resample_a[ch]);
            c->output_phase+=48000;
            while(c->output_phase>=c->desc.rate) { c->output_phase-=c->desc.rate;
                memcpy(c->resample_a,c->resample_b,sizeof c->resample_a); mix_one(c,c->resample_b); }
        }
    }
    c->stats.samples=c->sample; c->stats.voices=0;
    for(unsigned i=0;i<SNDMIN_MAX_VOICES;++i) c->stats.voices+=c->voices[i].id!=0;
    const uint32_t w=atomic_load_explicit(&c->snapshot_write,memory_order_relaxed);
    const uint32_t r=atomic_load_explicit(&c->snapshot_read,memory_order_acquire);
    if(w-r<SND_SNAP) {
        snd_snapshot *s=&c->snapshots[w%SND_SNAP]; s->stats=c->stats;
        for(unsigned i=0;i<SND_STREAMS;++i) s->stream_read[i]=c->stream_mix[i].read;
        atomic_store_explicit(&c->snapshot_write,w+1,memory_order_release);
    }
    callback_active=false;
}
