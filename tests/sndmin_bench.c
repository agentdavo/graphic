/* Silent capacity measurements. CPU timing surrounds, never enters, the mixer.
 * Report the workload rather than extrapolating the cheap PCM case to synths. */
#include "sndmin_internal.h"
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"benchmark: line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static double cpu_seconds(void) {
#ifdef _WIN32
    FILETIME created,ended,kernel,user;
    CHECK(GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user));
    const uint64_t ticks=((uint64_t)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+
        ((uint64_t)user.dwHighDateTime<<32)+user.dwLowDateTime;
    return (double)ticks/10000000;
#else
    return (double)clock()/CLOCKS_PER_SEC;
#endif
}
int main(void) {
    const char *names[]={"64 mono PCM, unity pitch, stereo", "64 spatial PCM, fractional pitch, 7.1",
        "64 spatial synths, four unison copies, 7.1", "56 spatial synths + 8 Vorbis streams, 7.1"};
    for(unsigned mode=0;mode<4;++mode) {
        sndmin_ctx *c=sndmin_init(&(sndmin_desc){.offline=true,.layout=mode?SNDMIN_71:SNDMIN_STEREO}); CHECK(c);
        float pcm[480]; for(unsigned i=0;i<480;++i) pcm[i]=snd_sin((float)i/480)*0.01f;
        const sndmin_sound sound=sndmin_make_sound(c,SNDMIN_BYTES(pcm),1,48000); CHECK(sound.id);
        const sndmin_patch patch=sndmin_make_patch(c,&(sndmin_patch_desc){.wave={SNDMIN_SAW,SNDMIN_PULSE},
            .unison=4,.unison_cents=12,.detune=7,.sub=0.3f,.cutoff=1800,.resonance=0.7f,
            .filter_env=3,.lfo_hz=0.5f,.lfo_depth=0.4f,.lfo_route=SNDMIN_LFO_CUTOFF,.chorus=0.6f}); CHECK(patch.id);
        sndmin_stream streams[8]={{0}};
        if(mode==3) for(unsigned i=0;i<8;++i) { streams[i]=sndmin_open_stream(c,"tests/assets/sndmin_river.ogg"); CHECK(streams[i].id); }
        const sndmin_box room[]={{{-5,-3,-8},{-4,3,8},0},{{4,-3,-8},{5,3,8},0},
            {{-5,-3,-8},{5,-2,8},0},{{-5,2,-8},{5,3,8},0},{{-5,-3,-8},{5,3,-7},0},{{-5,-3,7},{5,3,8},0}};
        sndmin_frame(c,&(sndmin_frame_desc){.boxes=mode?room:NULL,.box_count=mode?6:0,
            .delay_seconds=mode?0.18f:0,.delay_feedback=0.6f});
        for(unsigned i=0;i<64;++i) {
            sndmin_play_desc play={.loop=true,.spatial=mode!=0,.bus=(sndmin_bus)(i%4),.note=36+i%48,
                .voice={.gain=0.01f,.pitch=mode?1.37f:1,.position={(float)(i%7)*0.4f-1.2f,0,-2},
                    .velocity={0.3f,0,0},.reverb_send=0.5f,.lfe_send=0.2f}};
            if(mode==3&&i<8) play.stream=streams[i];
            else if(mode>=2) play.patch=patch;
            else play.sound=sound;
            CHECK(sndmin_play(c,&play).id);
        }
        size_t index=0; float buffer[800*8]; double mix_cpu=0,energy=0; float peak=0;
        const double begin=cpu_seconds();
        for(unsigned frame=0;frame<3600;++frame) {
            if(frame) sndmin_frame(c,&(sndmin_frame_desc){.index=frame,.boxes=mode?room:NULL,.box_count=mode?6:0,
                .listener={snd_sin((float)frame/360)*0.2f,0,0},.velocity={0.2f,0,0},
                .delay_seconds=mode?0.18f:0,.delay_feedback=0.6f});
            CHECK(sndmin_feed(c,(uint64_t)frame*800,&index));
            sndmin_pump_streams(c,(uint64_t)frame*800);
            const double start=cpu_seconds();
            sndmin_mix(c,buffer,512); sndmin_mix(c,buffer+512*c->channels,288);
            mix_cpu+=cpu_seconds()-start;
            for(unsigned i=0;i<800*c->channels;++i) {
                CHECK(isfinite(buffer[i])); energy+=(double)buffer[i]*(double)buffer[i];
                if(snd_abs(buffer[i])>peak) peak=snd_abs(buffer[i]);
            }
        }
        const double total_cpu=cpu_seconds()-begin;
        const sndmin_stats stats=sndmin_stats_get(c);
        CHECK(stats.voices==64&&stats.samples==48000*60&&stats.underruns==0&&stats.dropped_commands==0&&stats.late_commands==0&&energy>0);
        printf("%s: mixer %.3f s / 60 s = %.2f%%; full harness %.2f%%; peak %.4f; zero starvation/late/drop\n",
            names[mode],mix_cpu,mix_cpu*100/60,total_cpu*100/60,(double)peak);
        sndmin_shutdown(c);
    }
    return 0;
}
