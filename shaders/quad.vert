#version 450
// quad.vert -- six vertices per Quad, read by device address from push.aux.
// The flags pick the space: screen pixels, a camera-facing billboard, a quad
// lying on the ground, or (default) one standing in the XY plane. Depth is
// the real depth for world quads so they sort against the scene.
#include "common.glsl"
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) flat out uint v_texture;
layout(location = 3) flat out uint v_flags;
layout(location = 4) out float v_softness;

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Quad q = QuadRef(push.aux).q[push.param + gl_VertexIndex / 6u];
    uint corner = uint(gl_VertexIndex) % 6u;
    // 0,1,2 then 2,1,3 over the corners (0,0) (1,0) (0,1) (1,1)
    uint c = corner < 3u ? corner : (corner == 3u ? 2u : corner == 4u ? 1u : 3u);
    vec2 t = vec2(float(c & 1u), float(c >> 1u));
    vec2 local = (t - 0.5) * q.size_uv0.xy;                 // centred, +y up in world, +y down on screen
    float cs = cos(q.pos.w), sn = sin(q.pos.w);
    vec2 r = vec2(local.x * cs - local.y * sn, local.x * sn + local.y * cs);
    if ((q.flags & VKMIN_QUAD_SCREEN) != 0u) {
        vec2 px = q.pos.xy + r;
        gl_Position = vec4(px * frame.screen.zw * 2.0 - 1.0, 0.0, 1.0);
    } else {
        vec3 world;
        if ((q.flags & VKMIN_QUAD_BILLBOARD) != 0u) {
            vec3 right = vec3(frame.view[0][0], frame.view[1][0], frame.view[2][0]);
            vec3 up = vec3(frame.view[0][1], frame.view[1][1], frame.view[2][1]);
            world = q.pos.xyz + right * r.x - up * r.y;
        } else if ((q.flags & VKMIN_QUAD_GROUND) != 0u) {
            world = q.pos.xyz + vec3(r.x, 0.0, r.y);
        } else {
            world = q.pos.xyz + vec3(r.x, -r.y, 0.0);
        }
        gl_Position = frame.view_proj * vec4(world, 1.0);
    }
    v_uv = mix(q.size_uv0.zw, q.uv1.xy, t);
    v_color = rgba8_decode(q.color);
    v_texture = q.texture;
    v_flags = q.flags;
    v_softness = q.uv1.z > 0.0 ? q.uv1.z : 1.0;
}
