// outdoor.glsl -- shared weather. No wall clock, mutable random stream or
// texture sky. Single-scattering approximation using optical air mass,
// Rayleigh phase and Henyey-Greenstein Mie phase; metres, Y up.
#ifndef VKMIN_OUTDOOR_GLSL
#define VKMIN_OUTDOOR_GLSL
uint outdoor_hash(uvec2 cell, uint seed) {
    uint h = cell.x*747796405u + cell.y*2891336453u + seed*277803737u;
    h = (h ^ (h >> 16u))*2246822519u;
    return (h ^ (h >> 13u))*3266489917u;
}
float coverage_noise(vec2 pixel) { return fract(52.9829189*fract(dot(floor(pixel),vec2(0.06711056,0.00583715)))); }
float hash_unit(uint h) { return float(h & 0xffffffu)/16777216.0; }
vec3 wind(vec3 position, uint frame) {
    float t = float(frame)/60.0;
    float gust = 0.55 + 0.35*sin(dot(position.xz, vec2(0.009, 0.013))-t*0.37);
    float bend = sin(dot(position.xz, vec2(0.19, 0.11))-t*1.7);
    return vec3(1.0, 0.0, 0.36)*(gust*(0.6+0.4*bend));
}
vec3 sky_radiance(vec3 direction, vec3 sun) {
    float mu = clamp(dot(direction, sun), -1.0, 1.0);
    vec3 betaR = vec3(5.8, 13.5, 33.1)*1e-6;
    vec3 betaM = vec3(3e-6);
    float air = 1.0/sqrt(max(direction.y, 0.0)*max(direction.y, 0.0)+0.012);
    float sunAir = 1.0/sqrt(max(sun.y, 0.0)*max(sun.y, 0.0)+0.025);
    vec3 opticalR = betaR*8000.0, opticalM = betaM*1200.0;
    vec3 extinct = opticalR+opticalM;
    float rayleigh = 3.0*(1.0+mu*mu)/(16.0*3.14159265);
    const float g = 0.76;
    float mie = (1.0-g*g)/(4.0*3.14159265*pow(max(1.0+g*g-2.0*g*mu, 0.01), 1.5));
    vec3 solar = vec3(20.0, 19.3, 18.2)*exp(-extinct*sunAir*0.35);
    vec3 scattered = solar*(1.0-exp(-extinct*air))*(opticalR*rayleigh+opticalM*mie)/extinct;
    float disc = smoothstep(cos(0.006), cos(0.0045), mu);
    return scattered + solar*disc*3.0;
}
// A distant planar cloud layer shares the shadow mask and its frame motion.
// It has no volume, marching, history or extra draw. The sky beneath is analytic.
vec3 outdoor_sky(Outdoor o, vec3 ray, vec3 sun, uint frame) {
    vec3 clear = sky_radiance(ray,sun);
    vec2 uv = ray.xz/max(ray.y,0.035)*0.225+vec2(float(frame)/60.0*0.003,0);
    float broad = textureLod(TEX(o.maps.w),uv,0).r;
    float detail = textureLod(TEX(o.maps.w),uv*3.7,0).r;
    float coverage = smoothstep(0.49,0.68,broad*.8+detail*.2)*smoothstep(0.08,0.3,ray.y);
    vec3 cloud = mix(vec3(.65,.72,.78),vec3(1.05,1.02,.96),smoothstep(.48,.8,broad));
    return mix(clear,cloud,coverage*min(o.weather.x*2.0,1.0));
}
vec3 aerial(Outdoor o, vec3 color, vec3 P, vec3 camera, vec3 sun) {
    vec3 delta = P-camera;
    float d = length(delta);
    vec3 transmit = exp(-vec3(0.45, 0.75, 1.25)*d*o.height.w);
    return color*transmit + sky_radiance(delta/max(d, 0.001), sun)*(1.0-transmit);
}
vec2 terrain_uv(Outdoor o, vec2 p) { return (p-o.terrain.xy)/o.terrain.zw; }
float terrain_height(Outdoor o, vec2 p) {
    vec2 size = vec2(textureSize(TEX(o.maps.x),0));
    vec2 uv = (terrain_uv(o,p)*(size-1.0)+0.5)/size;
    vec2 rg = textureLod(TEX(o.maps.x), uv, 0).rg;
    return o.height.x + dot(rg, vec2(65280.0,255.0))/65535.0*o.height.y;
}
float cloud_shadow(Outdoor o, vec3 P, vec3 sun, uint frame) {
    vec2 cloud = (P.xz + sun.xz/max(sun.y, 0.08)*(250.0-P.y))*0.0009;
    cloud += vec2(float(frame)/60.0*0.003, 0.0);
    float noise = texture(TEX(o.maps.w), cloud).r;
    return 1.0-o.weather.x*smoothstep(0.35, 0.7, noise);
}
// Stable world-space dither: depth prepass, shadow map and colour agree.
// Grass density falls gradually, then the remaining blades shrink into the
// matching terrain material. Wind is weighted by height so roots stay fixed.
float grass_fade(Frame f, vec3 root) {
    if (f.outdoor == uint64_t(0)) return 1.0;
    float distanceLimit = OutdoorRef(f.outdoor).o.weather.w;
    return 1.0-smoothstep(distanceLimit*0.55, distanceLimit, distance(root.xz, f.camera_pos.xz));
}
#endif
