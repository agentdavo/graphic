#version 450
#include "omega.glsl"
layout(buffer_reference, scalar) readonly buffer OmegaVertices { OmegaVertex v[]; };
layout(location=0) out vec3 world;
layout(location=1) out vec3 normal;
layout(location=2) out vec4 tint;
layout(location=3) out vec3 local;
layout(location=4) flat out int material;
void main() {
    OmegaVertex v=OmegaVertices(o.vertices).v[gl_VertexIndex];
    vec3 p=v.position.xyz,n=v.normal.xyz;
    material=int(v.position.w+.5);
    local=p;
    if(v.normal.w>.5 && v.normal.w<1.5) {
        float a=F.scene.x*.17+.23;
        mat2 r=mat2(cos(a),sin(a),-sin(a),cos(a));
        p.xy=r*p.xy; n.xy=r*n.xy;
    }
    if(material==5) {
        p.z=-9.9+(p.z+9.9)*step(.01,F.flash);
    }
    if(v.normal.w<2.5) p.z+=F.scene.z;
    if(v.normal.w>1.5 && v.normal.w<2.5) p.y+=.15*sin(F.scene.x*.8+p.x);
    world=p; normal=n; tint=v.color; tint.a=step(2.5,v.normal.w);
    if(v.normal.w<2.5) tint.rgb*=smoothstep(4.5,5.1,F.scene.x);
    gl_Position=o.pass==OMEGA_PASS_SHADOW?omegaShadow(p):F.vp*vec4(p,1);
    if(v.normal.w<2.5 && F.scene.x<4.5) gl_Position=vec4(2,2,2,1);
}
