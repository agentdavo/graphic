// lib/cel.glsl -- the anime lighting model, whole: N.L quantised through a
// ramp texture the game supplies, shadow as a tint rather than a darkening,
// an optional rim term, and specular either off or one stepped highlight.
// Returns radiance for one light of colour `radiance` with lit fraction
// `shadow` (0 = fully shadowed).
vec3 cel(Frame frame, vec3 N, vec3 V, vec3 L, vec3 albedo, vec3 radiance, float shadow) {
    float ndl = dot(N, L);
    // The ramp's u axis spans N.L over its full -1..1, not the clamped 0..1,
    // so the artist can shape both the terminator and the back half. It is a
    // 2D sampler read at y = 0.5 only because there is one texture array and
    // it holds 2D images. Slot 0 is the engine's 1x1 white, i.e. a constant 1,
    // which degrades this to flat albedo plus rim and spec -- but a caller
    // going through render.h never sees that, since render.c substitutes its
    // own three-step ramp for a zero cel_ramp_tex.
    float ramp = texture(TEX(frame.cel_ramp_tex), vec2(ndl * 0.5 + 0.5, 0.5)).r;
    float lit = min(ramp, step(0.5, shadow));
    vec3 base = mix(albedo * frame.shadow_tint.rgb, albedo, lit);
    float rim = pow(1.0 - max(dot(N, V), 0.0), max(frame.cel.y, 1.0)) * frame.cel.x * step(0.0, ndl);
    float spec_raw = pow(max(dot(N, normalize(V + L)), 0.0), 48.0);
    float spec = frame.cel.z > 0.0 ? step(frame.cel.z, spec_raw) * 0.5 * lit : 0.0;
    return (base + (rim + spec) * albedo) * radiance;
}
