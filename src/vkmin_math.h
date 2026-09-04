/* vkmin_math.h -- vectors, matrices, quaternions: all by value, all pure,
 * nothing mutating in place. There is only Normalized(), never Normalize().
 * vec2/vec4/mat4 are the shared.h types the GPU sees; vec3 is host-only. */
#ifndef VKMIN_MATH_H
#define VKMIN_MATH_H

#include <math.h>

#include "shared.h" /* vec2, vec4, mat4: the same types the GPU sees */

typedef struct { float x, y, z; } vec3;

static inline vec3 vkmin_vec3_sub(vec3 a, vec3 b) {
    return (vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline vec3 vkmin_vec3_add(vec3 a, vec3 b) { return (vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline vec3 vkmin_vec3_scale(vec3 a, float s) { return (vec3){a.x * s, a.y * s, a.z * s}; }
static inline float vkmin_vec3_length(vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

static inline vec3 vkmin_vec3_cross(vec3 a, vec3 b) {
    return (vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static inline float vkmin_vec3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static inline vec3 vkmin_vec3_normalize(vec3 v) {
    const float len = sqrtf(vkmin_vec3_dot(v, v));
    const float inv = len > 0.0f ? 1.0f / len : 0.0f;
    return (vec3){v.x * inv, v.y * inv, v.z * inv};
}

static inline mat4 vkmin_mat4_identity(void) {
    return (mat4){{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}

/* Written as an explicit loop rather than sixteen copy-pasted index
 * expressions: short index arithmetic pasted around is exactly the shape of
 * bug this codebase is trying not to have. */
static inline mat4 vkmin_mat4_mul(mat4 a, mat4 b) {
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

static inline mat4 vkmin_mat4_rotate_x(float radians) {
    const float s = sinf(radians), c = cosf(radians);
    mat4 out = vkmin_mat4_identity();
    out.m[5] = c;
    out.m[6] = s;
    out.m[9] = -s;
    out.m[10] = c;
    return out;
}

static inline mat4 vkmin_mat4_rotate_y(float radians) {
    const float s = sinf(radians), c = cosf(radians);
    mat4 out = vkmin_mat4_identity();
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
}

/* Right-handed, depth range 0..1, with the Y flip Vulkan's framebuffer
 * orientation needs folded in here rather than into a negative viewport. */
static inline mat4 vkmin_mat4_perspective(float fovy_radians, float aspect, float znear, float zfar) {
    const float f = 1.0f / tanf(fovy_radians * 0.5f);
    mat4 out = {{0}};
    out.m[0] = f / aspect;
    out.m[5] = -f;
    out.m[10] = zfar / (znear - zfar);
    out.m[11] = -1.0f;
    out.m[14] = (zfar * znear) / (znear - zfar);
    return out;
}

static inline mat4 vkmin_mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    const vec3 f = vkmin_vec3_normalize(vkmin_vec3_sub(center, eye));
    const vec3 s = vkmin_vec3_normalize(vkmin_vec3_cross(f, up));
    const vec3 u = vkmin_vec3_cross(s, f);
    mat4 out = vkmin_mat4_identity();
    out.m[0] = s.x;  out.m[4] = s.y;  out.m[8] = s.z;
    out.m[1] = u.x;  out.m[5] = u.y;  out.m[9] = u.z;
    out.m[2] = -f.x; out.m[6] = -f.y; out.m[10] = -f.z;
    out.m[12] = -vkmin_vec3_dot(s, eye);
    out.m[13] = -vkmin_vec3_dot(u, eye);
    out.m[14] = vkmin_vec3_dot(f, eye);
    return out;
}

static inline mat4 vkmin_mat4_translate(vec3 t) {
    mat4 out = vkmin_mat4_identity();
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
}

static inline mat4 vkmin_mat4_scale(vec3 s) {
    mat4 out = vkmin_mat4_identity();
    out.m[0] = s.x;
    out.m[5] = s.y;
    out.m[10] = s.z;
    return out;
}

/* Unit quaternion xyzw to a rotation matrix. */
static inline mat4 vkmin_mat4_from_quat(vec4 q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    mat4 out = vkmin_mat4_identity();
    out.m[0] = 1 - 2 * (y * y + z * z); out.m[4] = 2 * (x * y - z * w);     out.m[8] = 2 * (x * z + y * w);
    out.m[1] = 2 * (x * y + z * w);     out.m[5] = 1 - 2 * (x * x + z * z); out.m[9] = 2 * (y * z - x * w);
    out.m[2] = 2 * (x * z - y * w);     out.m[6] = 2 * (y * z + x * w);     out.m[10] = 1 - 2 * (x * x + y * y);
    return out;
}

static inline mat4 vkmin_mat4_trs(vec3 t, vec4 r, vec3 s) {
    return vkmin_mat4_mul(vkmin_mat4_translate(t), vkmin_mat4_mul(vkmin_mat4_from_quat(r), vkmin_mat4_scale(s)));
}

static inline vec4 vkmin_mat4_mul_vec4(mat4 m, vec4 v) {
    vec4 out;
    out.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w;
    out.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w;
    out.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    out.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return out;
}

static inline vec3 vkmin_mat4_mul_point(mat4 m, vec3 p) {
    const vec4 r = vkmin_mat4_mul_vec4(m, (vec4){p.x, p.y, p.z, 1.0f});
    const float inv = r.w != 0.0f ? 1.0f / r.w : 1.0f;
    return (vec3){r.x * inv, r.y * inv, r.z * inv};
}

/* Orthographic, depth 0..1, Y flipped like mat4_perspective so the two agree. */
static inline mat4 vkmin_mat4_ortho(float l, float r, float b, float t, float znear, float zfar) {
    mat4 out = {{0}};
    out.m[0] = 2.0f / (r - l);
    out.m[5] = -2.0f / (t - b);
    out.m[10] = 1.0f / (znear - zfar);
    out.m[12] = -(r + l) / (r - l);
    out.m[13] = (t + b) / (t - b);
    out.m[14] = znear / (znear - zfar);
    out.m[15] = 1.0f;
    return out;
}

/* General 4x4 inverse by cofactors. Returns identity for a singular input,
 * which is wrong but visible; an assert here would be in a hot loop. */
static inline mat4 vkmin_mat4_inverse(mat4 a) {
    const float *m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) return vkmin_mat4_identity();
    mat4 out;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] / det;
    return out;
}

#endif /* VKMIN_MATH_H */
