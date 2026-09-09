/* Deterministic scalar maths: IEEE float, no contraction/fast-math.
 * Explicit approximations keep libm/platform transcendental code out of DSP.
 *
 * These are not here because they are faster than libm. They are here because
 * libm's accuracy is not specified: sqrtf and expf may differ by an ulp
 * between two C libraries, or between two versions of one, and an ulp inside a
 * feedback loop is a different WAV. Writing the approximation out fixes the
 * answer to this source file. Every function below is pure, branch-light and
 * defined for the whole input range its callers can produce -- a NaN escaping
 * into a delay line never comes back out.
 *
 * Accuracy is chosen for audio, not for numerics: a fraction of a cent of
 * pitch and a fraction of a dB of level are inaudible, so none of these is
 * correctly rounded and none of them needs to be. Do not reach for one in code
 * that is not making sound. */
#ifndef SNDMIN_DSP_H
#define SNDMIN_DSP_H
#include <stdint.h>
#include <string.h>
#include "min_math.h"
static inline float snd_clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }
static inline float snd_abs(float x) { return x<0?-x:x; }
/* Flush to zero. A filter or delay line decaying towards silence eventually
 * produces denormals, which on many CPUs cost tens of times a normal
 * multiply -- so a reverb tail that is already inaudible can be what makes the
 * callback miss its deadline. Applied where state is stored, not where it is
 * read, so the small values never accumulate in the first place. */
static inline float snd_zap(float x) { return snd_abs(x)<1e-20f?0:x; }
/* Newton's method for the square root, seeded by the classic exponent trick:
 * halving the exponent field of an IEEE float halves its logarithm, giving a
 * first guess within a few percent, and five iterations take that to float
 * precision. Zero and negatives return 0 rather than a NaN, because every
 * caller is taking the root of a squared length and a NaN would propagate. */
static inline float snd_sqrt(float x) {
    if (x<=0) return 0;
    uint32_t u; memcpy(&u,&x,4); u=(u>>1)+0x1fc00000u;
    float y; memcpy(&y,&u,4);
    for (unsigned i=0;i<5;++i) y=0.5f*(y+x/y);
    return y;
}
/* 2^x, split the standard way: the integer part becomes the exponent field
 * directly, and a fifth-order polynomial covers the fraction in [0,1). Base
 * two rather than e because everything that calls it is already thinking in
 * octaves, semitones or halvings. The clamp is not cosmetic: without it the
 * shifted exponent could be assembled out of range, so the result would be a
 * bit pattern rather than a number. With it, an extreme argument saturates at
 * 2^-24 or 2^20 instead of reaching zero or infinity. */
static inline float snd_exp2(float x) {
    x=snd_clamp(x,-24,20);
    int n=(int)x; if ((float)n>x) --n;
    const float f=x-(float)n;
    const float p=1+f*(0.69314718f+f*(0.24022651f+f*(0.05550411f+f*(0.00961813f+f*0.00133336f))));
    const uint32_t bits=(uint32_t)(n+127)<<23;
    float scale; memcpy(&scale,&bits,4); return scale*p;
}
/* Sine of a phase in cycles, not radians -- oscillator phase is naturally
 * 0..1, and taking the argument that way removes a multiply by 2*pi and a
 * range reduction from every call site. Shared with min_math so authored
 * animation and DSP wobble in step. */
static inline float snd_sin(float cycles) {
    return min_sin_cycles(cycles);
}
/* An integer bit mixer (the xor-multiply-shift family). Not a random number
 * generator and not cryptographic; it just spreads adjacent inputs far apart. */
