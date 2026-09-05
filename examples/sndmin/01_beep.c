#include "demo.h"
int main(int argc,char **argv) {
    char wav[512],png[512]; FILE *journal=NULL;
    sndmin_ctx *c=demo_init(argc,argv,&journal,wav,png); if(!c) return 1;
    const sndmin_patch patch=sndmin_make_patch(c,&(sndmin_patch_desc){.wave={SNDMIN_TRIANGLE,SNDMIN_TRIANGLE},.cutoff=10000});
    sndmin_frame(c,&(sndmin_frame_desc){0});
    const sndmin_voice v=sndmin_play(c,&(sndmin_play_desc){.patch=patch,.note=69,.duration=0.6f,.voice={.gain=0.45f}});
    if(!v.id) { sndmin_shutdown(c); fclose(journal); return 1; }
    return demo_finish(c,journal,120,wav,png);
}
