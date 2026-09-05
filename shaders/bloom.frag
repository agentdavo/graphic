#version 450
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
void main() {
    vec2 pixel=1.0/vec2(textureSize(TEX(push.param),0));
    vec3 sum=vec3(0);
    for (int k=-4;k<=4;++k) {
        vec2 offset=push.param2==2u ? vec2(0,k) : vec2(k,0);
        vec3 c=texture(TEX(push.param),v_uv+offset*pixel*2.0).rgb;
        if (push.param2==0u) c=max(c-vec3(1.0),vec3(0));
        sum+=c*exp(-float(k*k)/8.0);
    }
    if (push.param2==2u) sum *= uintBitsToFloat(push.param3);
    o_color=vec4(sum/4.898,1);
}
