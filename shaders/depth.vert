#version 450
// depth.vert -- depth-only vertex shader for the shadow atlas and the prepass.
// push.view selects which View this draw renders into.
#include "common.glsl"
#include "scene_vertex.glsl"
layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out uint v_material;
layout(location = 2) flat out uint v_instance;

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Instance inst = InstanceRef(frame.instances).i[gl_InstanceIndex];
    FetchedVertex v = fetch_vertex(frame, inst, uint(gl_VertexIndex));
    View view = ViewRef(frame.views).v[push.view];
    gl_Position = view.view_proj * vec4(v.world_pos, 1.0);
    v_uv = v.uv;
    v_material = inst.material;
    v_instance = uint(gl_InstanceIndex);
}
