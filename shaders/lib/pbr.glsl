// lib/pbr.glsl -- metallic-roughness: GGX distribution, Smith visibility,
// Schlick Fresnel, Lambert diffuse. pbr() returns reflected radiance per unit
// incoming radiance, N.L included; multiply by the light's colour.
const float PI = 3.14159265359;

float d_ggx(float NdotH, float a2) {
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float g_smith(float NdotV, float NdotL, float a2) {
    float gv = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
    float gl = NdotL + sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
    return 1.0 / max(gv * gl, 1e-6); // includes the 1/(4 NdotV NdotL) of the BRDF
}

vec3 f_schlick(vec3 f0, float VdotH) {
    float f = pow(1.0 - VdotH, 5.0);
    return f0 + (1.0 - f0) * f;
}

vec3 pbr(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = f_schlick(f0, VdotH);
    vec3 spec = d_ggx(NdotH, a2) * g_smith(NdotV, NdotL, a2) * F;
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * NdotL;
}
