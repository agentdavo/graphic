/* anim.h -- deterministic skeletal animation sampling. Caller-owned bone
 * matrices out; nothing here reads a clock or changes the source scene. */
#ifndef VKMIN_ANIM_H
#define VKMIN_ANIM_H

#include "vkmin_math.h"
#include "vkmin.h"
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

/* Authored pose tracks for the supplied CesiumMan rig (19 named joints).
 * Run uses its imported walk clip. Idle and jump have independent keyframed
 * torso, knee and arm offsets; these are demo art, not renderer policy. */
enum { ANIM_IDLE, ANIM_RUN, ANIM_JUMP };
static inline uint32_t anim_character(const scene *s, float time, uint32_t state, float phase,
                                      mat4 mesh_world, mat4 *out, uint32_t cap) {
    if (state == ANIM_RUN || s->header.joint_count != 19) return anim_bones(s, time * 1.6f, mesh_world, out, cap);
    typedef struct { float time, torso, knee, arm; } pose_key;
    static const pose_key idle[] = {{0, -.015f, 0, -.025f}, {1.5f, .015f, .02f, .025f}, {3, -.015f, 0, -.025f}};
    static const pose_key jump[] = {{0, .15f, .25f, -.3f}, {.18f, -.12f, .85f, -.8f}, {.48f, .05f, .45f, -.5f}, {.8f, .18f, .15f, -.15f}};
    const pose_key *track = state == ANIM_JUMP ? jump : idle;
    const uint32_t count = state == ANIM_JUMP ? 4 : 3;
    const float t = state == ANIM_JUMP ? fminf(fmaxf(phase, 0), .8f) : fmodf(time, 3);
    uint32_t hi = 1;
    while (hi + 1 < count && track[hi].time < t) ++hi;
    const pose_key a = track[hi - 1], b = track[hi];
    const float f = (t - a.time) / (b.time - a.time);
    const float torso = a.torso + (b.torso-a.torso)*f, knee = a.knee + (b.knee-a.knee)*f, arm = a.arm + (b.arm-a.arm)*f;
    vkm_channel channels[ANIM_MAX_JOINTS * 3];
    vkm_key keys[ANIM_MAX_JOINTS * 3];
    VKMIN_ASSERT(s->header.channel_count <= ANIM_MAX_JOINTS * 3, "too many character channels");
    for (uint32_t c = 0; c < s->header.channel_count; ++c) {
        channels[c] = s->channels[c]; channels[c].first_key = c; channels[c].key_count = 1;
        keys[c].time = 0;
        anim_sample_channel(&s->channels[c], s->keys, 0, keys[c].value);
        if (channels[c].path != VKM_PATH_ROTATION) continue;
        const uint32_t j = channels[c].joint;
        const float angle = j == 1 ? torso : j == 13 || j == 14 ? knee : j == 5 || j == 6 ? arm : 0;
        const float sn = sinf(angle * .5f), cs = cosf(angle * .5f);
        const vec4 q = {keys[c].value[0], keys[c].value[1], keys[c].value[2], keys[c].value[3]};
        keys[c].value[0] = q.w*sn + q.x*cs; keys[c].value[1] = q.y*cs + q.z*sn;
        keys[c].value[2] = q.z*cs - q.y*sn; keys[c].value[3] = q.w*cs - q.x*sn;
    }
    scene pose = *s;
    pose.channels = channels; pose.keys = keys; pose.header.anim_duration = 0;
    return anim_bones(&pose, 0, mesh_world, out, cap);
}

#endif
