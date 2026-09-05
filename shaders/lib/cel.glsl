// lib/cel.glsl -- the anime lighting model, whole: N.L quantised through a
// ramp texture the game supplies, shadow as a tint rather than a darkening,
// an optional rim term, and specular either off or one stepped highlight.
// Returns radiance for one light of colour `radiance` with lit fraction
// `shadow` (0 = fully shadowed).
vec3 cel(Frame frame, vec3 N, vec3 V, vec3 L, vec3 albedo, vec3 radiance, float shadow) {
    float ndl = dot(N, L);
    // The ramp is a 1D texture over N.L in 0..1; 0 selects the white default
    // and the model degrades to smooth Lambert-ish shading.
    float ramp = texture(TEX(frame.cel_ramp_tex), vec2(ndl * 0.5 + 0.5, 0.5)).r;
    float lit = min(ramp, step(0.5, shadow));
    vec3 base = mix(albedo * frame.shadow_tint.rgb, albedo, lit);
    float rim = pow(1.0 - max(dot(N, V), 0.0), max(frame.cel.y, 1.0)) * frame.cel.x * step(0.0, ndl);
    float spec_raw = pow(max(dot(N, normalize(V + L)), 0.0), 48.0);
    float spec = frame.cel.z > 0.0 ? step(frame.cel.z, spec_raw) * 0.5 * lit : 0.0;
    return (base + (rim + spec) * albedo) * radiance;
}
