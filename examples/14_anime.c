/* 14_anime -- the platformer's character and a small scene under the cel()
 * shader: a four-step ramp, a tinted shadow colour, a rim light, one stepped
 * highlight, the screen-space outline pass and a colour LUT, from a camera
 * that orbits on its own and follows a mouse drag.
 *
 * Cel shading is unforgiving of small lighting differences, which makes its
 * golden images unusually good regression detectors; that is the point. */
#include "play.h"
#include "shaders.h"

enum { HZ = 60, PILLARS = 8, MAX_QUADS = 128 };

/* Four steps over N.L, with a wide lit band: the classic look. */
static uint32_t ramp_texture(vkmin_ctx *gpu) {
    const uint8_t steps[16] = {30, 30, 30, 30, 30, 30, 30, 110, 110, 110, 200, 200, 200, 255, 255, 255};
    uint32_t px[16];
    for (int i = 0; i < 16; ++i) px[i] = gk_rgba(steps[i] / 255.0f, steps[i] / 255.0f, steps[i] / 255.0f, 1.0f);
    const vkmin_image img = vkmin_make_image(gpu, &(vkmin_image_desc){.width = 16, .height = 1, .format = VKMIN_FMT_RGBA8_UNORM,
                                                                      .usage = VKMIN_IMAGE_SAMPLED, .sampler = VKMIN_SAMPLER_NEAREST_CLAMP, .label = "anime.ramp"});
    vkmin_image_upload(gpu, img, 0, VKMIN_BYTES(px));
    return vkmin_index(gpu, img);
}

static const char *usage = "usage: 14_anime [--profile lavapipe] [vkmin flags] [cvar=value ...]\n  drag with the left button to turn the camera\n";

