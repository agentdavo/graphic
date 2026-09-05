#version 450
#include "omega.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
// Finite hollow cone from the mouth to a narrow throat. Analytic
// intersections give the energy cone perspective and curvature. It is a
// luminous backdrop, not an opaque wall that clips the passing hull.
const float mouthZ=OMEGA_GATE_MOUTH_Z,entranceZ=OMEGA_GATE_THROAT_Z,mouthRadius=OMEGA_GATE_MOUTH_RADIUS,entranceRadius=OMEGA_GATE_THROAT_RADIUS;
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
    // Exact view ray from the view-projection rows: the direction d whose
    // clip x/w and y/w equal this pixel's NDC satisfies (r0-x r3).d=0 and
    // (r1-y r3).d=0. No separate target, FOV or aspect needs passing.
    vec2 ndc=uv*2.-1.;
    vec3 r0=vec3(o.vp[0][0],o.vp[1][0],o.vp[2][0]);
    vec3 r1=vec3(o.vp[0][1],o.vp[1][1],o.vp[2][1]);
    vec3 r3=vec3(o.vp[0][3],o.vp[1][3],o.vp[2][3]);
    vec3 ray=cross(r0-ndc.x*r3,r1-ndc.y*r3);
    if(dot(ray,r3)<0.) ray=-ray;
    ray=normalize(ray);
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
    // Streaks along the band: stretched, single-octave-weighted noise.
    vec2 streak=vec2(sky.x*3.+sky.y*9.,sky.y*28.-sky.x*4.);
    float nebula=fbm(streak+vec2(3.1,7.9));
    nebula=pow(max(nebula-.25,0.)*1.6,1.6);
    color+=vec3(.05,.08,.34)*band*nebula*.10;
    vec3 skyColor=color;
    if(o.scene.w>.005) {
        float aperture=o.scene.w,time=o.scene.x;
        float rotation=max(time-2.,0.)*.10;
        vec3 shape=omegaGateShape();
        float gateStart=shape.x,gateEnd=shape.y,scale=shape.z;
        float hit=coneHit(o.eye.xyz,ray,gateStart,gateEnd,mouthRadius*scale,entranceRadius*scale);
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
            color=mix(color,voidColor,smoothstep(.62,.92,depth));
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
        // Arrival: a warm pink-white glow at the throat, where the hull will
        // appear, grows just before its silhouette shows and fades as it does.
        if(ray.z>1e-5) {
            float near=(gateStart-o.eye.z)/ray.z;
            float inside=1.-smoothstep(.9,1.,length((o.eye.xyz+ray*near).xy)/(mouthRadius*scale));
            float arrival=smoothstep(7.0,8.2,time)*(1.-smoothstep(8.6,9.4,time));
            vec4 hull=o.vp*vec4(0,0,mix(gateStart,gateEnd,.9),1);
            if(arrival>0. && hull.w>0.) {
                vec2 dp=uv-(hull.xy/hull.w*.5+.5); dp.x*=o.scene.y;
                float d=length(dp),radius=mix(.015,.06,smoothstep(7.,9.,time));
                color+=arrival*inside*(vec3(3.,1.7,1.3)*exp(-d*d/(radius*radius))
                    +vec3(.9,.28,.22)*exp(-d*d/pow(radius*2.5,2.)));
            }
        }
    }
    result=vec4(color,1);
}
