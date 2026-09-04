#version 450
// overlay.vert -- six vertices per OverlayQuad, read by device address.
#include "common.glsl"
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main() {
    Frame frame = FrameRef(push.frame).frame;
    OverlayQuad q = OverlayRef(push.aux).q[gl_VertexIndex / 6u];
    uint corner = uint(gl_VertexIndex) % 6u;
    // 0,1,2 then 2,1,3 over the quad's corners: (0,0) (1,0) (0,1) (1,1)
    uint c = corner < 3u ? corner : (corner == 3u ? 2u : corner == 4u ? 1u : 3u);
    vec2 t = vec2(float(c & 1u), float(c >> 1u));
    vec2 px = mix(q.rect.xy, q.rect.zw, t);
    gl_Position = vec4(px * frame.screen.zw * 2.0 - 1.0, 0.0, 1.0);
    v_uv = mix(q.uv.xy, q.uv.zw, t);
    v_color = rgba8_decode(q.color);
}
