#include "lib/outdoor.glsl"
// scene_vertex.glsl -- the one vertex fetch, shared by the depth-only and
// forward vertex shaders. Pulls the packed vertex by device address, applies
// skinning when the instance says so, and returns world-space attributes.
struct FetchedVertex {
    vec3 world_pos;
    vec3 normal;
    vec4 tangent;
    vec2 uv;
};

// TODO(v0.2): compute pre-skinning into a transient buffer once the shadow
// views and the prepass are both re-skinning the same character; build it as a
// parallel implementation when the timestamps say it matters.
FetchedVertex fetch_vertex(Frame frame, Instance inst, uint vertex_index) {
    Vertex v = VertexRef(frame.vertices).v[vertex_index];
    vec3 pos = vec3(v.px, v.py, v.pz);
    vec3 nrm = oct_decode(v.normal);
    vec4 tan = tangent_decode(v.tangent);
    mat4 model = inst.transform;
    if ((inst.flags & VKMIN_INST_SKINNED) != 0u) {
        Mesh mesh = MeshRef(frame.meshes).m[inst.mesh];
        SkinVertex sv = SkinVertexRef(frame.skin_vertices).v[vertex_index - mesh.vertex_offset + mesh.skin_offset];
        vec4 w = rgba8_decode(sv.weights);
        BoneRef bones = BoneRef(frame.bones);
        mat4 skin = mat4(0.0);
        for (int k = 0; k < 4; ++k) {
            uint joint = (sv.joints >> (8u * uint(k))) & 0xffu;
            skin += bones.m[inst.bone_offset + joint] * w[k];
        }
        model = model * skin;
    }
    FetchedVertex out_v;
    out_v.world_pos = (model * vec4(pos, 1.0)).xyz;
    if (frame.outdoor != uint64_t(0) && (inst.flags & (VKMIN_INST_GRASS | VKMIN_INST_LEAF)) != 0u) {
        Outdoor o = OutdoorRef(frame.outdoor).o;
        vec3 root = inst.transform[3].xyz;
        float weight = (inst.flags & VKMIN_INST_GRASS) != 0u ? clamp(pos.y,0.0,1.0) : clamp(pos.y/9.0,0.0,1.0)*0.3;
        vec3 weatherPosition = root + vec3(float(inst.pad0 & 255u)*0.1,0,0);
        out_v.world_pos += wind(weatherPosition,frame.frame_index)*weight*weight*o.weather.y;
        if ((inst.flags & VKMIN_INST_GRASS) != 0u) out_v.world_pos = mix(root,out_v.world_pos,grass_fade(frame,root));
    }
    mat3 nm = mat3(model); // no non-uniform scale in this codebase's assets
    out_v.normal = normalize(nm * nrm);
    out_v.tangent = vec4(normalize(nm * tan.xyz), tan.w);
    out_v.uv = uv_decode(v.uv);
    return out_v;
}
