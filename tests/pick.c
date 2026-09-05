/* pick -- the ID target and vkmin_pick, end to end: one cube with id 42 in
 * the middle of a 64x64 frame; the centre texel reads 42, a corner reads 0,
 * and a texel off the image reads 0 without complaint. No golden needed. */
#include "gamekit.h"

int main(int argc, char **argv) {
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "pick", .width = 64, .height = 64, .headless = true});
    vkr *r = vkr_init(gpu, &(vkr_desc){.width = 64, .height = 64, .shadow_atlas = 256, .max_vertices = 4096, .max_indices = 8192,
                                       .max_meshes = 8, .max_materials = 4, .max_instances = 4});
    const gk_shapes shapes = gk_upload_shapes(r);
    const Material m = gk_material(1, 0.5f, 0.2f, 0, 0.5f, 0);
    const uint32_t mat = vkr_upload_materials(r, &m, 1);
    const mat4 t = vkmin_mat4_identity();
    const Instance inst = {.transform = t, .prev_transform = t, .bounds = {0, 0, 0, 1.7321f}, .mesh = shapes.cube, .material = mat,
                           .bone_offset = VKMIN_NONE, .id = 42};
    const vkmin_camera cam = vkmin_camera_fps((vec3){0, 0, 6}, 0.0f, 0.0f, 1.0f, 1.0f, 0.1f, 50.0f);
    const Light sun = gk_sun((vec3){0.3f, -1, 0.2f}, 3.0f);
    int failures = 0;
    for (int frame = 0; frame < 2 && vkmin_frame_begin(gpu, NULL); ++frame) {
        vkr_frame(r, &(vkr_frame_desc){.view = cam.view, .proj = cam.proj, .camera_pos = {0, 0, 6, 1}, .near = 0.1f, .far = 50.0f,
                                       .instances = &inst, .instance_count = 1, .lights = &sun, .light_count = 1, .frame_index = (uint32_t)frame});
        vkmin_frame_end(gpu);
        const uint32_t centre = vkmin_pick(gpu, vkr_id_target(r), 32, 32), corner = vkmin_pick(gpu, vkr_id_target(r), 1, 1);
        const uint32_t off = vkmin_pick(gpu, vkr_id_target(r), 500, -3);
        if (centre != 42 || corner != 0 || off != 0) {
            fprintf(stderr, "pick: frame %d: centre %u (want 42), corner %u (want 0), off-image %u (want 0)\n", frame, centre, corner, off);
            ++failures;
        }
    }
    vkr_shutdown(r);
    vkmin_shutdown(gpu);
    if (!failures) puts("pick: ok");
    return failures ? 1 : 0;
}