static inline uint32_t snd_hash(uint32_t x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
/* White noise in [-1,1), as a pure function of position rather than a stream.
 * That is the whole point: a voice's noise depends on the absolute sample
 * index and a per-voice salt, so it carries no state, survives a voice being
 * stolen, and comes out identical on replay. Two voices with different salts
 * are uncorrelated, which is what stops a wall of noise sources summing into
 * one loud correlated hiss. */
static inline float snd_noise(uint64_t sample,uint32_t salt) {
    return (float)(snd_hash((uint32_t)sample ^ snd_hash((uint32_t)(sample>>32)) ^ salt)>>8)*(1.0f/8388608.0f)-1;
}
/* Catmull-Rom through b and c, using a and d for the slopes; t in [0,1]. The
 * fractional-delay interpolator for pitched sample playback: linear is cheaper
 * but its error is a lowpass that varies with the fraction, which pitched
 * material turns into audible warble. Written in Horner form -- rearranging it
 * would be algebraically identical and numerically different. */
static inline float snd_cubic(float a,float b,float c,float d,float t) {
    return b+0.5f*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)));
}
/* Soft clip: smoothly compresses towards +-1 and never reaches it. Used where
 * a signal must stay bounded without a corner, notably the filter's resonance
 * feedback -- a hard clip there would inject the very harmonics the filter is
 * there to remove. */
static inline float snd_soft(float x) { return x/(1+snd_abs(x)); }
/* Topology-preserving-transform state variable section, one per biquad's worth
 * of filtering. Not a direct-form biquad on purpose: the .1 channel's corners
 * sit at 2 Hz and 70 Hz against a 48 kHz rate, where a direct form's poles are
 * close enough to z=1 that single-precision conditioning becomes the dominant
 * error -- and this mixer is float by design, for determinism. The SVF stays
 * well behaved down to DC.
 *
 * Coefficients are literals because SNDMIN_RATE is fixed. That also keeps them
 * out of libm's hands, which is the same reason this header carries its own
 * sqrt, exp2 and sin: one ulp inside a feedback loop is a different WAV. They
 * are a1 = 1/(1+g(g+k)), a2 = g*a1, a3 = g*a2 with g = tan(pi*f0/fs) and
 * k = 1/Q; Q is Butterworth, so two cascaded low sections make a fourth-order
 * Linkwitz-Riley that is down exactly 6 dB at its corner. */
typedef struct { float ic1, ic2; } snd_svf;
#define SND_SVF_K 1.414213562f          /* 1/Q, Butterworth */
#define SND_LFE_HP_A1 0.999814897f      /* 2 Hz, driver protection */
#define SND_LFE_HP_A2 0.0001308754647f
#define SND_LFE_HP_A3 1.713155837e-08f
#define SND_LFE_LP_A1 0.9935417403f     /* 70 Hz; two of these are the LR4 */
#define SND_LFE_LP_A2 0.004551932687f
#define SND_LFE_LP_A3 2.085477675e-05f
static inline float snd_svf_low(float in,float a1,float a2,float a3,snd_svf *s) {
    const float v3=in-s->ic2;
    const float v1=a1*s->ic1+a2*v3;
    const float v2=s->ic2+a2*s->ic1+a3*v3;
    s->ic1=snd_zap(2*v1-s->ic1); s->ic2=snd_zap(2*v2-s->ic2);
    return v2;
}
static inline float snd_svf_high(float in,float a1,float a2,float a3,snd_svf *s) {
    const float v3=in-s->ic2;
    const float v1=a1*s->ic1+a2*v3;
    const float v2=s->ic2+a2*s->ic1+a3*v3;
    s->ic1=snd_zap(2*v1-s->ic1); s->ic2=snd_zap(2*v2-s->ic2);
    return in-SND_SVF_K*v1-v2;
}
/* The whole .1 chain, so the mixer and its test drive exactly the same code. */
static inline float snd_lfe_band(float in,snd_svf *hp,snd_svf *lp) {
    const float protected_=snd_svf_high(in,SND_LFE_HP_A1,SND_LFE_HP_A2,SND_LFE_HP_A3,hp);
    const float once=snd_svf_low(protected_,SND_LFE_LP_A1,SND_LFE_LP_A2,SND_LFE_LP_A3,&lp[0]);
    return snd_svf_low(once,SND_LFE_LP_A1,SND_LFE_LP_A2,SND_LFE_LP_A3,&lp[1]);
}
#endif
