// lib/lights.glsl -- which lights reach this pixel, and how strongly.
//
//   LightList list = lights_for_pixel(frame, gl_FragCoord.xy, view_depth);
//   for (uint k = 0u; k < list.count; ++k) {
//       Light light = light_at(frame, list, k);
//       vec3 L; float atten = light_attenuation(light, P, L);
//       ...
//   }
//
// With VKMIN_FRAME_CLUSTERED set the list is the pixel's froxel; without it
// the list is every light, the reference path. The sun is never in either:
// frame.sun_light names it and the caller treats it separately.
struct LightList {
    uint base;       // index into the cluster light array, or unused
    uint count;
    bool clustered;
};

LightList lights_for_pixel(Frame frame, vec2 frag_coord, float view_depth) {
    LightList list;
    list.clustered = (frame.flags & VKMIN_FRAME_CLUSTERED) != 0u;
    if (list.clustered) {
        uvec2 tile = uvec2(frag_coord * frame.screen.zw * vec2(VKMIN_CLUSTER_X, VKMIN_CLUSTER_Y));
        tile = min(tile, uvec2(VKMIN_CLUSTER_X - 1u, VKMIN_CLUSTER_Y - 1u));
        uint slice = uint(max(log(max(view_depth, 1e-4)) * frame.cluster_params.x + frame.cluster_params.y, 0.0));
        slice = min(slice, VKMIN_CLUSTER_Z - 1u);
        uint cluster = (slice * VKMIN_CLUSTER_Y + tile.y) * VKMIN_CLUSTER_X + tile.x;
        list.base = cluster * VKMIN_CLUSTER_STRIDE;
        list.count = ClusterRef(frame.cluster_lights).lights[list.base];
    } else {
        list.base = 0u;
        list.count = frame.light_count;
    }
    return list;
}

Light light_at(Frame frame, LightList list, uint k) {
    uint index = list.clustered ? ClusterRef(frame.cluster_lights).lights[list.base + 1u + k] : k;
    return LightRef(frame.lights).l[index];
}

// Windowed inverse square that reaches exactly zero at the radius, times the
// spot cone. Directional lights return 0: they are the sun, handled apart.
float light_attenuation(Light light, vec3 P, out vec3 L) {
    vec3 to_light = light.pos_radius.xyz - P;
    float dist2 = dot(to_light, to_light);
    float dist = sqrt(dist2);
    L = to_light / max(dist, 1e-4);
    if (light.type == VKMIN_LIGHT_DIRECTIONAL) return 0.0;
    float r = light.pos_radius.w;
    float w = clamp(1.0 - (dist2 * dist2) / (r * r * r * r), 0.0, 1.0);
    float atten = w * w / max(dist2, 1e-4);
    if (light.type == VKMIN_LIGHT_SPOT) {
        float cd = dot(-L, light.dir_cone.xyz);
        atten *= smoothstep(light.dir_cone.w, light.cos_inner, cd);
    }
    return atten;
}
