/* cook -- glTF in, a packed .vkm scene plus BCn .ktx2 textures out.
 *
 * Runs offline, once, and its output is committed. Everything expensive or
 * fiddly about assets happens here so the runtime never parses JSON, never
 * generates a mip, never compresses a texture, and never has a file-path
 * failure mode beyond "the cooked file is missing".
 *
 *   cook <scene.gltf|glb> <outdir> [--max-size N]
 *
 * Tools are not core and are not held to the core line budget, but they are
 * held to the same warning set.
 */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "cook_image.h"
#include "pack.h"
#include "vkm_format.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FATAL(...)                                                                \
    do {                                                                          \
        fprintf(stderr, "cook: ");                                                \
        fprintf(stderr, __VA_ARGS__);                                             \
        fputc('\n', stderr);                                                      \
        exit(1);                                                                  \
    } while (0)

/* ------------------------------------------------------- growable arrays -- */

#define ARRAY(T)   \
    struct {       \
        T *v;      \
        size_t n;  \
        size_t cap; \
    }
#define PUSH(a, item)                                                             \
    do {                                                                          \
        if ((a).n == (a).cap) {                                                   \
            (a).cap = (a).cap ? (a).cap * 2 : 256;                                \
            (a).v = realloc((a).v, (a).cap * sizeof *(a).v);                      \
            if (!(a).v) FATAL("out of memory");                                   \
        }                                                                         \
        (a).v[(a).n++] = (item);                                                  \
    } while (0)

typedef struct {
    ARRAY(Vertex) verts;
    ARRAY(uint32_t) indices;
    ARRAY(SkinVertex) skin;
    ARRAY(Mesh) meshes;
    ARRAY(Material) materials;
    ARRAY(vkm_texture) textures;
    ARRAY(vkm_node) nodes;
    ARRAY(vkm_joint) joints;
    ARRAY(vkm_channel) channels;
    ARRAY(vkm_key) keys;
    int *image_texture;      /* glTF image index -> cooked texture index, -1 if unused */
    int *image_kind;         /* what the first material to use it thinks it is */
    float bmin[3], bmax[3];
    float anim_duration;
    float skin_parent[16];
    const char *out_dir;
    int max_size;
} cook_state;

enum { KIND_ALBEDO = 0, KIND_ALBEDO_ALPHA, KIND_NORMAL, KIND_DATA, KIND_EMISSIVE };

/* ------------------------------------------------------------- textures --- */

static const char *image_uri_dir(const char *gltf_path, char *buf, size_t cap) {
    const char *slash = strrchr(gltf_path, '/');
    if (!slash) {
        snprintf(buf, cap, ".");
        return buf;
    }
    const size_t n = (size_t)(slash - gltf_path);
    snprintf(buf, cap, "%.*s", (int)n, gltf_path);
    return buf;
}

static int cook_texture(cook_state *st, const cgltf_data *data, const char *gltf_path,
                        const cgltf_texture *tex, int kind) {
    if (!tex || !tex->image) return -1;
    const int image_index = (int)cgltf_image_index(data, tex->image);
    if (st->image_texture[image_index] >= 0) {
        if (st->image_kind[image_index] != kind) {
            fprintf(stderr, "cook: image %d used as kind %d and %d; keeping the first\n",
                    image_index, st->image_kind[image_index], kind);
        }
        return st->image_texture[image_index];
    }
    const cgltf_image *img = tex->image;
    cook_rgba src = {0};
    if (img->buffer_view) {
        const uint8_t *bytes = (const uint8_t *)img->buffer_view->buffer->data + img->buffer_view->offset;
        src = cook_image_decode(bytes, img->buffer_view->size);
    } else if (img->uri) {
        char dir[512], path[1024];
        image_uri_dir(gltf_path, dir, sizeof dir);
        snprintf(path, sizeof path, "%s/%s", dir, img->uri);
        src = cook_image_load(path);
    }
    if (!src.px) FATAL("could not decode image %d (%s)", image_index, img->uri ? img->uri : "embedded");

    const cook_bc_format fmt = kind == KIND_ALBEDO ? COOK_BC1_SRGB
                               : kind == KIND_ALBEDO_ALPHA ? COOK_BC3_SRGB
                               : kind == KIND_NORMAL ? COOK_BC5
                               : kind == KIND_EMISSIVE ? COOK_BC1_SRGB
                                                        : COOK_BC1_UNORM;
    vkm_texture t = {.sampler = VKMIN_SAMPLER_ANISO_REPEAT};
    if (tex->sampler && (tex->sampler->wrap_s == 33071 || tex->sampler->wrap_t == 33071)) {
        t.sampler = VKMIN_SAMPLER_LINEAR_CLAMP;
    }
    snprintf(t.file, sizeof t.file, "tex_%03zu.ktx2", st->textures.n);
    char out_path[1024];
    snprintf(out_path, sizeof out_path, "%s/%s", st->out_dir, t.file);
    cook_image_write_ktx2(&src, fmt, st->max_size, out_path);
    free(src.px);
    const int index = (int)st->textures.n;
    PUSH(st->textures, t);
    st->image_texture[image_index] = index;
    st->image_kind[image_index] = kind;
    printf("  texture %3d  %-32s %s\n", index, img->uri ? img->uri : "(embedded)",
           fmt == COOK_BC1_SRGB ? "BC1 sRGB" : fmt == COOK_BC3_SRGB ? "BC3 sRGB"
           : fmt == COOK_BC5 ? "BC5" : "BC1");
    return index;
}