int main(int argc, char **argv) {
    const gk_options opt = gk_parse(argc, argv, usage);
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "14_anime", .device_index = opt.device});
    int width = 0, height = 0;
    vkmin_size(gpu, &width, &height);
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = width, .height = height, .shadow_atlas = cvar_get_int(CV_r_shadow_atlas),
                                       .max_vertices = 16384, .max_indices = 65536, .max_skin_vertices = 8192, .max_meshes = 16,
                                       .max_materials = 8, .max_instances = 32, .fs = VKMIN_BYTES(lit_cel_frag_spv)});
    const gk_shapes shapes = gk_upload_shapes(r);
    const scene hero = scene_load("assets/cesium/scene.vkm");
    uint32_t hero_mat0 = 0;
    const uint32_t hero_mesh0 = gk_load_scene(r, gpu, &hero, &hero_mat0);
    const Material mats[3] = {gk_material(0.85f, 0.80f, 0.72f, 0.0f, 0.9f, 0), gk_material(0.90f, 0.35f, 0.30f, 0.0f, 0.4f, 0),
                              gk_material(0.35f, 0.55f, 0.90f, 0.0f, 0.4f, 0)};
    const uint32_t mat0 = vkr_upload_materials(r, mats, 3);
    const uint32_t lut = gk_grade(gpu, .12f, 1.15f), ramp = ramp_texture(gpu), disc = gk_disc_texture(gpu, 32);

    Instance instances[PILLARS + 4];
    Quad quads[MAX_QUADS];
    mat4 bones[ANIM_MAX_JOINTS];
    float yaw_offset = 0.0f, prev_mouse_x = 0.0f;
    bool dragging = false, help = true;
    uint32_t origin = 0;
    FILE *trace = gk_trace_open(argc, argv, "tick animation yaw");
    while (vkmin_running(gpu)) {
        const vkmin_frame fr = vkmin_frame_begin(gpu, NULL); /* the one read of the outside world */
        const vkmin_inputs *in = &fr.input;
        const uint32_t frame = fr.index;
        width = fr.width;
        height = fr.height;
        if (vkmin_key_pressed(in, VKMIN_KEY_ESCAPE)) { vkmin_frame_end(gpu); break; }
        if (vkmin_key_pressed(in, VKMIN_KEY_F1)) help = !help;
        if (vkmin_key_pressed(in, 'R')) { origin = frame; yaw_offset = 0; dragging = false; }
        if (dragging && (in->buttons & VKMIN_MOUSE_LEFT)) yaw_offset += (in->mouse_x - prev_mouse_x) * 0.01f;
        dragging = (in->buttons & VKMIN_MOUSE_LEFT) != 0u;
        prev_mouse_x = in->mouse_x;
        const uint32_t tick = vkmin_ticks_for_frame(frame - origin, HZ).ticks;
        const float t = (float)tick / HZ;

        /* --- the scene: a floor, a pedestal, a ring of pillars, the hero --- */
        uint32_t n = 0;
        mat4 m = vkmin_mat4_mul(vkmin_mat4_translate((vec3){0, -0.5f, 0}), vkmin_mat4_scale((vec3){9, 0.5f, 9}));
        instances[n++] = (Instance){.transform = m, .prev_transform = m, .bounds = gk_world_bounds(m, (vec4){0, 0, 0, 1.7321f}), .mesh = shapes.cube,
                                    .material = mat0, .bone_offset = VKMIN_NONE, .id = 1};
        m = vkmin_mat4_mul(vkmin_mat4_translate((vec3){0, 0.3f, 0}), vkmin_mat4_scale((vec3){1.4f, 0.3f, 1.4f}));
        instances[n++] = (Instance){.transform = m, .prev_transform = m, .bounds = gk_world_bounds(m, (vec4){0, 0, 0, 1.7321f}), .mesh = shapes.cube,
                                    .material = mat0 + 2, .bone_offset = VKMIN_NONE, .id = 2};
        for (uint32_t i = 0; i < PILLARS; ++i) {
            const float a = 6.2831853f * (float)i / PILLARS, h = 1.2f + 0.8f * gk_hash(61, i);
            m = vkmin_mat4_mul(vkmin_mat4_translate((vec3){5.5f * cosf(a), h, 5.5f * sinf(a)}), vkmin_mat4_scale((vec3){0.45f, h, 0.45f}));
            instances[n++] = (Instance){.transform = m, .prev_transform = m, .bounds = gk_world_bounds(m, (vec4){0, 0, 0, 1.7321f}),
                                        .mesh = (i & 1) ? shapes.sphere : shapes.cube, .material = mat0 + 1 + (i & 1), .bone_offset = VKMIN_NONE, .id = 10 + i};
        }
        const mat4 placement = vkmin_mat4_mul(vkmin_mat4_translate((vec3){0, 0.6f, 0}), vkmin_mat4_mul(vkmin_mat4_rotate_y(t * 0.3f), vkmin_mat4_scale((vec3){2.2f, 2.2f, 2.2f})));
        instances[n] = gk_scene_instance(&hero, 0, hero_mesh0, hero_mat0, placement);
        instances[n].id = 100;
        const uint32_t animation = (tick / 180) % 3;
        const uint32_t bone_count = anim_character(&hero, t, animation, (float)(tick % 48) / HZ, gk_node_transform(&hero, 0), bones, ANIM_MAX_JOINTS);
        n++;

        const float yaw = t * 0.25f + yaw_offset;
        const vkmin_camera cam = vkmin_camera_rts((vec3){0, 1.8f, 0}, 6.5f, 0.3f, yaw, 0.9f, (float)width / (float)height, 0.1f, 60.0f);
        uint32_t nq = 0;
        for (uint32_t i = 0; i < 14; ++i) {
            const float a = t*.3f + (float)i*.4488f, y = .7f + 3*gk_hash(67, i);
            quads[nq++] = (Quad){.pos = {2.5f*cosf(a), y, 2.5f*sinf(a), a}, .size_uv0 = {.05f,.05f,0,0},
                .uv1 = {1,1,0,0}, .texture = disc, .color = 0xa0d4eaffu, .flags = VKMIN_QUAD_BILLBOARD};
        }
        const char *states[] = {"Idle / breathing pose", "Run / imported clip", "Jump / authored pose track"};
        nq += gk_card(r, width, "PORCELAIN / cel lighting", states[animation], "Drag to orbit  R restart  F1 help", help, quads + nq, MAX_QUADS - nq);
        const uint32_t words[] = {tick, animation, gk_float_bits(yaw_offset)};
        gk_trace(trace, frame, words, sizeof words / sizeof words[0]);
        Light lights[2] = {gk_sun((vec3){-0.5f, -1.0f, -0.4f}, 1.4f), gk_point_light((vec3){3.0f * cosf(t), 2.5f, 3.0f * sinf(t)}, 6.0f, (vec3){1.0f, 0.5f, 0.8f}, 4.0f)};
        vkr_frame(r, &(vkr_frame_desc){.view = cam.view, .proj = cam.proj, .camera_pos = {cam.pos.x, cam.pos.y, cam.pos.z, 1},
                                       .near = 0.1f, .far = 60.0f, .instances = instances, .instance_count = n, .lights = lights, .light_count = 2,
                                       .bones = bones, .bone_count = bone_count, .quads = quads, .quad_count = nq, .frame = fr,
                                       .look = {.cel_ramp_tex = ramp, .rim_strength = 0.45f, .rim_power = 4.0f, .spec_step = 0.5f,
                                                .shadow_tint = {0.55f, 0.45f, 0.75f}, .outline = 1.0f, .outline_depth = 0.03f,
                                                .lut_tex = lut, .lut_strength = 0.8f}});
        vkmin_frame_end(gpu);
    }
    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    scene_free((scene *)&hero);
    if (trace) fclose(trace);
    return 0;
}
