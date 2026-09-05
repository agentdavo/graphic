/* Silent two-owner test. Test pacing uses relaxed atomics deliberately: only
 * the engine's rings may establish happens-before for commands and snapshots. */
#include "sndmin_internal.h"
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"thread test: line %d: %s\n",__LINE__,#x); abort(); } } while(0)
enum { FRAMES=600, CHANNELS=8 };
typedef struct {
    sndmin_ctx *ctx;
    float *output;
    _Atomic unsigned ready,done;
} run;
static void consume_frame(run *r,unsigned frame) {
    unsigned at=0;
    while(at<800) {
        unsigned n=1+(frame*17+at)%127;
        if(n>800-at) n=800-at;
        sndmin_mix(r->ctx,r->output+((size_t)frame*800+at)*CHANNELS,n);
        at+=n;
    }
}
static void *consumer(void *arg) {
    run *r=arg;
    for(unsigned frame=0;frame<FRAMES;++frame) {
        while(atomic_load_explicit(&r->ready,memory_order_relaxed)<=frame) sched_yield();
        consume_frame(r,frame);
        atomic_store_explicit(&r->done,frame+1,memory_order_relaxed);
    }
    return NULL;
}
static void produce(float *output,bool threaded) {
    sndmin_ctx *c=sndmin_init(&(sndmin_desc){.offline=true,.layout=SNDMIN_71}); CHECK(c);
    const float pcm[7]={0,0.2f,-0.7f,0.3f,0.4f,-0.1f,0.01f};
    const sndmin_sound sound=sndmin_make_sound(c,SNDMIN_BYTES(pcm),1,48000);
    const sndmin_patch patch=sndmin_make_patch(c,&(sndmin_patch_desc){.wave={SNDMIN_SAW,SNDMIN_PULSE},.unison=4,.chorus=0.4f});
    const sndmin_stream stream=sndmin_open_stream(c,"tests/assets/sndmin_river.ogg");
    CHECK(sound.id&&patch.id&&stream.id);
    /* Exercise unsigned rollover as well as repeated physical ring wrap. */
    const uint32_t seed=UINT32_MAX-1023;
    atomic_store(&c->command_read,seed); atomic_store(&c->command_write,seed);
    atomic_store(&c->snapshot_read,seed); atomic_store(&c->snapshot_write,seed);
    run r={.ctx=c,.output=output}; atomic_init(&r.ready,0); atomic_init(&r.done,0);
    pthread_t worker;
    if(threaded) CHECK(pthread_create(&worker,NULL,consumer,&r)==0);
    size_t index=0;
    sndmin_voice moving={0},music={0};
    for(unsigned frame=0;frame<FRAMES;++frame) {
        if(threaded) while(frame>atomic_load_explicit(&r.done,memory_order_relaxed)+3) {
            (void)sndmin_stats_get(c); sched_yield();
        }
        const sndmin_box walls[]={{{-6,-3,-8},{-5,3,8},0},{{5,-3,-8},{6,3,8},0}};
        sndmin_frame(c,&(sndmin_frame_desc){.index=frame,.boxes=walls,.box_count=2,
            .listener={0,0,(float)(frame%60)*0.01f},.delay_seconds=0.12f,.delay_feedback=0.3f});
        if(!frame) {
            moving=sndmin_play(c,&(sndmin_play_desc){.sound=sound,.loop=true,.spatial=true});
            music=sndmin_play(c,&(sndmin_play_desc){.stream=stream,.loop=true,.bus=SNDMIN_MUSIC,
                .voice={.gain=0.1f,.pitch=1.37f,.reverb_send=0.4f}});
            CHECK(moving.id&&music.id);
        }
        sndmin_set(c,moving,&(sndmin_voice_desc){.position={(float)(frame%40)*0.1f,0,-2},
            .velocity={1,0,0},.gain=0.01f,.pitch=1.37f,.reverb_send=0.5f,.fade_seconds=0.01f});
        if(frame%6==0) CHECK(sndmin_play(c,&(sndmin_play_desc){.patch=patch,.note=48+frame%24,
            .duration=0.03f,.voice={.gain=0.01f,.reverb_send=0.4f}}).id);
        if(frame==FRAMES-20) { sndmin_stop(c,moving,0.02f); sndmin_stop(c,music,0.02f); }
        CHECK(sndmin_feed(c,(uint64_t)frame*800,&index));
        sndmin_pump_streams(c,(uint64_t)frame*800);
        CHECK(!sndmin_stats_get(c).dropped_commands);
        if(threaded) atomic_store_explicit(&r.ready,frame+1,memory_order_relaxed);
        else consume_frame(&r,frame);
    }
    if(threaded) CHECK(pthread_join(worker,NULL)==0);
    (void)sndmin_stats_get(c); sndmin_mix(c,output,0);
    const sndmin_stats stats=sndmin_stats_get(c);
    CHECK(stats.samples==FRAMES*800&&stats.underruns==0&&stats.late_commands==0&&stats.dropped_commands==0);
    CHECK(atomic_load(&c->command_write)<seed&&atomic_load(&c->snapshot_write)<seed);
    sndmin_shutdown(c);
}
int main(void) {
    const size_t bytes=(size_t)FRAMES*800*CHANNELS*sizeof(float);
    float *serial=malloc(bytes),*parallel=malloc(bytes); CHECK(serial&&parallel);
    produce(serial,false); produce(parallel,true);
    CHECK(memcmp(serial,parallel,bytes)==0);
    double energy=0;
    for(size_t i=0;i<bytes/sizeof(float);++i) { CHECK(isfinite(parallel[i])); energy+=(double)parallel[i]*(double)parallel[i]; }
    CHECK(energy>1); free(serial); free(parallel);
    puts("sndmin: two-owner PCM/synth/stream/acoustic output == serial; rings wrap UINT32_MAX; zero late/drop/starvation");
    return 0;
}
