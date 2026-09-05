#include "demo.h"
int main(int argc,char **argv) {
    char wav[512],png[512]; FILE *journal=NULL;
    sndmin_ctx *c=demo_init(argc,argv,&journal,wav,png); if(!c) return 1;
    const sndmin_song song=sndmin_load_song(c,"examples/sndmin/02_song.snd");
    if(!song.id) { sndmin_shutdown(c); fclose(journal); return 1; }
    sndmin_frame(c,&(sndmin_frame_desc){.delay_seconds=0.375f,.delay_feedback=0.4f});
    (void)sndmin_play(c,&(sndmin_play_desc){.song=song,.bus=SNDMIN_MUSIC,.voice={.gain=0.3f,.reverb_send=0.22f}});
    return demo_finish(c,journal,2400,wav,png);
}
