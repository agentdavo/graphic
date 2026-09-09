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
    vec3 r0=vec3(F.vp[0][0],F.vp[1][0],F.vp[2][0]);
    vec3 r1=vec3(F.vp[0][1],F.vp[1][1],F.vp[2][1]);
    vec3 r3=vec3(F.vp[0][3],F.vp[1][3],F.vp[2][3]);
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
    // Direction-space wisps are seamless across the longitude wrap exposed by
    // the stern camera. Keep the fleet against near-black space, as in ISN.
    float band=exp(-pow((ray.y+.30+.18*sin(ray.x*1.7+.8))*4.,2.));
    float nebula=.5+.25*sin(dot(ray,vec3(8,31,13))+1.3*sin(dot(ray,vec3(19,-17,23))))
        +.25*sin(dot(ray,vec3(-11,47,29)));
    color+=vec3(.05,.08,.34)*band*pow(max(nebula,0.),3.)*.008;
    // The gas giant. One ray-sphere and one ray-plane against the same ray the
    // stars use, so the body occludes them, the ring occludes the body, and
    // the body drops its own shadow across the ring. What sells the scale is
    // not the size on screen: it is limb darkening, a terminator narrow enough
    // to have an edge, and a ring the ships are plainly not going to disturb.
    vec3 planetSun=normalize(vec3(.62,.50,.60));
    vec3 planetAxis=normalize(vec3(.19,.95,-.25));
    vec3 toCentre=omegaPlanetCentre()-F.eye.xyz;
    float along=dot(toCentre,ray);
    float rr=OMEGA_PLANET_RADIUS*OMEGA_PLANET_RADIUS;
    float miss=dot(toCentre,toCentre)-along*along;
    float front=(along>0. && miss<rr)?along-sqrt(rr-miss):-1.;
    if(front>0.) {
        vec3 sn=normalize(F.eye.xyz+ray*front-omegaPlanetCentre());
        float lat=dot(sn,planetAxis),lon=atan(sn.z,sn.x);
        // Domain-warped belts. Parallel sines read as wallpaper at this size;
        // the shear is what makes them look driven by something.
        float shear=.30*sin(lon*3.+lat*8.)+.16*sin(lon*7.-lat*13.+1.1);
        float bands=.5+.5*sin(lat*16.+shear+1.3*sin(lat*31.+.4));
        vec3 belt=mix(vec3(.13,.16,.26),vec3(.56,.42,.25),bands);
        belt=mix(belt,vec3(.09,.11,.19),pow(abs(lat),4.));
        float storm=exp(-(pow((lat+.24)*11.,2.)+pow(sin((lon-1.2)*.5)*6.,2.)));
        belt=mix(belt,vec3(.62,.30,.16),storm*.85);
        float mu=max(dot(sn,-ray),0.);
        belt*=.48+.52*pow(mu,.45);                 // limb darkening
        float day=dot(sn,planetSun);
        float lit=smoothstep(-.04,.20,day);
        float scatter=exp(-pow(day*8.,2.));        // the thin warm terminator line
        float limb=pow(1.-mu,3.5);
        color=belt*(.010+.115*lit)+vec3(.50,.26,.12)*scatter*.045
            +vec3(.24,.46,.92)*limb*(.04+.55*smoothstep(-.30,.45,day));
    }
    // The atmosphere, on rays that graze past the body rather than hit it. A
    // thin arc outside the silhouette is what makes a planet read as a planet
    // instead of a painted disc, and it cannot come from shading the surface:
    // every point that would carry it is over the horizon.
    if(along>0.) {
        float graze=sqrt(max(miss,0.));
        float shell=1.-smoothstep(OMEGA_PLANET_RADIUS*.997,OMEGA_PLANET_RADIUS*1.060,graze);
        if(shell>0. && graze>OMEGA_PLANET_RADIUS*.997) {
            vec3 gn=normalize(F.eye.xyz+ray*along-omegaPlanetCentre());
            color+=vec3(.34,.60,1.10)*shell*pow(max(dot(gn,planetSun),0.),.65)*.62;
        }
    }
    // The ring. Visible where it passes in front of the body, or beside it.
    // The plane test stays out of a branch so the derivative below is taken in
    // uniform control flow, and the divisor is clamped rather than guarded.
    float faceOn=dot(ray,planetAxis);
    {
        float ringT=dot(toCentre,planetAxis)/(abs(faceOn)<1e-4?1e-4:faceOn);
        vec3 radial=F.eye.xyz+ray*ringT-omegaPlanetCentre();
        float r=length(radial)/OMEGA_PLANET_RADIUS;
        // Both edges are antialiased in screen space, not in ring radius. At
        // grazing incidence r crosses the whole annulus inside one pixel, so a
        // fade written in r collapses to a hard line precisely where the ring
        // is thinnest -- which is what was clipping it short of the limb.
        float edge=max(fwidth(r)*1.4,.010);
        if(ringT>0. && r>1.30-edge && r<2.02+edge && (front<0. || ringT<front)) {
            float grain=.55+.45*sin(r*63.)*sin(r*27.+1.);
            float density=grain*smoothstep(1.30-edge,1.30+edge*2.,r)
                *(1.-smoothstep(2.02-edge*2.,2.02+edge,r));
            if(r>1.58 && r<1.67) density*=.12;      // the division
            // The planet's own shadow, cast along the sun: a point behind the
            // body and within its radius of the axis is in eclipse.
            float sunAlong=dot(radial,planetSun);
            float sunPerp=length(radial-planetSun*sunAlong);
            float eclipse=(sunAlong<0. && sunPerp<OMEGA_PLANET_RADIUS)?.10:1.;
            vec3 dust=mix(vec3(.40,.36,.30),vec3(.68,.62,.51),grain);
            float level=(.022+.058*max(dot(planetAxis,planetSun),0.))*eclipse;
            color=mix(color,dust*level,clamp(density,0.,1.));
        }
    }
    vec3 skyColor=color;
    if(F.scene.w>.005) {
        float aperture=F.scene.w,time=F.scene.x;
        float rotation=max(time-2.,0.)*.10;
        vec3 shape=omegaGateShape();
        float gateStart=shape.x,gateEnd=shape.y,scale=shape.z;
        float hit=coneHit(F.eye.xyz,ray,gateStart,gateEnd,mouthRadius*scale,entranceRadius*scale);
        bool throat=false;
        if(ray.z>1e-5) {
            float end=(gateEnd-F.eye.z)/ray.z;
            vec2 endXY=(F.eye.xyz+ray*end).xy;
            if(end>0. && end<hit && length(endXY)<entranceRadius*scale) {
                hit=end; throat=true;
            }
        }
        // Rays that miss the mouth disc see the funnel's outer skin; let it
        // fade with depth so the gate reads as a feathered ellipse, not a pipe.
        float outsideFade=1.;
        if(ray.z>1e-5) {
            float near=(gateStart-F.eye.z)/ray.z;
            float rMouth=length((F.eye.xyz+ray*near).xy)/(mouthRadius*scale);
            outsideFade=1.-smoothstep(1.,1.32,rMouth);
        }
        if(hit<1e4) {
            vec3 p=F.eye.xyz+ray*hit;
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
            float filament=pow(clamp(1.-abs(strands*2.-1.),0.,1.),3.);
            float light=.07+pow(cloud,2.)*.25+pow(billow,2.)*.38
                +filament*(.22+.28*cloud)+pow(eddies,2.)*.46+pow(silk,2.)*.28;
            // Mid-blue body, with strands allowed past 1 for whitish highlights.
            color=mix(vec3(.006,.025,.085),vec3(.085,.34,.92),clamp(light,0.,1.35));
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
            float near=(gateStart-F.eye.z)/ray.z;
            vec2 mouth=(F.eye.xyz+ray*near).xy;
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
            float near=(gateStart-F.eye.z)/ray.z;
            float inside=1.-smoothstep(.9,1.,length((F.eye.xyz+ray*near).xy)/(mouthRadius*scale));
            float arrival=smoothstep(7.0,8.2,time)*(1.-smoothstep(8.6,9.4,time));
            vec4 hull=F.vp*vec4(0,0,mix(gateStart,gateEnd,.9),1);
            if(arrival>0. && hull.w>0.) {
                vec2 dp=uv-(hull.xy/hull.w*.5+.5); dp.x*=F.scene.y;
                float d=length(dp),radius=mix(.015,.06,smoothstep(7.,9.,time));
                color+=arrival*inside*(vec3(3.,1.7,1.3)*exp(-d*d/(radius*radius))
                    +vec3(.9,.28,.22)*exp(-d*d/pow(radius*2.5,2.)));
            }
        }
    }
    result=vec4(color,1);
}
