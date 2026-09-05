#version 450
// scene.vert -- forward pass vertex shader. View 0 is always the camera.
#include "common.glsl"
#include "scene_vertex.glsl"
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_tangent;
layout(location = 3) out vec2 v_uv;
layout(location = 4) flat out uint v_instance;

void main() {
    Frame frame = FrameRef(push.frame).frame;
    Instance inst = InstanceRef(frame.instances).i[gl_InstanceIndex];
    bool grass = (inst.flags & VKMIN_INST_GRASS) != 0u;
    bool skip = (push.param2 == 1u && grass) || (push.param2 == 2u && !grass);
    if (skip) {
        gl_Position = vec4(2,2,2,1); v_world_pos = vec3(0); v_normal = vec3(0,1,0);
        v_tangent = vec4(1,0,0,1); v_uv = vec2(0); v_instance = uint(gl_InstanceIndex); return;
    }
    FetchedVertex v = fetch_vertex(frame, inst, uint(gl_VertexIndex));
    gl_Position = frame.view_proj * vec4(v.world_pos, 1.0);
    v_world_pos = v.world_pos;
    v_normal = v.normal;
    v_tangent = v.tangent;
    v_uv = v.uv;
    v_instance = uint(gl_InstanceIndex);
}
