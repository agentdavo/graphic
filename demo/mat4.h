/* mat4.h -- the demo's maths, kept out of vkmin entirely. Every function here
 * is pure: it reads only its parameters and returns a new value. That is what
 * makes the cube's animation a function of an integer frame index and nothing
 * else, and it is why tests/mat4_test.c can check it without a GPU. */
#ifndef VKMIN_MAT4_H
#define VKMIN_MAT4_H

#include <math.h>

typedef struct { float x, y, z; } vec3;
typedef struct { float m[16]; } mat4; /* column-major, as GLSL expects */

static inline vec3 vec3_sub(vec3 a, vec3 b) {
    return (vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline vec3 vec3_cross(vec3 a, vec3 b) {
    return (vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static inline float vec3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static inline vec3 vec3_normalize(vec3 v) {
    const float len = sqrtf(vec3_dot(v, v));
    const float inv = len > 0.0f ? 1.0f / len : 0.0f;
    return (vec3){v.x * inv, v.y * inv, v.z * inv};
}

static inline mat4 mat4_identity(void) {
    return (mat4){{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}

/* Written as an explicit loop rather than sixteen copy-pasted index
 * expressions: short index arithmetic pasted around is exactly the shape of
 * bug this codebase is trying not to have. */
static inline mat4 mat4_mul(mat4 a, mat4 b) {
    mat4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

static inline mat4 mat4_rotate_x(float radians) {
    const float s = sinf(radians), c = cosf(radians);
    mat4 out = mat4_identity();
    out.m[5] = c;
    out.m[6] = s;
    out.m[9] = -s;
    out.m[10] = c;
    return out;
}

static inline mat4 mat4_rotate_y(float radians) {
    const float s = sinf(radians), c = cosf(radians);
    mat4 out = mat4_identity();
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
}

/* Right-handed, depth range 0..1, with the Y flip Vulkan's framebuffer
 * orientation needs folded in here rather than into a negative viewport. */
static inline mat4 mat4_perspective(float fovy_radians, float aspect, float znear, float zfar) {
    const float f = 1.0f / tanf(fovy_radians * 0.5f);
    mat4 out = {{0}};
    out.m[0] = f / aspect;
    out.m[5] = -f;
    out.m[10] = zfar / (znear - zfar);
    out.m[11] = -1.0f;
    out.m[14] = (zfar * znear) / (znear - zfar);
    return out;
}

static inline mat4 mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    const vec3 f = vec3_normalize(vec3_sub(center, eye));
    const vec3 s = vec3_normalize(vec3_cross(f, up));
    const vec3 u = vec3_cross(s, f);
    mat4 out = mat4_identity();
    out.m[0] = s.x;  out.m[4] = s.y;  out.m[8] = s.z;
    out.m[1] = u.x;  out.m[5] = u.y;  out.m[9] = u.z;
    out.m[2] = -f.x; out.m[6] = -f.y; out.m[10] = -f.z;
    out.m[12] = -vec3_dot(s, eye);
    out.m[13] = -vec3_dot(u, eye);
    out.m[14] = vec3_dot(f, eye);
    return out;
}

#endif /* VKMIN_MAT4_H */
