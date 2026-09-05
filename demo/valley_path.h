/* Pure camera route shared by rendered frames and intervening audio ticks. */
#ifndef VALLEY_PATH_H
#define VALLEY_PATH_H
#include "vkmin_math.h"
typedef struct { vec3 eye, target; } valley_key;
typedef struct { vec3 eye, target, velocity; } valley_camera;
static inline vec3 valley_spline(vec3 a, vec3 b, vec3 c, vec3 d, float t) {
    const float w[4] = {-0.5f*t+t*t-0.5f*t*t*t, 1-2.5f*t*t+1.5f*t*t*t,
                        0.5f*t+2*t*t-1.5f*t*t*t, -0.5f*t*t+0.5f*t*t*t};
    const vec3 p[4] = {a,b,c,d};
    vec3 out = {0};
    for (int i = 0; i < 4; ++i) out = vkmin_vec3_add(out,vkmin_vec3_scale(p[i],w[i]));
    return out;
}
/* keys contains at least two entries. The finite route clamps at frame 7199. */
static inline valley_camera valley_camera_at(const valley_key *keys, uint32_t count, uint32_t tick) {
    const float t = fminf((float)tick/7199,1)*(float)(count-1);
    const uint32_t b = (uint32_t)t < count-1 ? (uint32_t)t : count-2;
    const uint32_t a = b ? b-1 : b, c = b+1, d = c+1 < count ? c+1 : c;
    const float u=t-(float)b;
    valley_camera camera={
        .eye=valley_spline(keys[a].eye,keys[b].eye,keys[c].eye,keys[d].eye,u),
        .target=valley_spline(keys[a].target,keys[b].target,keys[c].target,keys[d].target,u)};
    const float derivative[4]={-0.5f+2*u-1.5f*u*u,-5*u+4.5f*u*u,0.5f+4*u-4.5f*u*u,-u+1.5f*u*u};
    const vec3 points[4]={keys[a].eye,keys[b].eye,keys[c].eye,keys[d].eye};
    if(tick<7199) for(unsigned k=0;k<4;++k)
        camera.velocity=vkmin_vec3_add(camera.velocity,vkmin_vec3_scale(points[k],derivative[k]*(float)(count-1)*60/7199));
    return camera;
}
#endif
