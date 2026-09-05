/* mat4_test -- the demo's maths is pure, so testing it is build inputs, check
 * outputs, and needs no GPU. Written because this is exactly the finicky code
 * that silently produces a plausible-looking wrong picture. */
#include "vkmin_math.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

static void check(bool ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "mat4_test: FAIL %s\n", what);
        ++failures;
    }
}

static bool close_enough(float a, float b) {
    const float d = a - b;
    return (d < 0.0f ? -d : d) < 1e-5f;
}

/* Applies a matrix to a point and divides through by w, which is what the
 * vertex shader effectively does. */
static vec3 project(mat4 m, vec3 p) {
    float out[4];
    for (int row = 0; row < 4; ++row) {
        out[row] = m.m[0 * 4 + row] * p.x + m.m[1 * 4 + row] * p.y + m.m[2 * 4 + row] * p.z +
                   m.m[3 * 4 + row];
    }
    const float inv = out[3] != 0.0f ? 1.0f / out[3] : 0.0f;
    return (vec3){out[0] * inv, out[1] * inv, out[2] * inv};
}

int main(void) {
    const mat4 id = vkmin_mat4_identity();
    const mat4 rot = vkmin_mat4_rotate_y(0.7f);
    const mat4 through_identity = vkmin_mat4_mul(rot, id);
    for (int i = 0; i < 16; ++i) {
        check(close_enough(through_identity.m[i], rot.m[i]), "multiplying by identity changes a matrix");
    }

    /* Rotating a point 90 degrees about Y must send +X to -Z. */
    const vec3 spun = project(vkmin_mat4_rotate_y(3.14159265f * 0.5f), (vec3){1.0f, 0.0f, 0.0f});
    check(close_enough(spun.x, 0.0f) && close_enough(spun.y, 0.0f) && close_enough(spun.z, -1.0f),
          "rotate_y(90 degrees) does not send +X to -Z");

    /* The camera transform must put the eye at the origin looking down -Z. */
    const mat4 view = vkmin_mat4_look_at((vec3){0.0f, 0.0f, 4.0f}, (vec3){0.0f, 0.0f, 0.0f},
                                   (vec3){0.0f, 1.0f, 0.0f});
    const vec3 eye_in_view = project(view, (vec3){0.0f, 0.0f, 4.0f});
    check(close_enough(eye_in_view.x, 0.0f) && close_enough(eye_in_view.y, 0.0f) &&
              close_enough(eye_in_view.z, 0.0f),
          "look_at does not map the eye to the origin");
    const vec3 target_in_view = project(view, (vec3){0.0f, 0.0f, 0.0f});
    check(close_enough(target_in_view.z, -4.0f), "look_at does not look down -Z");

    /* Vulkan depth runs 0 at the near plane to 1 at the far plane, and the
     * projection carries the Y flip, so +Y in view space must come out below
     * the centre of the framebuffer. */
    const mat4 proj = vkmin_mat4_perspective(1.0f, 1.0f, 0.5f, 10.0f);
    const vec3 near_point = project(proj, (vec3){0.0f, 0.0f, -0.5f});
    const vec3 far_point = project(proj, (vec3){0.0f, 0.0f, -10.0f});
    check(close_enough(near_point.z, 0.0f), "near plane does not map to depth 0");
    check(close_enough(far_point.z, 1.0f), "far plane does not map to depth 1");
    const vec3 up_point = project(proj, (vec3){0.0f, 1.0f, -2.0f});
    check(up_point.y < 0.0f, "projection does not flip Y for Vulkan's framebuffer");

    if (failures == 0) printf("mat4_test: ok\n");
    /* cameras and rays: a pixel at the centre of an fps view looks along the
     * camera's forward axis, and hits a box placed there. */
    {
        const vkmin_camera cam = vkmin_camera_fps((vec3){0, 1, 5}, 0.0f, 0.0f, 1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        const vkmin_ray ray = vkmin_ray_from_pixel(cam.view, cam.proj, 640.0f, 360.0f, 1280.0f, 720.0f);
        check(close_enough(ray.dir.x, 0.0f) && close_enough(ray.dir.y, 0.0f) && ray.dir.z < -0.999f, "centre ray is not the forward axis");
        float t = 0.0f;
        /* the ray starts on the near plane, 0.1 in front of the eye */
        check(vkmin_ray_aabb(ray, (vec3){-1, 0, -1}, (vec3){1, 2, 1}, &t) && t > 3.85f && t < 3.95f, "ray misses the box in front of it");
        check(!vkmin_ray_aabb(ray, (vec3){3, 0, -1}, (vec3){5, 2, 1}, &t), "ray hits a box beside it");
        check(vkmin_ray_triangle(ray, (vec3){-1, 0, 0}, (vec3){1, 0, 0}, (vec3){0, 3, 0}, &t) && t > 4.85f && t < 4.95f, "ray misses the triangle");
        check(!vkmin_ray_triangle(ray, (vec3){-1, 0, 10}, (vec3){1, 0, 10}, (vec3){0, 3, 10}, &t), "ray hits a triangle behind it");
        const vkmin_camera top = vkmin_camera_ortho_topdown((vec3){0, 0, 0}, 10.0f, 1.0f, 50.0f);
        const vkmin_ray down = vkmin_ray_from_pixel(top.view, top.proj, 100.0f, 100.0f, 200.0f, 200.0f);
        check(down.dir.y < -0.999f, "top-down ray does not point down");
        const vkmin_tick tk = vkmin_ticks_for_frame(3600, 30);
        check(tk.ticks == 1800 && tk.alpha == 0.0f, "3600 frames at 30 Hz is not 1800 ticks");
        check(vkmin_ticks_for_frame(1, 30).ticks == 0 && close_enough(vkmin_ticks_for_frame(1, 30).alpha, 0.5f), "half a tick is not alpha 0.5");
    }
    return failures == 0 ? 0 : 1;
}
