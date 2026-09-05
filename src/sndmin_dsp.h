/* Deterministic scalar maths: IEEE float, no contraction/fast-math.
 * Explicit approximations keep libm/platform transcendental code out of DSP. */
#ifndef SNDMIN_DSP_H
#define SNDMIN_DSP_H
#include <stdint.h>
#include <string.h>
#include "vkmin_math.h"
static inline float snd_clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }
static inline float snd_abs(float x) { return x<0?-x:x; }
static inline float snd_zap(float x) { return snd_abs(x)<1e-20f?0:x; }
static inline float snd_sqrt(float x) {
    if (x<=0) return 0;
    uint32_t u; memcpy(&u,&x,4); u=(u>>1)+0x1fc00000u;
    float y; memcpy(&y,&u,4);
    for (unsigned i=0;i<5;++i) y=0.5f*(y+x/y);
    return y;
}
static inline float snd_exp2(float x) {
    x=snd_clamp(x,-24,20);
    int n=(int)x; if ((float)n>x) --n;
    const float f=x-(float)n;
    const float p=1+f*(0.69314718f+f*(0.24022651f+f*(0.05550411f+f*(0.00961813f+f*0.00133336f))));
    const uint32_t bits=(uint32_t)(n+127)<<23;
    float scale; memcpy(&scale,&bits,4); return scale*p;
}
static inline float snd_sin(float cycles) {
    return vkmin_sin_cycles(cycles);
}
static inline uint32_t snd_hash(uint32_t x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
static inline float snd_noise(uint64_t sample,uint32_t salt) {
    return (float)(snd_hash((uint32_t)sample ^ snd_hash((uint32_t)(sample>>32)) ^ salt)>>8)*(1.0f/8388608.0f)-1;
}
static inline float snd_cubic(float a,float b,float c,float d,float t) {
    return b+0.5f*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)));
}
static inline float snd_soft(float x) { return x/(1+snd_abs(x)); }
#endif
