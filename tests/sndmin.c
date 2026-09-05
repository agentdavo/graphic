#include "sndmin_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef SNDMIN_TEST_GUARD
#include "process.h"
#endif
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"sndmin test: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
static sndmin_ctx *sequence(void) {
    sndmin_ctx *c=sndmin_init(&(sndmin_desc){.offline=true});
    if(!c)return NULL;
    const sndmin_patch p=sndmin_make_patch(c,&(sndmin_patch_desc){.wave={SNDMIN_SAW,SNDMIN_PULSE},.detune=9,.unison=2,.chorus=0.4f});
    sndmin_frame(c,&(sndmin_frame_desc){0});
    for(unsigned i=0;i<70;++i) (void)sndmin_play(c,&(sndmin_play_desc){.patch=p,.note=36+i%36,.duration=0.12f,
        .voice={.gain=0.005f,.reverb_send=0.5f},.priority=(int)(i%3)});
    return c;
}
static double cpu_seconds(void) {
#ifdef _WIN32
    FILETIME created,ended,kernel,user;
    if(!GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user)) return -1;
    const uint64_t ticks=((uint64_t)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+
        ((uint64_t)user.dwHighDateTime<<32)+user.dwLowDateTime;
    return (double)ticks/10000000;
#else
    return (double)clock()/CLOCKS_PER_SEC;
#endif
}
int main(int argc,char **argv) {
#ifdef SNDMIN_TEST_GUARD
    if(argc==2&&strncmp(argv[1],"--violate",9)==0) sndmin_test_violation((unsigned)(argv[1][9]-'0'));
    CHECK(test_aborts(argv[0],"--violate0")); CHECK(test_aborts(argv[0],"--violate1")); CHECK(test_aborts(argv[0],"--violate2"));
#endif
    if(argc==2&&strcmp(argv[1],"--bench")==0) {
        sndmin_ctx *c=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(c);
        float pcm[480]; for(unsigned i=0;i<480;++i) pcm[i]=snd_sin((float)i/480)*0.001f;
        const sndmin_sound sound=sndmin_make_sound(c,SNDMIN_BYTES(pcm),1,48000); CHECK(sound.id);
        sndmin_frame(c,&(sndmin_frame_desc){0});
        for(unsigned i=0;i<64;++i) (void)sndmin_play(c,&(sndmin_play_desc){.sound=sound,.loop=true,.voice={.reverb_send=0.5f}});
        size_t index=0; CHECK(sndmin_feed(c,0,&index));
        float buffer[512*2]; const double begin=cpu_seconds();
        for(unsigned i=0;i<48000*60/512;++i) { sndmin_mix(c,buffer,512); (void)sndmin_stats_get(c); }
        const double seconds=cpu_seconds()-begin;
        printf("64 PCM voices + four FDN buses: %.3f CPU seconds / 60 audio seconds = %.2f%% of one core\n",seconds,seconds*100/60);
        sndmin_shutdown(c); return 0;
    }
    if(argc==5&&strcmp(argv[1],"--replay")==0) {
        sndmin_ctx *c=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(c);
        CHECK(sndmin_replay(c,argv[2]));
        CHECK(sndmin_render(c,(uint32_t)strtoul(argv[3],NULL,10),argv[4],NULL));
        sndmin_dump(c,stdout); sndmin_shutdown(c); return 0;
    }
    CHECK(sndmin_init(&(sndmin_desc){.layout=(sndmin_layout)9})==NULL);
    sndmin_ctx *a=sequence(),*b=sequence(); CHECK(a&&b);
    size_t ai=0,bi=0; CHECK(sndmin_feed(a,0,&ai)&&sndmin_feed(b,0,&bi));
    float x[4096*2],y[4096*2];
    sndmin_mix(a,x,4096);
    unsigned at=0; while(at<4096) { unsigned n=(at%127)+1; if(n>4096-at)n=4096-at; sndmin_mix(b,y+at*2,n); at+=n; (void)sndmin_stats_get(b); }
    CHECK(memcmp(x,y,sizeof x)==0);
    CHECK(sndmin_stats_get(a).stolen==6);
    double energy=0; for(unsigned i=0;i<8192;++i) { CHECK(isfinite(x[i])); energy+=(double)x[i]*(double)x[i]; }
    CHECK(energy>0.0001); sndmin_shutdown(a); sndmin_shutdown(b);
    for(unsigned layout=0;layout<3;++layout) for(unsigned angle=0;angle<360;++angle) {
        const sndmin_acoustic acoustic=sndmin_acoustics(&(sndmin_frame_desc){0},
            &(sndmin_voice_desc){.position={snd_sin((float)angle/360),0,-snd_sin((float)angle/360+0.25f)}},(sndmin_layout)layout);
        float energy_pan=0; for(unsigned ch=0;ch<8;++ch) energy_pan+=acoustic.pan[ch]*acoustic.pan[ch];
        CHECK(snd_abs(energy_pan-1)<0.00001f); CHECK(acoustic.openness==1);
    }
    const sndmin_box wall={{-1,-1,-4},{1,1,-3},0};
    const sndmin_voice_desc emitter={.position={0,0,-10}};
    const sndmin_acoustic clear=sndmin_acoustics(&(sndmin_frame_desc){0},&emitter,SNDMIN_STEREO);
    const sndmin_acoustic blocked=sndmin_acoustics(&(sndmin_frame_desc){.boxes=&wall,.box_count=1},&emitter,SNDMIN_STEREO);
    CHECK(blocked.gain<clear.gain&&blocked.lowpass<clear.lowpass&&blocked.taps[0].gain>0);
    a=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(a);
    const float pulse[4]={1,0,0,0};
    const sndmin_sound sound=sndmin_make_sound(a,SNDMIN_BYTES(pulse),1,48000); CHECK(sound.id);
    sndmin_frame(a,&(sndmin_frame_desc){.index=1});
    (void)sndmin_play(a,&(sndmin_play_desc){.sound=sound});
    ai=0; CHECK(sndmin_feed(a,800,&ai)); sndmin_mix(a,x,1024);
    for(unsigned i=0;i<1600;++i) CHECK(x[i]==0);
    CHECK(x[1600]>0.5f); sndmin_shutdown(a);
    /* The integral-position fast path must null against four-point reference,
     * including loop edges, fractional pitch, fades and stale handles. */
    for(unsigned pass=0;pass<2;++pass) {
        a=sndmin_init(&(sndmin_desc){.offline=true}); b=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(a&&b);
        b->reference_pcm=true;
        const float waveform[7]={0,0.2f,-0.7f,0.3f,0.4f,-0.1f,0.01f};
        const sndmin_sound sa=sndmin_make_sound(a,SNDMIN_BYTES(waveform),1,48000),sb=sndmin_make_sound(b,SNDMIN_BYTES(waveform),1,48000);
        CHECK(sa.id&&sb.id);
        (void)sndmin_play(a,&(sndmin_play_desc){.sound=sa,.loop=true,.voice={.pitch=pass?1.37f:1}});
        (void)sndmin_play(b,&(sndmin_play_desc){.sound=sb,.loop=true,.voice={.pitch=pass?1.37f:1}});
        ai=bi=0; CHECK(sndmin_feed(a,0,&ai)&&sndmin_feed(b,0,&bi));
        sndmin_mix(a,x,4096); sndmin_mix(b,y,4096); CHECK(memcmp(x,y,sizeof x)==0);
        sndmin_shutdown(a); sndmin_shutdown(b);
    }
    /* Decoder/streaming parallel implementations, including seek-to-start. */
    a=sndmin_init(&(sndmin_desc){.offline=true}); b=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(a&&b);
    const sndmin_sound memory=sndmin_load(a,"tests/assets/sndmin_river.ogg");
    const sndmin_stream stream=sndmin_open_stream(b,"tests/assets/sndmin_river.ogg"); CHECK(memory.id&&stream.id);
    (void)sndmin_play(a,&(sndmin_play_desc){.sound=memory,.loop=true,.voice={.pitch=1.37f}});
    (void)sndmin_play(b,&(sndmin_play_desc){.stream=stream,.loop=true,.voice={.pitch=1.37f}});
    ai=bi=0; CHECK(sndmin_feed(a,0,&ai)&&sndmin_feed(b,0,&bi));
    for(unsigned block=0;block<600;++block) {
        sndmin_pump_streams(b,(uint64_t)block*400);
        sndmin_mix(a,x,400); sndmin_mix(b,y,400); CHECK(memcmp(x,y,800*sizeof(float))==0);
    }
    CHECK(sndmin_stats_get(b).underruns==0); sndmin_shutdown(a); sndmin_shutdown(b);
    /* Nonspatial one-shots cannot evict game-side acoustic tracking for a
     * high-priority looping emitter just by wrapping its handle index. */
    a=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(a);
    const sndmin_sound persistent=sndmin_make_sound(a,SNDMIN_BYTES(pulse),1,48000); CHECK(persistent.id);
    const sndmin_voice tracked=sndmin_play(a,&(sndmin_play_desc){.sound=persistent,.loop=true,.spatial=true,.priority=10});
    for(unsigned i=0;i<128;++i) (void)sndmin_play(a,&(sndmin_play_desc){.sound=persistent});
    const size_t before=a->pending_count;
    sndmin_frame(a,&(sndmin_frame_desc){.index=6});
    bool found=false;
    for(size_t i=before;i<a->pending_count;++i) if(a->pending[i].op==CMD_ACOUSTIC&&a->pending[i].id==tracked.id) found=true;
    CHECK(found); sndmin_shutdown(a);
    a=sequence(); b=sequence(); CHECK(a&&b); a->desc.rate=b->desc.rate=44100;
    ai=bi=0; CHECK(sndmin_feed(a,0,&ai)&&sndmin_feed(b,0,&bi)); sndmin_mix(a,x,4096);
    for(unsigned k=0;k<4096;k+=64) { sndmin_mix(b,y+k*2,64); (void)sndmin_stats_get(b); }
    CHECK(memcmp(x,y,sizeof x)==0); sndmin_shutdown(a); sndmin_shutdown(b);
    /* Full queue has bounded failure, never overwrites unread commands. */
    a=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(a);
    for(unsigned i=0;i<SND_COMMAND_CAP+1;++i) sndmin_submit(a,(snd_command){.op=CMD_STOP,.id=i+1});
    ai=0; CHECK(!sndmin_feed(a,0,&ai)&&ai==SND_COMMAND_CAP);
    sndmin_mix(a,x,1); CHECK(sndmin_feed(a,0,&ai)&&ai==SND_COMMAND_CAP+1); sndmin_shutdown(a);
    /* NaN, stale resources, invalid geometry and corrupt acoustic commands. */
    a=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(a);
    CHECK(!sndmin_play(a,&(sndmin_play_desc){.sound={999}}).id);
    CHECK(!sndmin_make_patch(a,&(sndmin_patch_desc){.cutoff=NAN}).id);
    snd_command malformed={.op=CMD_ACOUSTIC,.id=1,.u.acoustic={.taps={{.delay=NAN}}}};
    CHECK(!sndmin_command_valid(a,&malformed));
    sndmin_frame(a,&(sndmin_frame_desc){.listener={NAN,0,0}}); CHECK(sndmin_stats_get(a).dropped_commands==1);
    sndmin_shutdown(a);
    for(unsigned mode=0;mode<2;++mode) {
        a=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(a);
        const sndmin_song song=sndmin_load_song(a,"examples/sndmin/02_song.snd"); CHECK(song.id);
        sndmin_frame(a,&(sndmin_frame_desc){0});
        const sndmin_voice group=sndmin_play(a,&(sndmin_play_desc){.song=song,.voice={.gain=0.3f}}); CHECK(group.id);
        sndmin_frame(a,&(sndmin_frame_desc){.index=1});
        if(mode) sndmin_stop(a,group,0.01f);
        else sndmin_set(a,group,&(sndmin_voice_desc){.gain=0});
        const char *path=mode?"tests/out/sndmin/group-stop.wav":"tests/out/sndmin/group-mute.wav";
        CHECK(sndmin_render(a,30,path,NULL));
        if(mode) CHECK(sndmin_stats_get(a).voices==0);
        sndmin_shutdown(a);
        uint64_t count=0; uint32_t channels=0,rate=0;
        float *decoded=sndmin_decode(path,&count,&channels,&rate); CHECK(decoded&&count==24000&&channels==2);
        double first=0; for(unsigned i=0;i<1600;++i) first+=(double)decoded[i]*(double)decoded[i];
        CHECK(first>0);
        for(size_t i=6000;i<(size_t)count*channels;++i) CHECK(decoded[i]==0);
        free(decoded);
    }
    puts("sndmin: buffer partition null test, voice stealing, 1080 speaker-energy cases, occlusion, reflections, exact frame timing passed");
    return 0;
}
