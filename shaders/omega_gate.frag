#version 450
#include "omega.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
// Finite hollow cone: mouth projects forward; distant entrance stays anchored.
// Analytic intersections give the energy funnel perspective and curvature.
// It is a luminous backdrop, not an opaque wall that clips the passing hull.
const float mouthZ=OMEGA_GATE_MOUTH_Z,entranceZ=OMEGA_GATE_ENTRANCE_Z,mouthRadius=OMEGA_GATE_MOUTH_RADIUS,entranceRadius=7.5;
float coneHit(vec3 eye,vec3 ray,vec3 shape) {
    float gateStart=shape.x,gateEnd=shape.y,scale=shape.z;
    float slope=(entranceRadius-mouthRadius)*scale/(gateEnd-gateStart);
    float radius=mouthRadius*scale+slope*(eye.z-gateStart);
    float A=dot(ray.xy,ray.xy)-slope*slope*ray.z*ray.z;
    float B=2.*(dot(eye.xy,ray.xy)-radius*slope*ray.z);
    float C=dot(eye.xy,eye.xy)-radius*radius;
    float hit=1e5;
    if(abs(A)<1e-7) {
        if(abs(B)>1e-7) {
            float t=-C/B,z=eye.z+t*ray.z;
            if(t>0. && z>=gateStart && z<=gateEnd) hit=t;
        }
    } else {
        float discriminant=B*B-4.*A*C;
        if(discriminant>=0.) {
            float root=sqrt(discriminant);
            vec2 roots=vec2(-B-root,-B+root)/(2.*A);
            for(int k=0;k<2;k++) {
                float z=eye.z+roots[k]*ray.z;
                if(roots[k]>0. && z>=gateStart && z<=gateEnd) hit=min(hit,roots[k]);
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
    vec3 skyColor=color;
    if(o.scene.w>.005) {
        float aperture=o.scene.w,time=o.scene.x;
        float rotation=max(time-2.,0.)*.10;
        vec3 shape=omegaGateShape();
        float gateStart=shape.x,gateEnd=shape.y,scale=shape.z;
        float hit=coneHit(o.eye.xyz,ray,shape);
        bool throat=false;
        if(ray.z>1e-5) {
            float end=(gateEnd-o.eye.z)/ray.z;
            vec2 endXY=(o.eye.xyz+ray*end).xy;
            if(end>0. && end<hit && length(endXY)<entranceRadius*scale) {
                hit=end; throat=true;
            }
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
            float light=.022+pow(cloud,2.)*.25+pow(strands,2.)*.75+pow(eddies,2.)*.9+pow(silk,2.)*.24;
            color=mix(vec3(.001,.006,.028),vec3(.012,.25,1.05),clamp(light,0.,1.6));
            // The tunnel falls into a black throat, with a gradual extinction
            // through the last section instead of a bright disk or hard ring.
            color*=mix(1.,.32,depth);
            const vec3 voidColor=vec3(.000025,.00006,.00016);
            color=mix(color,voidColor,smoothstep(.48,.85,depth));
            // The lip dissolves into the starfield over a broad region.
            color=mix(skyColor,color,smoothstep(0.,.14,depth));
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
            color+=vec3(.025,.43,1.35)*(rim*.025+halo*.055)*flicker*containment;
            vec2 direction=vec2(cos(angle+rotation),sin(angle+rotation));
            float wisps=pow(noise2(direction*20.+r*2.),3.);
            float outside=smoothstep(.98,1.12,r);
            color+=vec3(.004,.06,.22)*wisps*exp(-abs(r-1.)*7.)*aperture*outside*containment;
            // Four stationary emitters inject energy at diagonal rim anchors.
            float anchor=pow(.5+.5*cos(4.*(angle-O_PI*.25)),32.);
            color+=vec3(.08,.7,1.8)*anchor*exp(-abs(r-1.)*38.)
                *(.7+.3*sin(time*4.))*aperture;
        }
    }
    // Concentrated ignition in empty space, before the throat has opened.
    float ignition=smoothstep(1.,1.65,o.scene.x)*(1.-smoothstep(1.8,2.65,o.scene.x));
    vec4 center=o.vp*vec4(0,0,mouthZ,1);
    vec2 delta=uv-(center.xy/center.w*.5+.5); delta.x*=o.scene.y;
    float d2=dot(delta,delta);
    color+=ignition*(vec3(9,14,17)*exp(-d2*18000.)+vec3(.012,.10,.19)*exp(-d2*18.));
    color+=ignition*vec3(.06,.55,1.)*exp(-abs(delta.y)*850.-abs(delta.x)*9.);
    result=vec4(color,1);
}
