#version 450
#include "omega.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
// Finite hollow funnel in two linear segments: a steep entry cone from the
// mouth to the knee, then a long narrow throat to the distant entrance.
// Analytic intersections give the energy funnel perspective and curvature.
// It is a luminous backdrop, not an opaque wall that clips the passing hull.
const float mouthZ=OMEGA_GATE_MOUTH_Z,entranceZ=OMEGA_GATE_ENTRANCE_Z,mouthRadius=OMEGA_GATE_MOUTH_RADIUS,entranceRadius=OMEGA_GATE_ENTRANCE_RADIUS;
float coneHit(vec3 eye,vec3 ray,float z0,float z1,float r0,float r1) {
    float slope=(r1-r0)/max(z1-z0,1e-4);
    float radius=r0+slope*(eye.z-z0);
    float A=dot(ray.xy,ray.xy)-slope*slope*ray.z*ray.z;
    float B=2.*(dot(eye.xy,ray.xy)-radius*slope*ray.z);
    float C=dot(eye.xy,eye.xy)-radius*radius;
    float hit=1e5;
    if(abs(A)<1e-7) {
        if(abs(B)>1e-7) {
            float t=-C/B,z=eye.z+t*ray.z;
            if(t>0. && z>=z0 && z<=z1) hit=t;
        }
    } else {
        float discriminant=B*B-4.*A*C;
        if(discriminant>=0.) {
            float root=sqrt(discriminant);
            vec2 roots=vec2(-B-root,-B+root)/(2.*A);
            for(int k=0;k<2;k++) {
                float z=eye.z+roots[k]*ray.z;
                if(roots[k]>0. && z>=z0 && z<=z1) hit=min(hit,roots[k]);
            }
        }
    }
    return hit;
}
void main() {
    vec2 screen=uv*2.-1.; screen.x*=o.scene.y; screen.y=-screen.y;
    vec3 forward=normalize(vec3(0,o.eye.w,1)-o.eye.xyz);
    vec3 right=normalize(cross(forward,vec3(0,1,0)));
    vec3 up=cross(right,forward);
    vec3 ray=normalize(forward+tan(.392699)* (right*screen.x+up*screen.y));
    vec2 sky=vec2(atan(ray.x,ray.z),asin(ray.y));
    vec3 color=vec3(0);
    for(int layer=0;layer<2;layer++) {
        float density=layer==0?330.:570.;
        vec2 p=sky*density,cell=floor(p);
        float star=hash21(cell+float(layer)*84.);
        vec2 delta=fract(p)-vec2(hash21(cell+1.),hash21(cell+9.));
        float size=layer==0?.052:.032;
        float sparkle=exp(-dot(delta,delta)/(size*size));
        color+=mix(vec3(.38,.63,1.),vec3(1.,.79,.52),hash21(cell+7.))*sparkle*step(.972,star)*(.35+.35*star);
    }
    // A faint blue-violet nebula band, as behind the gate in the footage.
    float band=exp(-pow((sky.y+.30+.18*sin(sky.x*1.7+.8))*4.,2.));
    float nebula=fbm(sky*5.5+vec2(3.1,7.9))*fbm(sky*13.+vec2(1.3,2.2));
    color+=vec3(.05,.08,.34)*band*nebula*.22;
    vec3 skyColor=color;
    if(o.scene.w>.005) {
        float aperture=o.scene.w,time=o.scene.x;
        float rotation=max(time-2.,0.)*.10;
        vec3 shape=omegaGateShape();
        float gateStart=shape.x,gateEnd=shape.y,scale=shape.z;
        float knee=mix(gateStart,gateEnd,(OMEGA_GATE_KNEE_Z-mouthZ)/(entranceZ-mouthZ));
        float hit=min(coneHit(o.eye.xyz,ray,gateStart,knee,mouthRadius*scale,OMEGA_GATE_KNEE_RADIUS*scale),
                      coneHit(o.eye.xyz,ray,knee,gateEnd,OMEGA_GATE_KNEE_RADIUS*scale,entranceRadius*scale));
        bool throat=false;
        if(ray.z>1e-5) {
            float end=(gateEnd-o.eye.z)/ray.z;
            vec2 endXY=(o.eye.xyz+ray*end).xy;
            if(end>0. && end<hit && length(endXY)<entranceRadius*scale) {
                hit=end; throat=true;
            }
        }
        // Rays that miss the mouth disc see the funnel's outer skin; let it
        // fade with depth so the gate reads as a feathered ellipse, not a pipe.
        float outsideFade=1.;
        if(ray.z>1e-5) {
            float near=(gateStart-o.eye.z)/ray.z;
            float rMouth=length((o.eye.xyz+ray*near).xy)/(mouthRadius*scale);
            outsideFade=1.-smoothstep(1.,1.32,rMouth);
        }
        if(hit<1e4) {
            vec3 p=o.eye.xyz+ray*hit;
            // Logarithmic shading distance preserves visible flow near the
            // mouth while the actual tunnel extends far into the distance.
            float shadingLength=24.*(time>=OMEGA_GATE_CLOSE_START?scale:1.);
            float depth=log(1.+max(p.z-gateStart,0.)/shadingLength)/log(1.+(gateEnd-gateStart)/shadingLength);
            // Rotate the complete plasma pattern coherently about the axis.
            float angle=atan(p.y,p.x)+rotation;
            vec2 around=vec2(cos(angle),sin(angle));
            float cloud=fbm(around*2.5+vec2(depth*3.-time*.08,depth*1.5+time*.035));
            // Long advected ribbons spiral into the throat; no crosswise grid.
            float twist=angle+depth*2.8+cloud*.32;
            vec2 flow=vec2(cos(twist),sin(twist));
            vec2 ribbon=flow*42.+vec2(depth*12.-time*.65,depth*5.);
            vec2 warp=vec2(fbm(ribbon*.21+time*.06),fbm(ribbon*.21+19.-time*.04));
            ribbon+=(warp-.5)*4.;
            float strands=(noise2(ribbon)+noise2(ribbon+.22)+noise2(ribbon-.22))/3.;
            float eddies=fbm(flow*25.+vec2(depth*32.-time*.9,depth*15.+time*.18));
            float silk=fbm(flow*8.+vec2(depth*1.5-time*.10,depth*.75));
            // Broad soft billows with whitish highlights, as in the footage,
            // rather than saturated fine streaks.
            // Big soft billows dominate; fine strands only add texture.
            float billow=fbm(around*1.6+vec2(depth*2.2-time*.05,depth*1.1+time*.02));
            float light=.04+pow(cloud,2.)*.55+pow(billow,2.)*.85+pow(strands,2.)*.22+pow(eddies,2.)*.15+pow(silk,2.)*.50;
            // Mid-blue body, with strands allowed past 1 for whitish highlights.
            color=mix(vec3(.006,.02,.08),vec3(.06,.22,.82),clamp(light,0.,1.35));
            // The tunnel falls into a black throat, with a gradual extinction
            // through the last section instead of a bright disk or hard ring.
            color*=mix(1.,.32,depth);
            // The gate begins as a ring: a dark centre inside a bright rim
            // until the aperture has grown.
            color*=mix(.12,1.,smoothstep(.10,.45,aperture));
            const vec3 voidColor=vec3(.000025,.00006,.00016);
            color=mix(color,voidColor,smoothstep(.40,.78,depth));
            // The lip dissolves into the starfield over a broad region.
            color=mix(skyColor,color,smoothstep(0.,.14,depth));
            color=mix(skyColor,color,outsideFade*mix(exp(-depth*5.),1.,outsideFade));
            if(throat) {
                color=voidColor;
            }
        }
        // A soft mouth halo sits around, rather than across, the open exit.
        if(ray.z>1e-5 && !throat) {
            float near=(gateStart-o.eye.z)/ray.z;
            vec2 mouth=(o.eye.xyz+ray*near).xy;
            float r=length(mouth)/(mouthRadius*scale);
            float containment=1.-smoothstep(1.,1.16,r);
            float angle=atan(mouth.y,mouth.x);
            float flicker=.65+.35*sin((angle+rotation)*21.);
            float rim=exp(-pow((r-1.)*40.,2.));
            float halo=exp(-abs(r-1.)*14.);
            color+=vec3(.12,.45,1.2)*(rim*.02+halo*.16)*flicker*containment;
            float young=1.-smoothstep(.12,.5,aperture);
            color+=vec3(.35,.7,1.5)*exp(-pow((r-1.)*9.,2.))*young*smoothstep(.02,.1,aperture);
            vec2 direction=vec2(cos(angle+rotation),sin(angle+rotation));
            float wisps=pow(noise2(direction*20.+r*2.),3.);
            float outside=smoothstep(.98,1.12,r);
            color+=vec3(.004,.06,.22)*wisps*exp(-abs(r-1.)*7.)*aperture*outside*containment;
            // Four stationary emitters inject energy at diagonal rim anchors.
            float anchor=pow(.5+.5*cos(4.*angle),32.);
            color+=vec3(.08,.7,1.8)*anchor*exp(-abs(r-1.)*38.)
                *(.7+.3*sin(time*4.))*aperture;
        }
    }
    // Concentrated ignition in empty space, before the throat has opened.
    float ignition=smoothstep(1.,1.65,o.scene.x)*(1.-smoothstep(1.8,2.65,o.scene.x));
    vec4 center=o.vp*vec4(0,0,mouthZ,1);
    vec2 delta=uv-(center.xy/center.w*.5+.5); delta.x*=o.scene.y;
    float d2=dot(delta,delta);
    // A warm white sphere with a pink halo and a faint red ring: the optical
    // flare vocabulary of the reference footage rather than a blue spark.
    color+=ignition*(vec3(18,15,12)*exp(-d2*140.)+vec3(1.6,.30,.22)*exp(-d2*14.)
        +vec3(.35,.05,.03)*exp(-pow((sqrt(d2)-.20)*22.,2.)));
    // Renderer-style lens artefacts: a horizontal streak and a faint ring.
    color+=ignition*(vec3(1.4,1.0,.85)*exp(-abs(delta.y)*320.-abs(delta.x)*5.)
        +vec3(.22,.10,.08)*exp(-pow((sqrt(d2)-.34)*45.,2.)));
    // Each pylon charges first: a pulsing red-orange flare a quarter of the
    // way along its spine, on the side facing the corridor.
    float charge=smoothstep(.2,.9,o.scene.x)*(1.-smoothstep(1.7,2.3,o.scene.x));
    for(int k=0;k<4;k++) {
        float a=float(k)*O_PI*.5;
        vec3 local=vec3(0,OMEGA_GATE_PYLON_RADIAL-1.2,OMEGA_GATE_PYLON_BACK_Z+10.);
        // Follow the pylon splay about its far cap.
        float dz=local.z-OMEGA_GATE_PYLON_Z,cs=cos(OMEGA_PYLON_SPLAY),sn=sin(OMEGA_PYLON_SPLAY);
        local=vec3(0,OMEGA_GATE_PYLON_RADIAL+(local.y-OMEGA_GATE_PYLON_RADIAL)*cs-dz*sn,
                   OMEGA_GATE_PYLON_Z+(local.y-OMEGA_GATE_PYLON_RADIAL)*sn+dz*cs);
        vec3 anchorPos=vec3(cos(a)*local.x-sin(a)*local.y,sin(a)*local.x+cos(a)*local.y,local.z);
        vec4 c=o.vp*vec4(anchorPos,1);
        if(c.w<=0.) continue;
        vec2 dp=uv-(c.xy/c.w*.5+.5); dp.x*=o.scene.y;
        float g2=dot(dp,dp);
        float pulse=.7+.3*sin(o.scene.x*9.+float(k)*1.7);
        color+=charge*pulse*(vec3(2.6,.55,.28)*exp(-g2*1400.)+vec3(.9,.14,.07)*exp(-g2*320.)
            +vec3(.30,.04,.02)*exp(-pow((sqrt(g2)-.05)*110.,2.)));
    }
    result=vec4(color,1);
}