/* ------------------------------------------------------------ materials --- */

static uint32_t tex_or_none(int t) { return t < 0 ? VKMIN_NONE : (uint32_t)t; }

static void cook_materials(cook_state *st, const cgltf_data *data, const char *gltf_path) {
    for (size_t i = 0; i < data->materials_count; ++i) {
        const cgltf_material *m = &data->materials[i];
        Material out = {
            .base_color = {1, 1, 1, 1},
            .emissive = {m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2], 0},
            .albedo_tex = VKMIN_NONE, .normal_tex = VKMIN_NONE, .mr_tex = VKMIN_NONE, .emissive_tex = VKMIN_NONE,
            .metallic = 1.0f, .roughness = 1.0f, .alpha_cutoff = 0.5f, .normal_scale = 1.0f,
        };
        const bool masked = m->alpha_mode == cgltf_alpha_mode_mask;
        const bool blend = m->alpha_mode == cgltf_alpha_mode_blend;
        if (masked) out.flags |= VKMIN_MAT_MASKED;
        if (blend) out.flags |= VKMIN_MAT_BLEND;
        if (m->double_sided) out.flags |= VKMIN_MAT_DOUBLE_SIDED;
        if (m->unlit) out.flags |= VKMIN_MAT_UNLIT;
        if (masked) out.alpha_cutoff = m->alpha_cutoff;
        if (m->has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness *p = &m->pbr_metallic_roughness;
            out.base_color = (vec4){p->base_color_factor[0], p->base_color_factor[1],
                                    p->base_color_factor[2], p->base_color_factor[3]};
            out.metallic = p->metallic_factor;
            out.roughness = p->roughness_factor;
            out.albedo_tex = tex_or_none(cook_texture(st, data, gltf_path, p->base_color_texture.texture,
                                                      (masked || blend) ? KIND_ALBEDO_ALPHA : KIND_ALBEDO));
            out.mr_tex = tex_or_none(cook_texture(st, data, gltf_path, p->metallic_roughness_texture.texture, KIND_DATA));
        }
        out.normal_tex = tex_or_none(cook_texture(st, data, gltf_path, m->normal_texture.texture, KIND_NORMAL));
        if (m->normal_texture.texture) out.normal_scale = m->normal_texture.scale;
        out.emissive_tex = tex_or_none(cook_texture(st, data, gltf_path, m->emissive_texture.texture, KIND_EMISSIVE));
        PUSH(st->materials, out);
    }
    /* A default material for primitives that have none. */
    PUSH(st->materials, ((Material){.base_color = {0.8f, 0.8f, 0.8f, 1}, .albedo_tex = VKMIN_NONE,
                                    .normal_tex = VKMIN_NONE, .mr_tex = VKMIN_NONE, .emissive_tex = VKMIN_NONE,
                                    .metallic = 0.0f, .roughness = 0.8f, .alpha_cutoff = 0.5f, .normal_scale = 1.0f}));
}

/* ------------------------------------------------------------- geometry --- */

typedef struct { float p[3], n[3], t[4], uv[2]; uint8_t joints[4]; float weights[4]; } raw_vertex;

