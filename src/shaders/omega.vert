#version 450
#include "omega.glsl"
layout(buffer_reference, scalar) readonly buffer OmegaVertices { OmegaVertex v[]; };
layout(location=0) out vec3 world;
layout(location=1) out vec3 normal;
layout(location=2) out vec4 tint;
layout(location=3) out vec3 local;
layout(location=4) flat out int material;
void main() {
    // Unpack once, then the rest of this shader works in the same values it
    // always did. The part code was a float compared against .5 offsets; it is
    // an integer now, so the comparisons below are exact rather than nearly.
    OmegaVertex v=OmegaVertices(o.vertices).v[gl_VertexIndex];
    uint codes=v.color_b_codes>>16;
    int partCode=int(codes>>8);
    vec3 p=vec3(v.x,v.y,v.z),n=omegaOctDecode(v.normal);
    vec4 color=vec4(unpackHalf2x16(v.color_rg),unpackHalf2x16(v.color_b_codes).x,1.);
    material=int(codes&0xffu);
    local=p;
    if(material==8) {
        int code=partCode-10,ship=code/6,battery=(code%6)/3,shot=code%3;
        float age=omegaParticleAge(ship,shot);
        vec3 start=F.pulse_start[code].xyz,end=F.pulse_end[code].xyz;
        vec3 axis=normalize(end-start),right=normalize(cross(axis,vec3(0,1,0))),up=cross(right,axis);
        float head=clamp(age/float(OMEGA_PARTICLE_FLIGHT),0.,1.);
        float tail=clamp((age-3.)/float(OMEGA_PARTICLE_FLIGHT),0.,1.);
        world=mix(start,end,mix(tail,head,p.z))+right*p.x+up*p.y;
        normal=right*n.x+up*n.y+axis*n.z;
        tint=vec4(color.rgb,0);
        gl_Position=F.vp*vec4(world,1);
        if(age<0. || age>float(OMEGA_PARTICLE_FLIGHT)+3. || o.pass==OMEGA_PASS_SHADOW)
            gl_Position=vec4(2,2,2,1);
        return;
    }
    bool articulated=partCode>=32;
    int owner=articulated?(partCode-32)/24:0;
    if(articulated) {
        mat4 pose=F.turrets[partCode-32];
        p=(pose*vec4(p,1)).xyz; n=mat3(pose)*n;
        world=p; normal=n; tint=color; tint.a=owner>0?-float(owner):0.;
        if(owner==0) tint.rgb*=smoothstep(4.5,5.1,F.scene.x);
        gl_Position=o.pass==OMEGA_PASS_SHADOW?omegaShadow(p):F.vp*vec4(p,1);
        if(owner==0 && F.scene.x<4.5) gl_Position=vec4(2,2,2,1);
        return;
    }
    bool opponent=partCode>3;
    int ship=opponent?(partCode-4)/2:0;
    int part=opponent?partCode-4-2*ship:partCode;
    if(part==1) {
        float a=F.scene.x*.17+.23+float(ship)*1.1;
        mat2 r=mat2(cos(a),sin(a),-sin(a),cos(a));
        p.xy=r*p.xy; n.xy=r*n.xy;
    }
    if(material==5) {
        float along=clamp((p.z-OMEGA_MUZZLE_Z)/(-100.-OMEGA_MUZZLE_Z),0.,1.);
        p.z=mix(OMEGA_MUZZLE_Z,omegaImpact(sign(p.x)).z-F.scene.z,along*step(.01,F.flash));
    }
    if(opponent) {
        p=omegaOpponentRotate(p)+omegaFormation(ship);
        n=omegaOpponentRotate(n);
    } else if(partCode<3) p.z+=F.scene.z;
    if(partCode==2) p.y+=.15*sin(F.scene.x*.8+p.x);
    world=p; normal=n; tint=color; tint.a=opponent?-1.-float(ship):(partCode>=3?1.:0.);
    if(partCode<3) tint.rgb*=smoothstep(4.5,5.1,F.scene.x);
    gl_Position=o.pass==OMEGA_PASS_SHADOW?omegaShadow(p):F.vp*vec4(p,1);
    if(partCode<3 && F.scene.x<4.5) gl_Position=vec4(2,2,2,1);
}
