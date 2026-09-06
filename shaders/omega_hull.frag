#version 450
#include "omega.glsl"
layout(location=0) in vec3 world;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 tint;
layout(location=3) in vec3 local;
layout(location=4) flat in int material;
layout(location=0) out vec4 result;
vec3 gateVeil(vec3 surface) {
    if(tint.a<.5 || F.scene.w<.005) return surface;
    vec3 ray=normalize(world-F.eye.xyz);
    if(ray.z<.00001) return surface;
    vec3 shape=omegaGateShape();
    float t=(shape.x-F.eye.z)/ray.z;
    vec2 mouth=(F.eye.xyz+t*ray).xy;
    float r=length(mouth)/(OMEGA_GATE_MOUTH_RADIUS*shape.z);
    float footprint=1.-smoothstep(.94,1.12,r);
    // The half of each pylon inside the funnel dissolves into its energy.
    float recess=smoothstep(OMEGA_GATE_MOUTH_Z,OMEGA_GATE_MOUTH_Z+24.,world.z);
    float veil=(1.-exp(-recess*4.5))*.6*footprint*smoothstep(.15,.85,F.scene.w);
    vec2 screen=gl_FragCoord.xy/vec2(textureSize(TEX(o.gate_id),0));
    return mix(surface,texture(TEX(o.gate_id),screen).rgb,veil);
}
void main() {
    float structure=tint.a;
    // Inside the funnel the hull exists only as seen through the mouth disc:
    // a fragment beyond the mouth whose line of sight misses the opening is
    // dropped, so the ship appears as a small silhouette deep in the vortex
    // and grows, and is never visible beside the cone from an off-axis view.
    if(structure<.5 && world.z>OMEGA_GATE_MOUTH_Z) {
        vec3 shape=omegaGateShape();
        vec3 ray=world-F.eye.xyz;
        if(ray.z<=0. || F.scene.w<.005) discard;
        float t=(shape.x-F.eye.z)/ray.z;
        float r=length((F.eye.xyz+ray*t).xy)/(OMEGA_GATE_MOUTH_RADIUS*shape.z);
        if(t<0. || r>.97) discard;
    }
    vec3 n=normalize(normal),v=normalize(F.eye.xyz-world);
    if(!gl_FrontFacing) n=-n;
    if(material==6) {
        float pulse=.85+.15*sin(F.scene.x*4.-local.y*3.);
        result=vec4(gateVeil(tint.rgb*vec3(.15,3.,6.)*(.015+F.scene.w*pulse)),1); return;
    }
    if(material==5) {
        if(F.flash<.01) discard;
        float fres=pow(abs(dot(n,v)),.6);
        result=vec4(vec3(5.5, .02, .006)*F.flash+vec3(7,1.6,.6)*pow(fres,3.)*F.flash,1); return;
    }
    if(material==7) {
        // Engine plume: bright at the bell, dissolving along its length, and
        // slightly hotter on the axis so the bloom reads as a glow, not a cone.
        float along=clamp((local.z-15.75)/4.75,0.,1.);
        float fres=pow(abs(dot(n,v)),1.5);
        result=vec4(vec3(.3,.7,1.6)*(1.-along)*(1.-along)*(1.2+1.8*fres),1); return;
    }
    if(material==3 || material==4) {
        vec3 emission=material==3?vec3(.12,2.3,4.4):vec3(3.2,.9,.12);
        float level=(structure>.5 && material==3)?.10:1.;
        result=vec4(gateVeil(tint.rgb*emission*(1.2+.15*sin(F.scene.x*7.+local.z))*level),1); return;
    }
    // Choose a face in model space. World normals rotate with the habitat and
    // would switch its UV projection abruptly while it emerges.
    vec3 an=abs(normalize(cross(dFdx(local),dFdy(local))));
    vec2 uv=an.z>.6?local.xy:(an.y>.6?local.xz:local.zy);
    vec2 grid=vec2(uv.x*1.6,uv.y*2.7);
    grid.x+=floor(grid.y)*.37;
    vec2 cell=fract(grid),edge=min(cell,1.-cell);
    float pixel=max(fwidth(grid.x),fwidth(grid.y));
    float aa=max(.008,pixel*.5);
    float seam=1.-smoothstep(.016-aa,.016+aa,min(edge.x,edge.y));
    float detail=1.-smoothstep(.35,1.,pixel);
    float panel=hash21(floor(grid));
    float grain=mix(.5,noise2(uv*90.),1.-smoothstep(.4,1.,max(fwidth(uv.x),fwidth(uv.y))*90.));
    vec3 base=tint.rgb*(.36+panel*.24+grain*.07);
    base*=1.-.45*seam*detail;
    base*=.72+.38*fbm(uv*3.);
    float rivet=1.-smoothstep(.03-aa,.03+aa,length(cell-vec2(.10,.10)));
    base+=rivet*.022*detail;
    if(material==2) {
        float stripe=uv.y*12.;
        float ribs=smoothstep(.16,.24,fract(stripe));
        base*=.40+.60*mix(.8,ribs,1.-smoothstep(.3,1.,fwidth(stripe)));
    }
    // A warm key from the side, as in the ISN footage, with cool engine fill.
    vec3 key=normalize(vec3(-.85,.35,-.45));
    vec3 rim=normalize(vec3(.2,.2,1.));
    float ndl=max(dot(n,key),0.);
    vec3 sc=omegaShadow(world).xyz;
    vec2 st=sc.xy*.5+.5;
    float visibility=0.;
    float bias=max(.0002,.0008*(1.-ndl));
    for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++)
        visibility+=step(sc.z-bias,texture(TEX(o.texture_id),st+vec2(x,y)/2048.).r)/9.;
    if(any(lessThan(st,vec2(0))) || any(greaterThan(st,vec2(1)))) visibility=1.;
    float rough=material==2?.7:.46;
    vec3 h=normalize(key+v);
    float spec=pow(max(dot(n,h),0.),mix(120.,14.,rough))*.9;
    float fres=pow(1.-max(dot(n,v),0.),4.);
    float ao=mix(.60,1.,smoothstep(.65,3.8,length(local.xy)));
    vec3 c=base*(vec3(.05,.07,.12)*ao+vec3(1.7,1.15,.75)*ndl*visibility);
    c+=vec3(.9,.68,.48)*spec*visibility;
    if(structure<.5) c+=base*vec3(.10,.35,1.1)*max(dot(n,vec3(0,0,1)),0.)*smoothstep(4.5,5.1,F.scene.x);
    c*=mix(1.,.38+.30*F.scene.w,structure);
    c+=vec3(.025,.42,1.)*(base*.7+fres*.18)*pow(max(dot(n,rim),0.),.65)*F.scene.w*1.4;
    // The fixed gate machinery receives blue light from its inward emitters.
    if(structure>.5) c+=base*vec3(.08,.6,1.3)*max(dot(n,normalize(vec3(-world.xy,0))),0.)*F.scene.w;
    float ignition=smoothstep(1.9,2.3,F.scene.x)*(1.-smoothstep(2.4,2.9,F.scene.x));
    if(structure>.5) c+=base*vec3(3.,2.6,2.)*ignition*max(dot(n,normalize(vec3(-world.xy,OMEGA_GATE_MOUTH_Z-world.z))),0.);
    // The plasma muzzle lights illuminate the forward armor in world space.
    for(int j=0;j<2;j++) {
        vec3 light=vec3(j==0?-OMEGA_MUZZLE_X:OMEGA_MUZZLE_X,.4,OMEGA_MUZZLE_Z-.1+F.scene.z)-world;
        float d=length(light);
        c+=vec3(1.3,.012,.003)*F.flash*max(dot(n,normalize(light)),0.)/(1.+d*d*.5);
    }
    // Blue light wraps the ship while it is inside the deep conical tunnel.
    float inside=smoothstep(OMEGA_GATE_MOUTH_Z-2.,OMEGA_GATE_MOUTH_Z+14.,world.z)*F.scene.w*(1.-structure);
    c+=base*vec3(.06,.55,1.6)*inside;
    c=mix(c,vec3(.025,.12,.3),inside*smoothstep(OMEGA_GATE_MOUTH_Z+5.,OMEGA_GATE_ENTRANCE_Z,world.z)*.32);
    // No luminous crossing plane: only broad diffuse light inside the funnel.
    result=vec4(gateVeil(c),1);
}