/* Per-triangle tangents accumulated per vertex, for meshes that ship without.
 * Good enough for a normal map; the cooker warns when it has to do this. */
static void generate_tangents(raw_vertex *v, size_t vn, const uint32_t *idx, size_t in) {
    float *acc = calloc(vn * 3, sizeof(float));
    if (!acc) FATAL("out of memory");
    for (size_t i = 0; i + 2 < in; i += 3) {
        const raw_vertex *a = &v[idx[i]], *b = &v[idx[i + 1]], *c = &v[idx[i + 2]];
        const float e1[3] = {b->p[0] - a->p[0], b->p[1] - a->p[1], b->p[2] - a->p[2]};
        const float e2[3] = {c->p[0] - a->p[0], c->p[1] - a->p[1], c->p[2] - a->p[2]};
        const float du1 = b->uv[0] - a->uv[0], dv1 = b->uv[1] - a->uv[1];
        const float du2 = c->uv[0] - a->uv[0], dv2 = c->uv[1] - a->uv[1];
        const float det = du1 * dv2 - du2 * dv1;
        const float r = fabsf(det) > 1e-12f ? 1.0f / det : 0.0f;
        for (int k = 0; k < 3; ++k) {
            const float t = (e1[k] * dv2 - e2[k] * dv1) * r;
            acc[idx[i] * 3 + k] += t;
            acc[idx[i + 1] * 3 + k] += t;
            acc[idx[i + 2] * 3 + k] += t;
        }
    }
    for (size_t i = 0; i < vn; ++i) {
        float t[3] = {acc[i * 3], acc[i * 3 + 1], acc[i * 3 + 2]};
        const float *n = v[i].n;
        const float d = t[0] * n[0] + t[1] * n[1] + t[2] * n[2];
        for (int k = 0; k < 3; ++k) t[k] -= n[k] * d; /* Gram-Schmidt against the normal */
        const float len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (len > 1e-8f) {
            for (int k = 0; k < 3; ++k) v[i].t[k] = t[k] / len;
        } else {
            v[i].t[0] = 1; v[i].t[1] = 0; v[i].t[2] = 0; /* any perpendicular-ish axis */
        }
        v[i].t[3] = 1.0f;
    }
    free(acc);
}

