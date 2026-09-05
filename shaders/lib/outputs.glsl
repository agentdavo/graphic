// lib/outputs.glsl -- the forward pass writes three targets: HDR colour, the
// instance id for picking, and the octahedral normal for the outline pass.
// Every opaque draw writes all three, unconditionally; a blended pipeline is
// created with the id and normal attachments masked off, so the same shader
// serves both.
layout(location = 0) out vec4 o_color;
layout(location = 1) out uint o_id;
layout(location = 2) out vec2 o_normal;

void write_outputs(vec3 color, float alpha, vec3 N, uint id) {
    o_color = vec4(color * alpha, alpha); // premultiplied
    o_id = id;
    o_normal = oct_encode(N);
}

// Debug views replace the lit colour; they never skip the lighting work above
// them. Modes the shader cannot show (atlas) fall through to the colour.
vec3 debug_color(Frame frame, vec3 color, Surface s, uint cluster_count, uint cascade, uint id) {
    if (frame.debug_mode == VKMIN_DEBUG_NORMALS) return s.N * 0.5 + 0.5;
    if (frame.debug_mode == VKMIN_DEBUG_ALBEDO) return s.albedo;
    if (frame.debug_mode == VKMIN_DEBUG_CASCADES) {
        const vec3 palette[4] = vec3[4](vec3(1, 0.2, 0.2), vec3(0.2, 1, 0.2), vec3(0.2, 0.4, 1), vec3(1, 1, 0.2));
        return palette[cascade] * (0.3 + 0.7 * max(dot(s.N, normalize(frame.sun.xyz)), 0.0));
    }
    if (frame.debug_mode == VKMIN_DEBUG_CLUSTERS) {
        float t = float(cluster_count) / 16.0;
        return cluster_count == 0u ? vec3(0.02) : mix(vec3(0.0, 0.1, 0.5), vec3(1.0, 0.1, 0.0), clamp(t, 0.0, 1.0));
    }
    if (frame.debug_mode == VKMIN_DEBUG_IDS) {
        uint h = id * 2654435761u;
        return id == 0u ? vec3(0.0) : vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) / 255.0;
    }
    return color;
}
