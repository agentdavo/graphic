/* "Iron Across the Blue": original D-minor cinematic battle cue.
 * 120 BPM eighth-note ostinato, low brass swells and half-time war drums.
 * All notes and patches are original and rendered by native sndmin. */
#ifndef OMEGA_SCORE_H
#define OMEGA_SCORE_H
#include "sndmin.h"
typedef struct {
    sndmin_patch pad,bass,arp,brass,kick,tom;
    sndmin_voice voices[128];
    uint32_t next;
} omega_score;
static inline omega_score omega_score_init(sndmin_ctx *ctx) {
    omega_score s={0};
    s.pad=sndmin_make_patch(ctx,&(sndmin_patch_desc){
        .wave={SNDMIN_SAW,SNDMIN_TRIANGLE},.detune=6,.unison=3,.unison_cents=13,.chorus=.75f,
        .cutoff=680,.resonance=.12f,.filter_env=1.1f,.filter={.65f,.6f,.45f,.8f},
        .amp={.55f,.4f,.7f,.9f},.lfo_hz=.12f,.lfo_depth=.25f,.lfo_route=SNDMIN_LFO_CUTOFF});
    s.bass=sndmin_make_patch(ctx,&(sndmin_patch_desc){
        .wave={SNDMIN_TRIANGLE,SNDMIN_PULSE},.sub=.7f,.detune=3,.cutoff=480,.resonance=.22f,
        .filter_env=1.4f,.filter={.005f,.18f,.1f,.08f},.amp={.008f,.15f,.45f,.085f}});
    s.arp=sndmin_make_patch(ctx,&(sndmin_patch_desc){
        .wave={SNDMIN_SAW,SNDMIN_PULSE},.unison=3,.unison_cents=11,.chorus=.35f,
        .cutoff=850,.resonance=.14f,.filter_env=1.6f,.filter={.025f,.12f,.15f,.10f},
        .amp={.025f,.12f,.25f,.12f}});
    s.brass=sndmin_make_patch(ctx,&(sndmin_patch_desc){
        .wave={SNDMIN_SAW,SNDMIN_PULSE},.unison=3,.unison_cents=9,.detune=4,.sub=.25f,
        .cutoff=520,.resonance=.18f,.filter_env=2.8f,.filter={.22f,.6f,.65f,.7f},
        .amp={.12f,.3f,.75f,.7f},.chorus=.3f});
    s.kick=sndmin_make_patch(ctx,&(sndmin_patch_desc){.instrument=SNDMIN_KICK});
    s.tom=sndmin_make_patch(ctx,&(sndmin_patch_desc){.instrument=SNDMIN_TOM});
    return s;
}
static inline bool omega_score_ready(omega_score s) {
    return s.pad.id && s.bass.id && s.arp.id && s.brass.id && s.kick.id && s.tom.id;
}
static inline bool omega_note(sndmin_ctx *ctx,omega_score *s,sndmin_patch patch,
                              uint32_t note,float duration,float gain,bool dry) {
    const sndmin_voice v=sndmin_play(ctx,&(sndmin_play_desc){.patch=patch,.note=note,
        .duration=duration,.bus=dry?SNDMIN_EFFECTS:SNDMIN_MUSIC,.voice={.gain=gain}});
    s->voices[s->next++%128]=v;
    return v.id!=0;
}
static inline void omega_score_stop(sndmin_ctx *ctx,omega_score *s) {
    for(unsigned i=0;i<128;++i) {
        if(s->voices[i].id) sndmin_stop(ctx,s->voices[i],.025f);
        s->voices[i]=(sndmin_voice){0};
    }
    s->next=0;
}
static inline bool omega_score_tick(sndmin_ctx *ctx,omega_score *s,uint32_t tick) {
    if(tick>=1800) return true;
    // Picture accents are evaluated before the musical grid: the cut and
    // sustained cannon onsets need not fall on a quarter note.
    if(tick==660 || tick==681 || tick==897 || tick==960 || tick==1260 || tick==1560) {
        if(!omega_note(ctx,s,s->tom,33,.5f,.42f,true)) return false;
        if(!omega_note(ctx,s,s->brass,38,1.0f,.13f,false)) return false;
        if(!omega_note(ctx,s,s->brass,45,1.0f,.09f,false)) return false;
    }
    if(tick%15) return true;
    const uint32_t bar=tick/120,within=(tick%120)/15;
    static const uint32_t chords[5][4]={{50,57,62,65},{46,53,58,62},{43,50,58,62},{45,52,59,64},{50,57,62,65}};
    static const uint32_t roots[5]={26,22,31,33,26};
    static const uint32_t progression[15]={0,0,1,2,3,0,1,2,3,0,2,1,3,3,4};
    static const uint32_t pattern[8]={0,1,0,2,0,1,3,2};
    const uint32_t chord=progression[bar];
    const float drive=tick<600?.65f:1.f;
    if(within==0) {
        for(unsigned n=0;n<4;++n) {
            if(!omega_note(ctx,s,s->pad,chords[chord][n],bar==14?1.05f:1.65f,.055f,false)) return false;
        }
        if(!omega_note(ctx,s,s->bass,roots[chord],1.5f,.23f,false)) return false;
    }
    // Low bowed-string pulse grows out of the aperture, then doubles up as
    // the ship arrives. Alternating roots and fifths retain a heavy tread.
    if(bar>=2 && bar<14 && (bar>=4 || within%2==0)) {
        if(!omega_note(ctx,s,s->arp,chords[chord][pattern[within]],.16f,.115f*drive,false)) return false;
    }
    // An upper answering pulse lifts the reverse broadside shot into a climax.
    if(bar>=10 && bar<13 && within%2==0) {
        if(!omega_note(ctx,s,s->arp,chords[chord][pattern[within]]+12,.22f,.065f,false)) return false;
    }
    if(bar>=2 && bar<14 && (within==0 || within==4)) {
        if(!omega_note(ctx,s,s->kick,30,.35f,.55f*drive,true)) return false;
        if(!omega_note(ctx,s,s->tom,within==0?36:41,.4f,.28f*drive,true)) return false;
    }
    if(bar>=4 && bar<14 && (within==3 || within==6 || within==7)) {
        if(!omega_note(ctx,s,s->tom,within==7?33:43,.25f,.20f,true)) return false;
    }
    // A spare, original rising call over the battle, answered one octave down.
    if(bar>=5 && bar<14 && within==0) {
        static const uint32_t call[4]={62,65,67,64};
        if(!omega_note(ctx,s,s->brass,call[(bar-5)%4],1.25f,.12f,false)) return false;
        if(!omega_note(ctx,s,s->brass,call[(bar-5)%4]-12,1.25f,.10f,false)) return false;
    }
    return sndmin_ok(ctx);
}
#endif