static void transform_point(const float *m, const float *p, float *out) {
    for (int r = 0; r < 3; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

static void cook_primitive(cook_state *st, const cgltf_data *data, const cgltf_primitive *prim,
                           const float *world, bool skinned) {
    if (prim->type != cgltf_primitive_type_triangles || !prim->indices) {
        fprintf(stderr, "cook: skipping a non-indexed or non-triangle primitive\n");
        return;
    }
    const cgltf_accessor *pos = NULL, *nrm = NULL, *tan = NULL, *uv = NULL, *joints = NULL, *weights = NULL;
    for (size_t a = 0; a < prim->attributes_count; ++a) {
        const cgltf_attribute *at = &prim->attributes[a];
        if (at->type == cgltf_attribute_type_position) pos = at->data;
        if (at->type == cgltf_attribute_type_normal) nrm = at->data;
        if (at->type == cgltf_attribute_type_tangent) tan = at->data;
        if (at->type == cgltf_attribute_type_texcoord && at->index == 0) uv = at->data;
        if (at->type == cgltf_attribute_type_joints && at->index == 0) joints = at->data;
        if (at->type == cgltf_attribute_type_weights && at->index == 0) weights = at->data;
    }
    if (!pos || !nrm) FATAL("primitive without POSITION and NORMAL");
    const size_t vn = pos->count, in = prim->indices->count;
    raw_vertex *raw = calloc(vn, sizeof *raw);
    uint32_t *idx = malloc(in * sizeof *idx);
    if (!raw || !idx) FATAL("out of memory");
    for (size_t i = 0; i < vn; ++i) {
        cgltf_accessor_read_float(pos, i, raw[i].p, 3);
        cgltf_accessor_read_float(nrm, i, raw[i].n, 3);
        if (tan) cgltf_accessor_read_float(tan, i, raw[i].t, 4);
        if (uv) cgltf_accessor_read_float(uv, i, raw[i].uv, 2);
        if (joints) {
            cgltf_uint j[4] = {0};
            cgltf_accessor_read_uint(joints, i, j, 4);
            for (int k = 0; k < 4; ++k) raw[i].joints[k] = (uint8_t)j[k];
        }
        if (weights) cgltf_accessor_read_float(weights, i, raw[i].weights, 4);
    }
    for (size_t i = 0; i < in; ++i) idx[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
    if (!tan) {
        static bool warned;
        if (!warned) fprintf(stderr, "cook: generating tangents for a primitive without them\n");
        warned = true;
        generate_tangents(raw, vn, idx, in);
    }

    /* Bounds are computed in world space for static meshes, since instances
     * get the identity below; skinned meshes keep local space plus slack. */
    float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
    Mesh mesh = {.first_index = (uint32_t)st->indices.n, .index_count = (uint32_t)in,
                 .vertex_offset = (uint32_t)st->verts.n,
                 .skin_offset = skinned ? (uint32_t)st->skin.n : VKMIN_NONE};
    for (size_t i = 0; i < vn; ++i) {
        float p[3];
        if (skinned) memcpy(p, raw[i].p, sizeof p); else transform_point(world, raw[i].p, p);
        for (int k = 0; k < 3; ++k) { bmin[k] = fminf(bmin[k], p[k]); bmax[k] = fmaxf(bmax[k], p[k]); }
        float n[3], t[3];
        if (skinned) {
            memcpy(n, raw[i].n, sizeof n);
            memcpy(t, raw[i].t, sizeof t);
        } else { /* rotate normal/tangent by the world matrix (no shear assumed) */
            for (int r = 0; r < 3; ++r) {
                n[r] = world[r] * raw[i].n[0] + world[4 + r] * raw[i].n[1] + world[8 + r] * raw[i].n[2];
                t[r] = world[r] * raw[i].t[0] + world[4 + r] * raw[i].t[1] + world[8 + r] * raw[i].t[2];
            }
        }
        const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const float tl = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (nl > 0) for (int k = 0; k < 3; ++k) n[k] /= nl;
        if (tl > 0) for (int k = 0; k < 3; ++k) t[k] /= tl;
        Vertex v = {.px = p[0], .py = p[1], .pz = p[2], .normal = oct_encode(n[0], n[1], n[2]),
                    .tangent = pack_tangent(t[0], t[1], t[2], raw[i].t[3]),
                    .uv = pack_half2(raw[i].uv[0], raw[i].uv[1])};
        PUSH(st->verts, v);
        if (skinned) {
            float wsum = raw[i].weights[0] + raw[i].weights[1] + raw[i].weights[2] + raw[i].weights[3];
            if (wsum <= 0.0f) { wsum = 1.0f; raw[i].weights[0] = 1.0f; }
            SkinVertex sv = {0};
            for (int k = 0; k < 4; ++k) {
                sv.joints |= (uint32_t)raw[i].joints[k] << (8 * k);
                sv.weights |= (uint32_t)lroundf(raw[i].weights[k] / wsum * 255.0f) << (8 * k);
            }
            PUSH(st->skin, sv);
        }
    }
    for (size_t i = 0; i < in; ++i) PUSH(st->indices, idx[i]);
    const float centre[3] = {(bmin[0] + bmax[0]) * 0.5f, (bmin[1] + bmax[1]) * 0.5f, (bmin[2] + bmax[2]) * 0.5f};
    const float half[3] = {(bmax[0] - bmin[0]) * 0.5f, (bmax[1] - bmin[1]) * 0.5f, (bmax[2] - bmin[2]) * 0.5f};
    float radius = sqrtf(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
    if (skinned) radius *= 1.5f; /* animation moves limbs outside the rest pose */
    mesh.bounds = (vec4){centre[0], centre[1], centre[2], radius};
    if (!skinned) {
        for (int k = 0; k < 3; ++k) { st->bmin[k] = fminf(st->bmin[k], bmin[k]); st->bmax[k] = fmaxf(st->bmax[k], bmax[k]); }
    }
    const uint32_t mesh_index = (uint32_t)st->meshes.n;
    PUSH(st->meshes, mesh);

    vkm_node node = {.mesh = mesh_index, .skinned = skinned ? 1u : 0u,
                     .material = prim->material ? (uint32_t)cgltf_material_index(data, prim->material)
                                                : (uint32_t)(st->materials.n - 1)};
    /* Static geometry is baked into world space so the node transform is the
     * identity; skinned geometry keeps the mesh node's world transform. */
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memcpy(node.transform, skinned ? world : identity, sizeof node.transform);
    PUSH(st->nodes, node);
    free(raw);
    free(idx);
}

static void cook_node(cook_state *st, const cgltf_data *data, const cgltf_node *node) {
    float world[16];
    cgltf_node_transform_world(node, world);
    if (node->mesh) {
        for (size_t p = 0; p < node->mesh->primitives_count; ++p) {
            cook_primitive(st, data, &node->mesh->primitives[p], world, node->skin != NULL);
        }
    }
    for (size_t i = 0; i < node->children_count; ++i) cook_node(st, data, node->children[i]);
}

/* ---------------------------------------------------------- skin & anim --- */

static int joint_of_node(const cgltf_skin *skin, const cgltf_node *node) {
    for (size_t j = 0; j < skin->joints_count; ++j) {
        if (skin->joints[j] == node) return (int)j;
    }
    return -1;
}

static void cook_skin(cook_state *st, const cgltf_data *data) {
    if (data->skins_count == 0) return;
    if (data->skins_count > 1) fprintf(stderr, "cook: %zu skins; only the first is cooked\n", data->skins_count);
    const cgltf_skin *skin = &data->skins[0];
    /* Joint globals are built from the joint hierarchy alone at runtime, so
     * whatever sits above the root joint (an armature node, an axis fix-up)
     * has to travel with the file. */
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memcpy(st->skin_parent, identity, sizeof identity);
    for (size_t j = 0; j < skin->joints_count; ++j) {
        const cgltf_node *n = skin->joints[j];
        if (joint_of_node(skin, n->parent) < 0 && n->parent) cgltf_node_transform_world(n->parent, st->skin_parent);
        vkm_joint out = {.parent = n->parent ? joint_of_node(skin, n->parent) : -1,
                         .rest_t = {0, 0, 0}, .rest_r = {0, 0, 0, 1}, .rest_s = {1, 1, 1}};
        if (skin->inverse_bind_matrices) {
            cgltf_accessor_read_float(skin->inverse_bind_matrices, j, out.inverse_bind, 16);
        } else {
            memcpy(out.inverse_bind, identity, sizeof identity);
        }
        if (n->has_matrix) fprintf(stderr, "cook: joint %zu uses a matrix, not TRS; rest pose will be identity\n", j);
        if (n->has_translation) memcpy(out.rest_t, n->translation, sizeof out.rest_t);
        if (n->has_rotation) memcpy(out.rest_r, n->rotation, sizeof out.rest_r);
        if (n->has_scale) memcpy(out.rest_s, n->scale, sizeof out.rest_s);
        PUSH(st->joints, out);
    }
    if (data->animations_count == 0) return;
    const cgltf_animation *anim = &data->animations[0];
    for (size_t c = 0; c < anim->channels_count; ++c) {
        const cgltf_animation_channel *ch = &anim->channels[c];
        const int joint = joint_of_node(skin, ch->target_node);
        if (joint < 0) {
            fprintf(stderr, "cook: animation channel %zu targets a non-joint node; dropped\n", c);
            continue;
        }
        uint32_t path;
        if (ch->target_path == cgltf_animation_path_type_translation) path = VKM_PATH_TRANSLATION;
        else if (ch->target_path == cgltf_animation_path_type_rotation) path = VKM_PATH_ROTATION;
        else if (ch->target_path == cgltf_animation_path_type_scale) path = VKM_PATH_SCALE;
        else continue;
        if (ch->sampler->interpolation != cgltf_interpolation_type_linear) {
            fprintf(stderr, "cook: channel %zu is not linear; sampled as linear anyway\n", c);
        }
        vkm_channel out = {.joint = (uint32_t)joint, .path = path, .first_key = (uint32_t)st->keys.n,
                           .key_count = (uint32_t)ch->sampler->input->count};
        for (size_t k = 0; k < ch->sampler->input->count; ++k) {
            vkm_key key = {0};
            cgltf_accessor_read_float(ch->sampler->input, k, &key.time, 1);
            cgltf_accessor_read_float(ch->sampler->output, k, key.value, path == VKM_PATH_ROTATION ? 4 : 3);
            if (key.time > st->anim_duration) st->anim_duration = key.time;
            PUSH(st->keys, key);
        }
        PUSH(st->channels, out);
    }
}

/* --------------------------------------------------------------- output --- */

static void write_all(FILE *f, const void *p, size_t bytes) {
    if (bytes && fwrite(p, 1, bytes, f) != bytes) FATAL("write failed");
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: cook <scene.gltf|glb> <outdir> [--max-size N]\n");
        return 2;
    }
    cook_state st = {.out_dir = argv[2], .max_size = 512,
                     .bmin = {1e30f, 1e30f, 1e30f}, .bmax = {-1e30f, -1e30f, -1e30f},
                     .skin_parent = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--max-size") == 0 && i + 1 < argc) st.max_size = atoi(argv[++i]);
    }
    const cgltf_options options = {0};
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&options, argv[1], &data) != cgltf_result_success) FATAL("cannot parse %s", argv[1]);
    if (cgltf_load_buffers(&options, data, argv[1]) != cgltf_result_success) FATAL("cannot load buffers for %s", argv[1]);
    st.image_texture = malloc(sizeof(int) * (data->images_count + 1));
    st.image_kind = malloc(sizeof(int) * (data->images_count + 1));
    if (!st.image_texture || !st.image_kind) FATAL("out of memory");
    for (size_t i = 0; i < data->images_count; ++i) { st.image_texture[i] = -1; st.image_kind[i] = -1; }

    printf("cook: %s -> %s (textures capped at %d)\n", argv[1], st.out_dir, st.max_size);
    cook_materials(&st, data, argv[1]);
    const cgltf_scene *scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : NULL);
    if (!scene) FATAL("no scene in %s", argv[1]);
    for (size_t i = 0; i < scene->nodes_count; ++i) cook_node(&st, data, scene->nodes[i]);
    cook_skin(&st, data);

    vkm_header h = {
        .magic = VKM_MAGIC, .version = VKM_VERSION,
        .vertex_count = (uint32_t)st.verts.n, .index_count = (uint32_t)st.indices.n,
        .skin_vertex_count = (uint32_t)st.skin.n, .mesh_count = (uint32_t)st.meshes.n,
        .material_count = (uint32_t)st.materials.n, .texture_count = (uint32_t)st.textures.n,
        .node_count = (uint32_t)st.nodes.n, .joint_count = (uint32_t)st.joints.n,
        .channel_count = (uint32_t)st.channels.n, .key_count = (uint32_t)st.keys.n,
        .anim_duration = st.anim_duration,
    };
    memcpy(h.skin_parent, st.skin_parent, sizeof h.skin_parent);
    memcpy(h.bounds_min, st.bmin, sizeof h.bounds_min);
    memcpy(h.bounds_max, st.bmax, sizeof h.bounds_max);
    char path[1024];
    snprintf(path, sizeof path, "%s/scene.vkm", st.out_dir);
    FILE *f = fopen(path, "wb");
    if (!f) FATAL("cannot write %s", path);
    write_all(f, &h, sizeof h);
    write_all(f, st.verts.v, st.verts.n * sizeof(Vertex));
    write_all(f, st.indices.v, st.indices.n * sizeof(uint32_t));
    write_all(f, st.skin.v, st.skin.n * sizeof(SkinVertex));
    write_all(f, st.meshes.v, st.meshes.n * sizeof(Mesh));
    write_all(f, st.materials.v, st.materials.n * sizeof(Material));
    write_all(f, st.textures.v, st.textures.n * sizeof(vkm_texture));
    write_all(f, st.nodes.v, st.nodes.n * sizeof(vkm_node));
    write_all(f, st.joints.v, st.joints.n * sizeof(vkm_joint));
    write_all(f, st.channels.v, st.channels.n * sizeof(vkm_channel));
    write_all(f, st.keys.v, st.keys.n * sizeof(vkm_key));
    fclose(f);
    printf("cook: %u vertices, %u indices, %u meshes, %u materials, %u textures, %u nodes, "
           "%u joints, %u channels (%.2fs)\n",
           h.vertex_count, h.index_count, h.mesh_count, h.material_count, h.texture_count,
           h.node_count, h.joint_count, h.channel_count, (double)h.anim_duration);
    printf("cook: bounds [%.2f %.2f %.2f] .. [%.2f %.2f %.2f]\n", (double)h.bounds_min[0],
           (double)h.bounds_min[1], (double)h.bounds_min[2], (double)h.bounds_max[0],
           (double)h.bounds_max[1], (double)h.bounds_max[2]);
    cgltf_free(data);
    return 0;
}
