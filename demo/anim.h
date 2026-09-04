/* anim.h -- skeletal animation sampling, as pure functions. Frame index in,
 * bone matrices out; nothing here reads a clock. */
#ifndef VKMIN_ANIM_H
#define VKMIN_ANIM_H

#include "vkmin_math.h"
#include "scene.h"

enum { ANIM_MAX_JOINTS = 128 };

static inline vec4 quat_slerp(vec4 a, vec4 b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) { b = (vec4){-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) { /* nearly parallel: lerp and renormalise */
        vec4 r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        const float len = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        return (vec4){r.x / len, r.y / len, r.z / len, r.w / len};
    }
    const float theta = acosf(d), s = sinf(theta);
    const float wa = sinf((1.0f - t) * theta) / s, wb = sinf(t * theta) / s;
    return (vec4){a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

/* Samples one channel at time t (seconds, already wrapped into the clip). */
static inline void anim_sample_channel(const vkm_channel *ch, const vkm_key *keys, float t, float out[4]) {
    const vkm_key *k = keys + ch->first_key;
    if (ch->key_count == 1 || t <= k[0].time) {
        for (int i = 0; i < 4; ++i) out[i] = k[0].value[i];
        return;
    }
    if (t >= k[ch->key_count - 1].time) {
        for (int i = 0; i < 4; ++i) out[i] = k[ch->key_count - 1].value[i];
        return;
    }
    uint32_t hi = 1;
    while (k[hi].time < t) ++hi;
    const vkm_key *a = &k[hi - 1], *b = &k[hi];
    const float span = b->time - a->time;
    const float u = span > 0.0f ? (t - a->time) / span : 0.0f;
    if (ch->path == VKM_PATH_ROTATION) {
        const vec4 q = quat_slerp((vec4){a->value[0], a->value[1], a->value[2], a->value[3]},
                                  (vec4){b->value[0], b->value[1], b->value[2], b->value[3]}, u);
        out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
    } else {
        for (int i = 0; i < 4; ++i) out[i] = a->value[i] + (b->value[i] - a->value[i]) * u;
    }
}

/* Bone matrices for the frame: inverse(mesh world) * parent * chain * inverse bind.
 * `mesh_world` is the skinned node's transform as the instance will use it. */
static inline uint32_t anim_bones(const scene *s, float time, mat4 mesh_world, mat4 *out, uint32_t cap) {
    const uint32_t n = s->header.joint_count < cap ? s->header.joint_count : cap;
    if (n == 0) return 0;
    vec3 t[ANIM_MAX_JOINTS], sc[ANIM_MAX_JOINTS];
    vec4 r[ANIM_MAX_JOINTS];
    for (uint32_t j = 0; j < n; ++j) {
        const vkm_joint *jt = &s->joints[j];
        t[j] = (vec3){jt->rest_t[0], jt->rest_t[1], jt->rest_t[2]};
        r[j] = (vec4){jt->rest_r[0], jt->rest_r[1], jt->rest_r[2], jt->rest_r[3]};
        sc[j] = (vec3){jt->rest_s[0], jt->rest_s[1], jt->rest_s[2]};
    }
    const float wrapped = s->header.anim_duration > 0.0f ? fmodf(time, s->header.anim_duration) : 0.0f;
    for (uint32_t c = 0; c < s->header.channel_count; ++c) {
        const vkm_channel *ch = &s->channels[c];
        if (ch->joint >= n) continue;
        float v[4];
        anim_sample_channel(ch, s->keys, wrapped, v);
        if (ch->path == VKM_PATH_TRANSLATION) t[ch->joint] = (vec3){v[0], v[1], v[2]};
        else if (ch->path == VKM_PATH_ROTATION) r[ch->joint] = (vec4){v[0], v[1], v[2], v[3]};
        else sc[ch->joint] = (vec3){v[0], v[1], v[2]};
    }
    /* Joints are cooked parent-before-child (glTF skins list them that way),
     * so one forward pass builds every global. */
    mat4 global[ANIM_MAX_JOINTS];
    mat4 skin_parent;
    for (int i = 0; i < 16; ++i) skin_parent.m[i] = s->header.skin_parent[i];
    const mat4 inv_mesh = vkmin_mat4_inverse(mesh_world);
    for (uint32_t j = 0; j < n; ++j) {
        const mat4 local = vkmin_mat4_trs(t[j], r[j], sc[j]);
        const int32_t parent = s->joints[j].parent;
        global[j] = vkmin_mat4_mul(parent >= 0 && (uint32_t)parent < j ? global[parent] : skin_parent, local);
        mat4 inv_bind;
        for (int i = 0; i < 16; ++i) inv_bind.m[i] = s->joints[j].inverse_bind[i];
        out[j] = vkmin_mat4_mul(inv_mesh, vkmin_mat4_mul(global[j], inv_bind));
    }
    return n;
}

#endif
