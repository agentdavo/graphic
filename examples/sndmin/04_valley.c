#include "demo.h"
#include "sndmin_dsp.h"
/* Audio-side valley reference. JRNL_GAME carries the same frame/wind/position
 * inputs a graphics integration consumes; geometry is ordinary world AABBs. */
int main(int argc,char **argv) {
    char wav[512],png[512]; FILE *journal=NULL;
    sndmin_ctx *c=demo_init(argc,argv,&journal,wav,png); if(!c) return 1;
    const sndmin_patch bird=sndmin_make_patch(c,&(sndmin_patch_desc){.wave={SNDMIN_TRIANGLE,SNDMIN_TRIANGLE},
        .lfo_hz=14,.lfo_depth=1.4f,.cutoff=9000,.amp={0.003f,0.03f,0.1f,0.03f}});
    const sndmin_patch wind=sndmin_make_patch(c,&(sndmin_patch_desc){.wave={SNDMIN_NOISE,SNDMIN_NOISE},.cutoff=600,
        .amp={0.2f,0.2f,1,0.5f},.lfo_hz=0.12f,.lfo_depth=0.4f,.lfo_route=SNDMIN_LFO_CUTOFF});
    const sndmin_stream river=sndmin_open_stream(c,"tests/assets/sndmin_river.ogg");
    const sndmin_song song=sndmin_load_song(c,"examples/sndmin/02_song.snd");
    if(!river.id||!song.id) { sndmin_shutdown(c); fclose(journal); return 1; }
    sndmin_voice gust={0};
    for(uint32_t frame=0;frame<600;++frame) {
        const float strength=0.3f+0.15f*snd_sin((float)frame/360);
        const sndmin_box boxes[]={{{-35,-10,-100},{-30,18,100},0},{{30,-10,-100},{35,25,100},0},
            {{-30,-2,-100},{30,-1,100},0}};
        const struct { uint32_t frame; float wind; vec3 listener; } input={frame,strength,{0,2,(float)frame*0.015f}};
        if(!jrnl_write(journal,(jrnl_packet){JRNL_GAME,input.frame,sizeof input},&input)) { sndmin_shutdown(c); fclose(journal); return 1; }
        sndmin_frame(c,&(sndmin_frame_desc){.index=input.frame,.listener=input.listener,.velocity={0,0,0.9f},.boxes=boxes,.box_count=3});
        if(!frame) {
            gust=sndmin_play(c,&(sndmin_play_desc){.patch=wind,.note=40,.voice={.gain=strength,.reverb_send=0.3f}});
            (void)sndmin_play(c,&(sndmin_play_desc){.stream=river,.loop=true,.spatial=true,
                .voice={.position={5,0,10},.gain=1,.reverb_send=0.7f,.min_radius=5}});
            (void)sndmin_play(c,&(sndmin_play_desc){.song=song,.bus=SNDMIN_MUSIC,.voice={.gain=0.12f}});
        }
        sndmin_set(c,gust,&(sndmin_voice_desc){.gain=input.wind,.pitch=1,.fade_seconds=0.1f,.reverb_send=0.3f});
        if(frame%37==0) (void)sndmin_play(c,&(sndmin_play_desc){.patch=bird,.note=90+snd_hash(frame)%12,.duration=0.15f,
            .spatial=true,.voice={.position={(float)(snd_hash(frame)%40)-20,5,(float)(snd_hash(frame+1)%50)},
                .gain=0.6f,.min_radius=3,.reverb_send=0.5f}});
    }
    return demo_finish(c,journal,600,wav,png);
}
