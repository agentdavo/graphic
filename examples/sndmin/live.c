#define _POSIX_C_SOURCE 200809L
#include "sndmin.h"
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
static double now(void) {
#ifdef _WIN32
    LARGE_INTEGER value,frequency; QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
    return (double)value.QuadPart/(double)frequency.QuadPart;
#else
    struct timespec t; (void)clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec+(double)t.tv_nsec/1000000000;
#endif
}
int main(int argc,char **argv) {
    const unsigned seconds=argc>1?(unsigned)strtoul(argv[1],NULL,10):40;
    if(seconds<1||seconds>3600) return 1;
    sndmin_ctx *c=sndmin_init(&(sndmin_desc){0}); if(!c) return 1;
    const sndmin_song song=sndmin_load_song(c,"examples/sndmin/02_song.snd");
    if(!song.id) { sndmin_shutdown(c); return 1; }
    const double start=now();
    for(uint32_t frame=0;frame<seconds*60;++frame) {
        while(now()<start+(double)frame/60) {
#ifdef _WIN32
            Sleep(1);
#else
            const struct timespec wait={0,1000000}; (void)nanosleep(&wait,NULL);
#endif
        }
        sndmin_frame(c,&(sndmin_frame_desc){.index=frame,.delay_seconds=0.375f,.delay_feedback=0.4f});
        if(frame%2280==0) (void)sndmin_play(c,&(sndmin_play_desc){.song=song,.bus=SNDMIN_MUSIC,.voice={.gain=0.3f,.reverb_send=0.22f}});
        if(sndmin_stats_get(c).dropped_commands) { sndmin_dump(c,stderr); sndmin_shutdown(c); return 1; }
    }
    sndmin_dump(c,stdout);
    const sndmin_stats stats=sndmin_stats_get(c);
    printf("live duration: %u seconds; starvation count is engine-side, hardware xruns unavailable\n",seconds);
    sndmin_shutdown(c); return stats.underruns||stats.late_commands?1:0;
}
