// quad.frag -- textured, tinted, premultiplied. With VKMIN_QUAD_SDF the red
// channel is a distance field: 128 is the edge, so text stays crisp at any
// size; the softness is in texels of the atlas, converted through the
// screen-space derivative so it is one pixel wide however the quad is scaled.
#include "common.glsl"
#include "lib/outdoor.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in uint v_texture;
layout(location = 3) flat in uint v_flags;
layout(location = 4) in float v_softness;
layout(location = 5) in vec3 v_world;
layout(location = 6) flat in float v_coverage;
layout(location = 0) out vec4 o_color;
// The world-quad pipeline masks these off, but an undeclared output leaves
// whatever the slot held (lavapipe stippled the normal target with garbage
// until they were declared); declared and written, the mask does its job.
#ifndef VKMIN_SCREEN_OUTPUT
layout(location = 1) out uint o_id;
layout(location = 2) out vec2 o_normal;
#endif

void main() {
    vec4 t = texture(TEX(v_texture), v_uv);
    float a;
    vec3 rgb;
    if ((v_flags & VKMIN_QUAD_SDF) != 0u) {
        float d = t.r - 0.5;
        float w = fwidth(t.r) * v_softness;
        a = smoothstep(-w, w, d) * v_color.a;
        rgb = v_color.rgb;
    } else {
        a = t.a * v_color.a;
        rgb = t.rgb * v_color.rgb;
    }
    if ((v_flags & VKMIN_QUAD_MASKED) != 0u && a < 0.5) discard;
    if ((v_flags & VKMIN_QUAD_MASKED) != 0u) {
        if (coverage_noise(gl_FragCoord.xy) < 1.0-v_coverage) discard;
        a = 1.0;
    }
    Frame f = FrameRef(push.frame).frame;
    if (f.outdoor != uint64_t(0) && (v_flags & VKMIN_QUAD_SCREEN) == 0u) {
        Outdoor o = OutdoorRef(f.outdoor).o;
        if ((v_flags & VKMIN_QUAD_MASKED) != 0u) {
            vec3 light = sky_radiance(vec3(0,1,0),f.sun.xyz)*0.6;
            if (f.sun_light != VKMIN_NONE) light += LightRef(f.lights).l[f.sun_light].color.rgb*max(f.sun.y,0.0)*0.35;
            rgb *= light*cloud_shadow(o,v_world,f.sun.xyz,f.frame_index);
        }
        rgb = aerial(o,rgb,v_world,f.camera_pos.xyz,f.sun.xyz);
    }
    o_color = vec4(rgb * a, a);
#ifndef VKMIN_SCREEN_OUTPUT
    o_id = 0u;
    o_normal = vec2(0.5);
#endif
}
