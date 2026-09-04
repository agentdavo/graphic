// lib/surface.glsl -- the material at a pixel, resolved once. Every canonical
// fragment shader starts here; a user shader may skip it and light anything.
struct Surface {
    vec3 albedo;
    float alpha;
    vec3 N;          // shading normal, normal-mapped when the material has one
    float metallic;
    float roughness;
    vec3 emissive;
    uint flags;      // VKMIN_MAT_*
};

// Always samples every map (the 1x1 defaults stand in for missing ones); the
// flags decide what is used, so the cost is constant and the branch is on data.
Surface surface_fetch(Frame frame, Material m, vec3 v_normal, vec4 v_tangent, vec2 uv, bool front_facing) {
    Surface s;
    uint albedo_tex = m.albedo_tex == VKMIN_NONE ? 0u : m.albedo_tex;
    vec4 albedo_s = texture(TEX(albedo_tex), uv) * m.base_color;
    s.albedo = albedo_s.rgb;
    s.alpha = albedo_s.a;
    uint mr_tex = m.mr_tex == VKMIN_NONE ? 0u : m.mr_tex;
    vec4 mr = texture(TEX(mr_tex), uv);
    s.roughness = clamp(mr.g * m.roughness, 0.03, 1.0);
    s.metallic = clamp(mr.b * m.metallic, 0.0, 1.0);
    uint em_tex = m.emissive_tex == VKMIN_NONE ? 0u : m.emissive_tex;
    s.emissive = m.emissive.rgb * (m.emissive_tex == VKMIN_NONE ? vec3(1.0) : texture(TEX(em_tex), uv).rgb);
    vec3 N = normalize(v_normal);
    if ((m.flags & VKMIN_MAT_DOUBLE_SIDED) != 0u && !front_facing) N = -N;
    uint normal_tex = m.normal_tex == VKMIN_NONE ? 1u : m.normal_tex;
    vec2 nxy = (texture(TEX(normal_tex), uv).rg * 2.0 - 1.0) * m.normal_scale;
    vec3 nts = vec3(nxy, sqrt(max(1.0 - dot(nxy, nxy), 0.0)));
    vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
    vec3 B = cross(N, T) * v_tangent.w;
    vec3 N_mapped = normalize(T * nts.x + B * nts.y + N * nts.z);
    bool use_normal_map = (frame.flags & VKMIN_FRAME_NORMAL_MAPS) != 0u && m.normal_tex != VKMIN_NONE;
    s.N = use_normal_map ? N_mapped : N;
    s.flags = m.flags;
    return s;
}
