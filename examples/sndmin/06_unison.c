/* Public C11 client: one input snapshot, two independent contexts, one journal.
 * A 5-second transport probe. P pauses gameplay and mutes; M mutes; R restarts
 * gameplay. Audio's absolute sample clock always advances. No private headers.
 * --script supplies a reproducible sequence of those same input edges. */
#include "demo.h"
#include "vkmin.h"
#include <string.h>
typedef struct { uint32_t phase; bool paused, muted, cue; } unison_state;
static unison_state unison_step(unison_state state, const vkmin_inputs *input) {
    if(vkmin_key_pressed(input,'R')) state=(unison_state){0};
    if(vkmin_key_pressed(input,'P')) state.paused=!state.paused;
    if(vkmin_key_pressed(input,'M')) state.muted=!state.muted;
    state.cue=!state.paused&&state.phase>0&&state.phase%60==0;
    if(!state.paused) ++state.phase;
    return state;
}
int main(int argc, char **argv) {
    char wav[512],png[512]; FILE *journal=NULL;
    sndmin_ctx *audio=demo_init(argc,argv,&journal,wav,png); if(!audio) return 1;
    const sndmin_patch bell=sndmin_make_patch(audio,&(sndmin_patch_desc){
        .wave={SNDMIN_TRIANGLE,SNDMIN_TRIANGLE},.amp={0.001f,0.08f,0.2f,0.04f}});
    if(!bell.id) { sndmin_shutdown(audio); fclose(journal); return 1; }
    bool script=false,ok=true;
    for(int arg=1;arg<argc;++arg) if(!strcmp(argv[arg],"--script")) script=true;
#ifdef SNDMIN_VIDEO
    vkmin_ctx *gpu=vkmin_init(&(vkmin_desc){.argc=argc,.argv=argv,.journal=journal,.title="unison"});
#endif
    unison_state state={0}; sndmin_voice voice={0}; uint32_t tick=0;
    while(ok&&tick<300) {
#ifdef SNDMIN_VIDEO
        if(!vkmin_running(gpu)) break;
        const vkmin_frame frame=vkmin_frame_begin(gpu,NULL);
        const uint32_t target=frame.index; const vkmin_inputs snapshot=frame.input;
        if(target>=300) { vkmin_frame_end(gpu); ok=false; break; }
#else
        const uint32_t target=tick; const vkmin_inputs snapshot={0};
#endif
        while(ok&&tick<=target) {
            vkmin_inputs input=tick==target?snapshot:(vkmin_inputs){0};
            if(script) {
                const unsigned key=(tick==100||tick==130)?'P':((tick==160||tick==180)?'M':(tick==200?'R':0));
                if(key) input.pressed[key/32]|=1u<<(key%32);
            }
            ok=jrnl_write(journal,(jrnl_packet){JRNL_GAME,tick,sizeof input},&input);
            state=unison_step(state,&input);
            sndmin_frame(audio,&(sndmin_frame_desc){.index=tick});
            ok=ok&&sndmin_bus_set(audio,SNDMIN_MASTER,state.paused||state.muted?0:1);
            if(vkmin_key_pressed(&input,'R')&&voice.id) sndmin_stop(audio,voice,0);
            if(state.cue) {
                voice=sndmin_play(audio,&(sndmin_play_desc){.patch=bell,.note=72,.duration=0.18f,
                    .spatial=true,.voice={.gain=0.3f,.position={(float)(state.phase%120)*0.01f,0,-2},.min_radius=3}});
                ok=ok&&voice.id;
            }
            ++tick;
        }
#ifdef SNDMIN_VIDEO
        vkmin_pass_begin(gpu,&(vkmin_pass_desc){.color=vkmin_backbuffer(gpu),.clear_color=true,
            .clear={state.cue?0.8f:0.04f,state.paused?0.4f:0.08f,state.muted?0.3f:0.12f,1}});
        vkmin_pass_end(gpu);
        vkmin_frame_end(gpu);
#endif
    }
#ifdef SNDMIN_VIDEO
    vkmin_shutdown(gpu);
#endif
    if(!ok) { sndmin_shutdown(audio); fclose(journal); return 1; }
    return demo_finish(audio,journal,tick,wav,png);
}
