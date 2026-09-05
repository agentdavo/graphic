#include "demo.h"
#include "../../demo/valley_score.h"
int main(int argc, char **argv) {
    char wav[512], png[512]; FILE *journal=NULL;
    sndmin_ctx *ctx=demo_init(argc,argv,&journal,wav,png);
    if (!ctx) return 1;
    const valley_score score=valley_score_init(ctx);
    bool ok=valley_score_ready(score);
    for (uint32_t tick=0; ok && tick<VALLEY_SCORE_RENDER_TICKS; ++tick) {
        sndmin_frame(ctx,&(sndmin_frame_desc){.index=tick});
        ok=valley_score_frame(ctx,score,tick,1);
    }
    if (!ok) { sndmin_shutdown(ctx); fclose(journal); return 1; }
    return demo_finish(ctx,journal,VALLEY_SCORE_RENDER_TICKS,wav,png);
}
