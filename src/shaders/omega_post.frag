#version 450
#include "omega.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
// Scene sample smeared along the hull's screen-space motion: a shutter for
// the flyby and the whip pan, as the footage has. Free when nothing moves.
vec3 scene(vec2 p) {
    vec2 sweep=F.blur.xy*F.blur.z;
    if(dot(sweep,sweep)<1e-9) return texture(TEX(o.texture_id),p).rgb;
    vec3 sum=vec3(0);
    for(int k=-2;k<=2;k++) sum+=texture(TEX(o.texture_id),p+sweep*(float(k)*.5)).rgb;
    return sum/5.;
}
// Two grades, chosen at runtime out of the scene block's spare field. The
// period curve is what this demo shipped with: no filmic roll-off, highlights
// clipping to white the way a 1990s renderer wrote them. The default now tones
// luminance and blends per-channel toward it, so a saturated beam stays red as
// it goes bright instead of blowing out to pink, and the floor is crushed to
// true black. They are a choice rather than an improvement, so both stay and
// --period still selects the old one.
vec3 omegaGrade(vec3 c) {
    if(F.reserved[0]>.5) { c*=1.15; c=c/(1.+c*.06); return pow(clamp(c,0.,1.),vec3(1./2.2)); }
    c*=1.06;
    float l=dot(c,vec3(.2126,.7152,.0722));
    float toned=l/(1.+l);
    c=mix(c/(1.+c),vec3(toned),toned*toned);
    c=max(c-.006,0.)*(1./(1.-.006));
    return pow(clamp(c,0.,1.),vec3(1./2.2));
}
void main() {
    if(o.pass==OMEGA_PASS_BACKDROP) { result=vec4(texture(TEX(o.texture_id),uv).rgb,1); return; }
    vec2 px=1./vec2(textureSize(TEX(o.texture_id),0));
    if(o.pass==OMEGA_PASS_BLOOM) {
        vec3 sum=vec3(0); float total=0.;
        for(int y=-2;y<=2;y++) for(int x=-2;x<=2;x++) {
            float weight=exp(-float(x*x+y*y)*.32);
            vec3 c=texture(TEX(o.texture_id),uv+vec2(x,y)*px*3.).rgb;
            sum+=max(c-vec3(.7),0.)*weight; total+=weight;
        }
        result=vec4(sum/total,1); return;
    }
    vec3 c=scene(uv);
    vec2 bp=1./vec2(textureSize(TEX(o.bloom_id),0));
    vec3 bloom=vec3(0);
    for(int k=-4;k<=4;k++) {
        bloom+=texture(TEX(o.bloom_id),uv+vec2(k,0)*bp*2.).rgb*exp(-float(k*k)*.16);
        bloom+=texture(TEX(o.bloom_id),uv+vec2(0,k)*bp*2.).rgb*exp(-float(k*k)*.16);
    }
    c+=bloom*.085;
    // Compact optical scatter from visible HDR sources. Dense pixel taps keep
    // tiny antenna beacons from dropping between the broad bloom's samples.
    // Sampling the rendered scene also respects hull and habitat occlusion.
    vec3 pinGlow=vec3(0);
    for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) {
        vec3 source=texture(TEX(o.texture_id),uv+vec2(x,y)*px).rgb;
        float peak=max(source.r,max(source.g,source.b));
        pinGlow+=source*(max(peak-1.,0.)/max(peak,.001))*exp(-float(x*x+y*y)*.8);
    }
    pinGlow*=.16;
    c+=pinGlow;
    vec3 flares=omegaFlares(uv);
    c+=flares;
    c=omegaGrade(c);
    // Composite video softness: luma stays sharp, chroma bleeds sideways.
    vec3 wide=vec3(0);
    for(int k=-3;k<=3;k++) {
        vec3 s=scene(uv+vec2(k,0)*px*1.5);
        s+=bloom*.085+pinGlow+flares;
        wide+=omegaGrade(s)*exp(-float(k*k)*.22);
    }
    wide/=1.+2.*(exp(-.22)+exp(-.88)+exp(-1.98));
    const vec3 lumaWeights=vec3(.299,.587,.114);
    c=wide-dot(wide,lumaWeights)+mix(dot(c,lumaWeights),dot(wide,lumaWeights),.35);
    // Fine grain, deterministic per tick.
    // Grain rides the signal rather than the floor, so empty space stays the
    // one thing in frame that is genuinely black.
    c+=(hash21(floor(uv*vec2(textureSize(TEX(o.texture_id),0)))+floor(F.scene.x*60.))-.5)
        *.030*smoothstep(0.,.22,dot(c,lumaWeights));
    c*=1.-.22*dot(uv-.5,uv-.5);
    // Cinemascope framing is part of the presentation, never the HDR passes.
    if(uv.y<.065 || uv.y>.935) c=vec3(0);
    result=vec4(c,1);
}
