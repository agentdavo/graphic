#include "demo.h"
#include "sndmin_dsp.h"
int main(int argc,char **argv) {
    char wav[512],png[512]; FILE *journal=NULL;
    sndmin_ctx *c=demo_init(argc,argv,&journal,wav,png); if(!c) return 1;
    const sndmin_patch p=sndmin_make_patch(c,&(sndmin_patch_desc){.instrument=SNDMIN_SNARE});
    for(uint32_t frame=0;frame<720;++frame) {
        const float size=frame<240?3:(frame<480?9:25);
        const sndmin_box boxes[]={{{-size,-1,-size},{size,0,size},0},{{-size,size,-size},{size,size+1,size},0},
            {{-size-1,0,-size},{-size,size,size},0},{{size,0,-size},{size+1,size,size},0},
            {{-size,0,-size-1},{size,size,-size},0},{{-size,0,size},{size,size,size+1},0}};
        sndmin_frame(c,&(sndmin_frame_desc){.index=frame,.listener={0,1,0},.boxes=boxes,.box_count=6});
        if(frame%60==0) (void)sndmin_play(c,&(sndmin_play_desc){.patch=p,.spatial=true,.note=48,
            .voice={.position={snd_sin((float)frame/240)*2,1,-2},.gain=0.7f,.reverb_send=0.85f}});
    }
    return demo_finish(c,journal,840,wav,png);
}
