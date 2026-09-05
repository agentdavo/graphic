#version 450
#include "omega.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
void main() {
    if(o.padding< -1.5) { result=vec4(texture(TEX(o.texture_id),uv).rgb,1); return; }
    vec2 px=1./vec2(textureSize(TEX(o.texture_id),0));
    if(o.padding>0.5) {
        vec3 sum=vec3(0); float total=0.;
        for(int y=-2;y<=2;y++) for(int x=-2;x<=2;x++) {
            float weight=exp(-float(x*x+y*y)*.32);
            vec3 c=texture(TEX(o.texture_id),uv+vec2(x,y)*px*3.).rgb;
            sum+=max(c-vec3(.7),0.)*weight; total+=weight;
        }
        result=vec4(sum/total,1); return;
    }
    vec3 c=texture(TEX(o.texture_id),uv).rgb;
    vec2 bp=1./vec2(textureSize(TEX(o.bloom_id),0));
    vec3 bloom=vec3(0);
    for(int k=-4;k<=4;k++) {
        bloom+=texture(TEX(o.bloom_id),uv+vec2(k,0)*bp*2.).rgb*exp(-float(k*k)*.16);
        bloom+=texture(TEX(o.bloom_id),uv+vec2(0,k)*bp*2.).rgb*exp(-float(k*k)*.16);
    }
    c+=bloom*.085;
    // Period look: no filmic curve, highlights clip to white the way a
    // 1990s renderer wrote them; a gentle knee keeps mid-tones from posterising.
    c*=1.15;
    c=c/(1.+c*.06);
    c=pow(clamp(c,0.,1.),vec3(1./2.2));
    // Composite video softness: luma stays sharp, chroma bleeds sideways.
    vec3 wide=vec3(0);
    for(int k=-3;k<=3;k++) {
        vec3 s=texture(TEX(o.texture_id),uv+vec2(k,0)*px*1.5).rgb;
        s+=bloom*.085; s*=1.15; s=s/(1.+s*.06);
        wide+=pow(clamp(s,0.,1.),vec3(1./2.2))*exp(-float(k*k)*.22);
    }
    wide/=1.+2.*(exp(-.22)+exp(-.88)+exp(-1.98));
    const vec3 lumaWeights=vec3(.299,.587,.114);
    c=wide-dot(wide,lumaWeights)+mix(dot(c,lumaWeights),dot(wide,lumaWeights),.35);
    // Fine grain, deterministic per tick.
    c+=(hash21(floor(uv*vec2(textureSize(TEX(o.texture_id),0)))+floor(o.scene.x*60.))-.5)*.028;
    c*=1.-.22*dot(uv-.5,uv-.5);
    // Cinemascope framing is part of the presentation, never the HDR passes.
    if(uv.y<.065 || uv.y>.935) c=vec3(0);
    result=vec4(c,1);
}
