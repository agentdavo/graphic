#version 450
#extension GL_EXT_nonuniform_qualifier : require
// Regression: an attachment written in one pass is sampled by the next vertex stage.
layout(set=0,binding=0) uniform sampler2D images[];
layout(push_constant) uniform Source { uint image; } source;
layout(location=0) out vec3 v_color;
void main() {
    const vec2 positions[3]=vec2[3](vec2(0,-0.6),vec2(-0.6,0.5),vec2(0.6,0.5));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    v_color=texelFetch(images[nonuniformEXT(source.image)],ivec2(32),0).rgb;
}
