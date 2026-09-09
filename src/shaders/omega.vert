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
    if(partCode>=OMEGA_FURY_PART) {
        int index=partCode-OMEGA_FURY_PART;
        int ship=index<OMEGA_FURY_HERO?0:1+(index-OMEGA_FURY_HERO)/OMEGA_FURY_WING;
        mat4 pose=F.fury[index];
        p=(pose*vec4(p,1)).xyz; n=mat3(pose)*n;
        world=p; normal=n; tint=color; tint.a=ship>0?-float(ship):0.;
        if(ship==0) tint.rgb*=smoothstep(4.5,5.1,F.scene.x);
        gl_Position=o.pass==OMEGA_PASS_SHADOW?omegaShadow(p):F.vp*vec4(p,1);
        if(ship==0 && (F.scene.x<4.5 || omegaFuryAge(index)<0.)) gl_Position=vec4(2,2,2,1);
        return;
    }
    if(material==10) {
        // A round blob, billboarded to the eye. The index rides in the
        // vertex's own z, exactly as the embers do.
        OmegaBolt b=omegaBolt(p.z);
        vec3 toEye=normalize(F.eye.xyz-b.centre);
        vec3 right=normalize(cross(toEye,vec3(0,1,0)));
        vec3 up=cross(right,toEye);
        float size=OMEGA_BOLT_SIZE*(.75+.45*b.fade);
        world=b.centre+right*(p.x*size)+up*(p.y*size);
        normal=toEye; local=p;
        tint=vec4(vec3(6.2,1.5,9.5)*(.35+.65*b.fade),0);
        gl_Position=F.vp*vec4(world,1);
        if(b.live<.5 || o.pass==OMEGA_PASS_SHADOW) gl_Position=vec4(2,2,2,1);
        return;
    }
    if(material==9) {
        // The quad corners are baked at (+-.5,+-.5); the ember's index rides
        // in the vertex's own z, which is the only place left that costs
        // nothing. Drawn as a streak along its velocity, billboarded about it.
        OmegaEmber e=omegaEmber(p.z);
        vec3 along=normalize(e.velocity);
        vec3 side=normalize(cross(along,normalize(F.eye.xyz-e.centre)));
        float length=OMEGA_EMBER_SIZE*(2.0+4.2*e.fade);
        world=e.centre+along*(p.x*length)+side*(p.y*OMEGA_EMBER_SIZE);
        normal=along; local=p;
        tint=vec4(vec3(9.,2.4,.30)*e.fade*e.fade,0);
        gl_Position=F.vp*vec4(world,1);
        if(e.live<.5 || o.pass==OMEGA_PASS_SHADOW) gl_Position=vec4(2,2,2,1);
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
        float a=F.scene.x*OMEGA_HABITAT_RATE+OMEGA_HABITAT_PHASE+float(ship)*OMEGA_HABITAT_STAGGER;
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
    world=p; normal=n; tint=color; tint.a=opponent?-1.-float(ship):(partCode>=3?1.:0.);
    if(partCode<3) tint.rgb*=smoothstep(4.5,5.1,F.scene.x);
    gl_Position=o.pass==OMEGA_PASS_SHADOW?omegaShadow(p):F.vp*vec4(p,1);
    if(partCode<3 && F.scene.x<4.5) gl_Position=vec4(2,2,2,1);
}
