#include "sndmin_internal.h"
#include <stdlib.h>
#include <string.h>
/* Tracker parser is deliberately line-oriented and bounded. Unknown input is
 * rejected, including trailing fields, missing patterns and out-of-range notes. */
static int note_number(const char *s) {
    if(strcmp(s,"---")==0) return -1;
    static const char letters[]="C D EF G A B";
    int semitone=-1;
    for(int i=0;i<12;++i) if(letters[i]==s[0]&&s[0]!=' ') semitone=i;
    if(semitone<0) return -2;
    unsigned at=1;
    if(s[at]=='#') { ++semitone; ++at; }
    if(s[at]<'0'||s[at]>'8'||s[at+1]) return -2;
    return (s[at]-'0'+1)*12+semitone;
}
static sndmin_patch_desc preset(const char *name,bool *ok) {
    sndmin_patch_desc p={.cutoff=3500,.detune=7,.sub=0.2f,.filter_env=2,
        .amp={0.003f,0.16f,0.25f,0.12f},.filter={0.002f,0.2f,0.1f,0.1f}};
    if(strcmp(name,"bass")==0) { p.cutoff=450; p.resonance=0.55f; p.sub=0.6f; }
    else if(strcmp(name,"pad")==0) { p.cutoff=950; p.filter_env=2.5f; p.detune=13; p.unison=3; p.unison_cents=14;
        p.amp=(sndmin_adsr){0.12f,0.5f,0.7f,0.8f}; p.filter=(sndmin_adsr){0.7f,1,0.4f,0.6f}; p.chorus=0.8f;
        p.lfo_hz=0.2f; p.lfo_depth=0.45f; p.lfo_route=SNDMIN_LFO_CUTOFF; }
    else if(strcmp(name,"lead")==0) { p.wave[0]=SNDMIN_PULSE; p.cutoff=2500; p.chorus=0.3f; p.lfo_hz=5; p.lfo_depth=0.1f; }
    else if(strcmp(name,"kick")==0) p.instrument=SNDMIN_KICK;
    else if(strcmp(name,"snare")==0) p.instrument=SNDMIN_SNARE;
    else if(strcmp(name,"gated")==0) p.instrument=SNDMIN_GATED_SNARE;
    else if(strcmp(name,"hat")==0) p.instrument=SNDMIN_HAT;
    else if(strcmp(name,"tom")==0) p.instrument=SNDMIN_TOM;
    else *ok=false;
    return p;
}
sndmin_song sndmin_load_song(sndmin_ctx *c,const char *path) {
    if(!c||c->failed||c->started||!path||c->song_count==SND_RES) return (sndmin_song){0};
    FILE *file=fopen(path,"r"); if(!file) return (sndmin_song){0};
    snd_song *song=calloc(1,sizeof *song);
    if(!song) { fclose(file); return (sndmin_song){0}; }
    song->bpm=120; song->rows=8;
    uint32_t patches[32]={0},pattern=32,row=0,patch_count=0;
    sndmin_patch_desc pending[32]={0};
    bool seen[32]={false},ok=true; char line[512];
    while(ok&&fgets(line,sizeof line,file)) {
        if(!strchr(line,'\n')&&!feof(file)) { ok=false; break; }
        char *comment=strchr(line,'#'); /* sharps inside notes are not comments */
        if(comment==line || (comment && comment[-1]==' ')) *comment=0;
        char command[32],extra; if(sscanf(line,"%31s",command)!=1) continue;
        if(strcmp(command,"tempo")==0) {
            ok=sscanf(line,"tempo %f %f %c",&song->bpm,&song->swing,&extra)==2&&
                song->bpm>=30&&song->bpm<=300&&song->swing>=0&&song->swing<0.5f;
        } else if(strcmp(command,"rows")==0) {
            ok=sscanf(line,"rows %u %c",&song->rows,&extra)==1&&song->rows>=1&&song->rows<=64&&!song->count;
        } else if(strcmp(command,"patch")==0) {
            unsigned id; char name[32];
            ok=sscanf(line,"patch %u %31s %c",&id,name,&extra)==2&&id>0&&id<32;
            if(ok) {
                const sndmin_patch_desc p=preset(name,&ok);
                ok=ok&&patch_count<32;
                if(ok) { pending[patch_count]=p; patches[id]=++patch_count; }
            }
        } else if(strcmp(command,"arp")==0) {
            unsigned channel; int a,b;
            ok=sscanf(line,"arp %u %d %d %c",&channel,&a,&b,&extra)==3&&channel<8&&a>=-24&&a<=24&&b>=-24&&b<=24;
            if(ok) { song->arp[channel][0]=a; song->arp[channel][1]=b; }
        } else if(strcmp(command,"order")==0) {
            (void)strtok(line," \t\r\n"); const char *token=strtok(NULL," \t\r\n");
            while(token&&ok) {
                char *end=NULL; const unsigned long p=strtoul(token,&end,10);
                ok=end&&!*end&&p<32&&song->orders<128;
                if(ok) song->order[song->orders++]=(uint32_t)p;
                token=strtok(NULL," \t\r\n");
            }
        } else if(strcmp(command,"pattern")==0) {
            ok=sscanf(line,"pattern %u %c",&pattern,&extra)==1&&pattern<32&&!seen[pattern];
            if(ok) { seen[pattern]=true; row=0; song->pattern_first[pattern]=song->count; }
        } else {
            if(pattern==32||row>=song->rows) { ok=false; break; }
            unsigned channel=0; const char *cell=strtok(line," \t\r\n");
            while(cell&&ok) {
                if(channel>=8) { ok=false; break; }
                if(strcmp(cell,"---")!=0) {
                    char note[5]; unsigned patch,volume; int effect;
                    ok=sscanf(cell,"%4[^:]:%u:%u:%d%c",note,&patch,&volume,&effect,&extra)==4&&
                        patch>0&&patch<32&&patches[patch]&&volume<=100&&effect>=-24&&effect<=24;
                    const int midi=ok?note_number(note):-2;
                    ok=ok&&midi>=0&&midi<=127&&song->count<2048;
                    if(ok) { song->notes[song->count++]=(snd_note){row,channel,(uint32_t)midi,patches[patch],volume,effect};
                        ++song->pattern_count[pattern]; }
                }
                ++channel; cell=strtok(NULL," \t\r\n");
            }
            ++row;
        }
    }
    if(ferror(file)) ok=false;
    fclose(file);
    for(uint32_t i=0;i<song->orders;++i) if(!seen[song->order[i]]) ok=false;
    if(!ok||!song->orders||!song->count||patch_count>SND_RES-c->patch_count) { free(song); return (sndmin_song){0}; }
    /* Parse and capacity checks commit no resources or journal bytes. Built-in
     * presets are valid; the only remaining failure is journal IO (terminal). */
    const uint32_t first_patch=c->patch_count;
    for(uint32_t i=0;i<patch_count;++i) {
        if(!sndmin_make_patch(c,&pending[i]).id||c->failed) { free(song); return (sndmin_song){0}; }
    }
    for(uint32_t i=0;i<song->count;++i) song->notes[i].patch+=first_patch;
    c->songs[c->song_count++]=song; return (sndmin_song){c->song_count};
}
void sndmin_song_schedule(sndmin_ctx *c,const sndmin_play_desc *play,uint32_t voice) {
    const snd_song *song=c->songs[play->song.id-1];
    const double step=720000.0/(double)song->bpm; /* 16th-note samples */
    for(uint32_t order=0;order<song->orders;++order) {
        const uint32_t pattern=song->order[order];
        for(uint32_t n=0;n<song->pattern_count[pattern];++n) {
            const snd_note note=song->notes[song->pattern_first[pattern]+n];
            const uint32_t row=order*song->rows+note.row;
            const double start=(double)row*step+(row%2?step*(double)song->swing:0);
            const double length=step*(row%2?1-(double)song->swing:1+(double)song->swing);
            const bool arpeggio=song->arp[note.channel][0]!=0||song->arp[note.channel][1]!=0;
            const unsigned repeats=arpeggio?3:1;
            for(unsigned a=0;a<repeats;++a) {
                sndmin_play_desc p=*play; p.song.id=0; p.patch.id=note.patch; p.loop=false;
                const int pitch=(int)note.note+note.effect+(a?song->arp[note.channel][a-1]:0);
                p.note=(uint32_t)snd_clamp((float)pitch,1,127);
                p.voice.gain*= (float)note.volume*0.01f;
                p.duration=(float)(length/(double)repeats/48000)*0.85f;
                if(c->patches[p.patch.id-1].amp.attack>0.05f) p.duration=(float)(step*4/48000);
                if(c->next_voice==UINT32_MAX) { c->failed=true; return; }
                sndmin_submit(c,(snd_command){.sample=c->game_sample+(uint64_t)(start+length*(double)a/(double)repeats),
                    .op=CMD_PLAY,.id=++c->next_voice,.count=voice,.u.play=p});
            }
        }
    }
}
