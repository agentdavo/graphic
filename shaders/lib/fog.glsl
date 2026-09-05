// lib/fog.glsl -- exponential distance fog towards frame.fog.rgb. Density 0
// returns the colour unchanged, so it is safe to call unconditionally.
vec3 fog(Frame frame, vec3 color, float view_depth) {
    float t = 1.0 - exp(-frame.fog.w * view_depth);
    return mix(color, frame.fog.rgb, clamp(t, 0.0, 1.0));
}
