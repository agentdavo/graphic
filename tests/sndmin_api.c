/* A client of public headers only; also compiled in the reverse include order. */
#ifdef AUDIO_HEADER_FIRST
#include "sndmin.h"
#include "vkmin.h"
#else
#include "vkmin.h"
#include "sndmin.h"
#endif
#include "jrnl.h"
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"api:%d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    FILE *bad=fopen("tests/out/sndmin/bad-song.snd","w"); CHECK(bad);
    CHECK(fputs("patch 1 bass\nthis is not a song\n",bad)>=0); CHECK(fclose(bad)==0);
    FILE *journal=jrnl_open("tests/out/sndmin/api.jrnl",true); CHECK(journal);
    sndmin_ctx *ctx=sndmin_init(&(sndmin_desc){.offline=true,.journal=journal}); CHECK(ctx);
    const long before=ftell(journal); CHECK(before>=0);
    for(unsigned attempt=0;attempt<256;++attempt) {
        CHECK(!sndmin_load_song(ctx,"tests/out/sndmin/bad-song.snd").id);
        CHECK(sndmin_ok(ctx)&&ftell(journal)==before);
    }
    CHECK(sndmin_make_patch(ctx,&(sndmin_patch_desc){0}).id==1);
    CHECK(sndmin_load_song(ctx,"examples/sndmin/02_song.snd").id);
    const float wave[4]={0.2f,-0.2f,0.3f,-0.3f};
    const sndmin_sound sound=sndmin_make_sound(ctx,SNDMIN_BYTES(wave),1,SNDMIN_RATE); CHECK(sound.id);
    CHECK(!sndmin_bus_set(ctx,SNDMIN_MUSIC,0)); /* frame first */
    sndmin_voice voice={0};
    for(uint32_t tick=0;tick<8;++tick) {
        sndmin_frame(ctx,&(sndmin_frame_desc){.index=tick}); CHECK(sndmin_ok(ctx));
        if(tick==0) { voice=sndmin_play(ctx,&(sndmin_play_desc){.sound=sound,.loop=true,.bus=SNDMIN_MUSIC}); CHECK(voice.id); }
        if(tick==1||tick==3) CHECK(sndmin_bus_set(ctx,SNDMIN_MUSIC,tick==1?0:1));
        if(tick==4||tick==5) sndmin_set(ctx,voice,&(sndmin_voice_desc){.gain=tick==4?0:1});
        if(tick==6) sndmin_stop(ctx,voice,0);
    }
    CHECK(!sndmin_bus_set(ctx,SNDMIN_MUSIC,-1)); CHECK(sndmin_ok(ctx));
    CHECK(sndmin_render(ctx,8,"tests/out/sndmin/api.wav",NULL));
    CHECK(!sndmin_play(ctx,&(sndmin_play_desc){.sound=sound}).id);
    sndmin_shutdown(ctx); CHECK(fclose(journal)==0);
    FILE *wav=fopen("tests/out/sndmin/api.wav","rb"); CHECK(wav);
    CHECK(fseek(wav,44,SEEK_SET)==0);
    for(unsigned tick=0;tick<8;++tick) {
        unsigned energy=0;
        for(unsigned sample=0;sample<800*4;++sample) {
            const int byte=fgetc(wav); CHECK(byte!=EOF);
            if(sample>=8) energy+=(unsigned)byte;
        }
        CHECK((energy!=0)==(tick==0||tick==3||tick==5));
    }
    CHECK(fclose(wav)==0);
    ctx=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(ctx);
    const sndmin_patch patch=sndmin_make_patch(ctx,&(sndmin_patch_desc){0}); CHECK(patch.id);
    sndmin_frame(ctx,&(sndmin_frame_desc){0});
    sndmin_frame(ctx,&(sndmin_frame_desc){0}); /* invalid repeated timeline */
    CHECK(!sndmin_ok(ctx)&&!sndmin_play(ctx,&(sndmin_play_desc){.patch=patch}).id);
    sndmin_shutdown(ctx);
    journal=jrnl_open("tests/out/sndmin/io-failure.jrnl",true); CHECK(journal);
    ctx=sndmin_init(&(sndmin_desc){.offline=true,.journal=journal}); CHECK(ctx);
    CHECK(fflush(journal)==0);
    journal=freopen("tests/out/sndmin/io-failure.jrnl","rb",journal); CHECK(journal);
    CHECK(!sndmin_make_patch(ctx,&(sndmin_patch_desc){0}).id&&!sndmin_ok(ctx));
    sndmin_shutdown(ctx); CHECK(fclose(journal)==0);
    ctx=sndmin_init(&(sndmin_desc){.offline=true}); CHECK(ctx);
    CHECK(!sndmin_render(ctx,1,"tests/out/sndmin/api.wav/invalid.wav",NULL)&&!sndmin_ok(ctx));
    sndmin_shutdown(ctx);
    puts("Public C11 API: transactional loads, persistent bus mute, restore, stop and terminal failure passed.");
    return 0;
}
