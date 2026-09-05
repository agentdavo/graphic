/* Optional sndmin integration for 20_valley. This is game code: cameras,
 * material proxies and weather belong here, never in either library. */
#ifndef VALLEY_AUDIO_H
#define VALLEY_AUDIO_H
#include "sndmin.h"
#include "jrnl.h"
#include "valley_score.h"
typedef struct {
    sndmin_ctx *ctx; sndmin_patch bird,wind; sndmin_stream river; valley_score score;
    sndmin_voice gust; uint32_t frames;
} valley_audio;
static inline valley_audio valley_audio_init(FILE *journal) {
    valley_audio a={.ctx=sndmin_init(&(sndmin_desc){.offline=true,.journal=journal})};
    if(!a.ctx) gk_die("audio init failed");
    a.bird=sndmin_make_patch(a.ctx,&(sndmin_patch_desc){.wave={SNDMIN_TRIANGLE,SNDMIN_TRIANGLE},
        .lfo_hz=14,.lfo_depth=1.4f,.cutoff=9000,.amp={0.003f,0.03f,0.1f,0.03f}});
    a.wind=sndmin_make_patch(a.ctx,&(sndmin_patch_desc){.wave={SNDMIN_NOISE,SNDMIN_NOISE},.cutoff=600,
        .amp={0.2f,0.2f,1,0.5f},.lfo_hz=0.12f,.lfo_depth=0.4f,.lfo_route=SNDMIN_LFO_CUTOFF});
    a.river=sndmin_open_stream(a.ctx,"tests/assets/sndmin_river.ogg");
    a.score=valley_score_init(a.ctx);
    if(!a.bird.id||!a.wind.id||!a.river.id||!valley_score_ready(a.score)) gk_die("audio resource load failed");
    return a;
}
/* Integer hash matches outdoor_hash() in shaders/lib/outdoor.glsl. */
static inline uint32_t valley_audio_hash(uint32_t x,uint32_t z,uint32_t seed) {
    uint32_t h=x*747796405u+z*2891336453u+seed*277803737u;
    h=(h^(h>>16))*2246822519u; return (h^(h>>13))*3266489917u;
}
/* CPU reference of shader wind(): same spatial and temporal frequencies.
 * Deterministic polynomial sine avoids platform libm in the audio journal. */
static inline float valley_audio_wind(vec3 p,uint32_t frame) {
    const float t=(float)frame/60;
    const float gust=0.55f+0.35f*vkmin_sin_cycles((p.x*0.009f+p.z*0.013f-t*0.37f)/6.28318530718f);
    const float bend=vkmin_sin_cycles((p.x*0.19f+p.z*0.11f-t*1.7f)/6.28318530718f);
    return gust*(0.6f+0.4f*bend);
}
static inline void valley_audio_frame(valley_audio *a,uint32_t index,vec3 eye,vec3 target,vec3 velocity,
                                     const sndmin_box *boxes,uint32_t count,float river_height) {
    sndmin_frame(a->ctx,&(sndmin_frame_desc){.index=index,.listener=eye,.forward=vkmin_vec3_sub(target,eye),
        .velocity=velocity,.boxes=boxes,.box_count=count});
    if(!a->frames) {
        a->gust=sndmin_play(a->ctx,&(sndmin_play_desc){.patch=a->wind,.voice={.gain=0.15f}});
        (void)sndmin_play(a->ctx,&(sndmin_play_desc){.stream=a->river,.loop=true,.spatial=true,
            .voice={.position={eye.x,river_height,eye.z+5},.gain=0.7f,.min_radius=12,.reverb_send=0.6f}});
    }
    if(!valley_score_frame(a->ctx,a->score,index,0.5f)) gk_die("audio score queue failed");
    sndmin_set(a->ctx,a->gust,&(sndmin_voice_desc){.gain=valley_audio_wind(eye,index)*0.3f,.pitch=1,.fade_seconds=0.1f});
    if(index%37==0) {
        const uint32_t h=valley_audio_hash(index,0,13);
        (void)sndmin_play(a->ctx,&(sndmin_play_desc){.patch=a->bird,.note=90+h%12,.duration=0.15f,.spatial=true,
            .voice={.position={eye.x+(float)(h%40)-20,eye.y+5,eye.z+(float)((h>>8)%40)-20},
                .gain=0.5f,.min_radius=3,.reverb_send=0.4f}});
    }
    a->frames=index+1;
}
#endif
