/* "Blue Circuit / Transit": original 20-second F-minor electronic score.
 * 150 BPM pulse / 75 BPM backbeat; native sndmin patches and original notes.
 * Inspired by measured bass/pulse features of isn.mp4, not a transcription. */
#ifndef OMEGA_SCORE_H
#define OMEGA_SCORE_H
#include "sndmin.h"
typedef struct {
    sndmin_patch pad,bass,arp,kick,snare,hat,tom;
    sndmin_voice voices[32];
    uint32_t next;
} omega_score;
static inline omega_score omega_score_init(sndmin_ctx *ctx) {
    omega_score s={0};
    s.pad=sndmin_make_patch(ctx,&(sndmin_patch_desc){
        .wave={SNDMIN_SAW,SNDMIN_TRIANGLE},.detune=6,.unison=3,.unison_cents=13,.chorus=.75f,
        .cutoff=950,.resonance=.12f,.filter_env=1.1f,.filter={.35f,.6f,.45f,.55f},
        .amp={.22f,.4f,.7f,.55f},.lfo_hz=.12f,.lfo_depth=.25f,.lfo_route=SNDMIN_LFO_CUTOFF});
    s.bass=sndmin_make_patch(ctx,&(sndmin_patch_desc){
        .wave={SNDMIN_TRIANGLE,SNDMIN_PULSE},.sub=.7f,.detune=3,.cutoff=480,.resonance=.22f,
        .filter_env=1.4f,.filter={.005f,.18f,.1f,.08f},.amp={.008f,.15f,.45f,.085f}});
    s.arp=sndmin_make_patch(ctx,&(sndmin_patch_desc){
        .wave={SNDMIN_SAW,SNDMIN_PULSE},.unison=3,.unison_cents=11,.chorus=.35f,
        .cutoff=1450,.resonance=.24f,.filter_env=1.2f,.filter={.008f,.18f,.15f,.18f},
        .amp={.008f,.16f,.2f,.2f}});
    s.kick=sndmin_make_patch(ctx,&(sndmin_patch_desc){.instrument=SNDMIN_KICK});
    s.snare=sndmin_make_patch(ctx,&(sndmin_patch_desc){.instrument=SNDMIN_GATED_SNARE});
    s.hat=sndmin_make_patch(ctx,&(sndmin_patch_desc){.instrument=SNDMIN_HAT});
    s.tom=sndmin_make_patch(ctx,&(sndmin_patch_desc){.instrument=SNDMIN_TOM});
    return s;
}
static inline bool omega_score_ready(omega_score s) {
    return s.pad.id && s.bass.id && s.arp.id && s.kick.id && s.snare.id && s.hat.id && s.tom.id;
}
static inline bool omega_note(sndmin_ctx *ctx,omega_score *s,sndmin_patch patch,
                              uint32_t note,float duration,float gain,bool dry) {
    const sndmin_voice v=sndmin_play(ctx,&(sndmin_play_desc){.patch=patch,.note=note,
        .duration=duration,.bus=dry?SNDMIN_EFFECTS:SNDMIN_MUSIC,.voice={.gain=gain}});
    s->voices[s->next++%32]=v;
    return v.id!=0;
}
static inline void omega_score_stop(sndmin_ctx *ctx,omega_score *s) {
    for(unsigned i=0;i<32;++i) {
        if(s->voices[i].id) sndmin_stop(ctx,s->voices[i],.025f);
        s->voices[i]=(sndmin_voice){0};
    }
    s->next=0;
}
static inline bool omega_score_tick(sndmin_ctx *ctx,omega_score *s,uint32_t tick) {
    if(tick>=1200) return true;
    // A 150 BPM beat is 24 ticks; sixteenths align to six simulation ticks.
    const uint32_t step=tick/6;
    if(tick%6) return true;
    const uint32_t bar=step/16,within=step%16;
    static const uint32_t chords[4][4]={{53,56,60,63},{49,53,56,60},{51,55,58,62},{48,51,55,58}};
    static const uint32_t roots[4]={29,25,27,24};
    static const uint32_t pattern[8]={0,2,1,3,2,0,3,1};
    const uint32_t chord=(bar/2)%4;
    const float fade=tick>1080?(float)(1200-tick)/120.f:1.f;
    if(within==0 && bar<11) {
        for(unsigned n=0;n<4;++n)
            if(!omega_note(ctx,s,s->pad,chords[chord][n],1.22f,.065f*fade,false)) return false;
    }
    // Filtered intro; kick/bass enter under the gate expansion, full arp at 5 s.
    if(bar>=2 && bar<11 && within%4==0)
        if(!omega_note(ctx,s,s->kick,36,.18f,.68f*fade,true)) return false;
    if(bar>=3 && bar<11 && within==8)
        if(!omega_note(ctx,s,s->snare,38,.16f,.17f*fade,true)) return false;
    if(bar>=2 && within%4==2 && bar<11)
        if(!omega_note(ctx,s,s->hat,72,.055f,.075f*fade,true)) return false;
    if(bar>=1 && bar<11 && (within==0 || within==3 || within==6 || within==10 || within==12))
        if(!omega_note(ctx,s,s->bass,roots[chord]+(within==10?12:0),.18f,.36f*fade,false)) return false;
    if((bar==3 || bar==7 || bar==10) && (within==12 || within==14))
        if(!omega_note(ctx,s,s->tom,within==12?43:38,.22f,.26f*fade,true)) return false;
    if(bar>=3 && bar<11 && (bar>=6 || within%2==0)) {
        const uint32_t note=chords[chord][pattern[within%8]]+12+(within==14?12:0);
        if(!omega_note(ctx,s,s->arp,note,.13f,.095f*fade,false)) return false;
    }
    // A final high minor chord rings into the gate's shutdown.
    if(bar==11 && within==0) for(unsigned n=0;n<4;++n)
        if(!omega_note(ctx,s,s->pad,chords[0][n],1.6f,.075f,false)) return false;
    return sndmin_ok(ctx);
}
#endif
