/* The room model. One pure function, sndmin_acoustics, turns a listener, a
 * source and a box soup into everything the mixer needs to place that source:
 * how loud, how dull, how pitch-shifted, which speakers, which four early
 * reflections, and how the room as a whole reverberates.
 *
 * It is a geometric-acoustics sketch, not a wave solver. Sound is treated as
 * rays: one from listener to source for the direct path, and sixteen fixed
 * directions to probe the enclosure. Nothing here is stochastic and nothing is
 * iterative, so it costs the same every call and returns the same answer every
 * call -- which is what lets the game thread run it and a replay reproduce it.
 *
 * Three room numbers come out of the probe and drive the shared reverb:
 *   mean_free_path  metres, the average distance to the first surface over the
 *                   sixteen directions. The classical acoustics quantity: how
 *                   far sound travels between bounces, so how big the room is
 *                   and how far apart its echoes are.
 *   openness        0..1, the fraction of those rays that hit nothing within
 *                   250 m. One means outdoors, where there is no room to
 *                   reverberate; zero means fully enclosed.
 *   decay           seconds, how long the tail lasts, taken as a straight
 *                   function of size. See apply()'s CMD_FRAME in sndmin.c for
 *                   what the mixer does with it, including the exact dB it
 *                   means -- which is not the usual RT60.
 *
 * Called on the game thread. Pure: no context, no allocation, no IO, no
 * globals. Compiled with contraction and fast-math off like the rest of
 * sndmin, and it uses snd_sqrt rather than libm's, so its output is bit-stable
 * across platforms and lands unchanged in a journal. */
#include "sndmin.h"
#include "sndmin_dsp.h"
/* Ray against an axis-aligned box by the slab method: clip the ray's parameter
 * range against each axis in turn, and what survives is the span inside the
 * box. Returns the length of that span, or 0 for a miss, and writes the entry
 * distance to *entry. A ray starting inside the box is not a miss -- it gets
 * near = 0 and the span from there out -- because the listener standing in a
 * wall's volume must still be occluded by it. The 0.000001f floor makes a
 * grazing hit report as a hit rather than as a miss, so it still contributes a
 * surface. Pure. */
static float ray(vec3 p,vec3 d,const sndmin_box *b,float limit,float *entry) {
    const float origin[3]={p.x,p.y,p.z},dir[3]={d.x,d.y,d.z};
    const float lo[3]={b->min.x,b->min.y,b->min.z},hi[3]={b->max.x,b->max.y,b->max.z};
    float near=0,far=limit;
    for (unsigned k=0;k<3;++k) {
        if (snd_abs(dir[k])<1e-8f) { if(origin[k]<lo[k]||origin[k]>hi[k]) return 0; }
        else {
            float a=(lo[k]-origin[k])/dir[k],z=(hi[k]-origin[k])/dir[k];
            if(a>z) { const float t=a; a=z; z=t; }
            if(a>near) near=a;
            if(z<far) far=z;
            if(far<near) return 0;
        }
    }
    *entry=near; return far>near?far-near:0.000001f;
}
/* Normalise, with -Z (the default facing) standing in for a zero vector so
 * callers never have to guard. snd_sqrt rather than libm's: the whole file has
 * to be bit-reproducible, and libm's accuracy is not specified across
 * platforms. Pure. */
static vec3 unit(vec3 v) {
    const float d=snd_sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return d>0?min_vec3_scale(v,1/d):(vec3){0,0,-1};
}
sndmin_acoustic sndmin_acoustics(const sndmin_frame_desc *f,const sndmin_voice_desc *v,sndmin_layout layout) {
    sndmin_acoustic a={0};
    const vec3 delta=min_vec3_sub(v->position,f->listener),d=unit(delta);
    const float distance=snd_sqrt(min_vec3_dot(delta,delta));
    /* Note this fallback for max_radius differs from the one sndmin_play
     * applies through defaults(); see the comment there. */
    const float min=v->min_radius>0?v->min_radius:1,max=v->max_radius>min?v->max_radius:100;
    /* Inverse-distance falloff, times a linear taper to zero at max_radius.
     * Pure 1/r never reaches silence, so a distant source would keep costing a
     * voice and keep adding a whisper to the mix; the taper gives a definite
     * end without the audible edge a hard cutoff would have. */
    a.gain=distance<=min?1:(distance>=max?0:min/distance*(max-distance)/(max-min));
    /* Air absorption: high frequencies fade with distance faster than low
     * ones. This is the coefficient of the mixer's one-pole, so 1 is fully
     * open at zero distance and it closes smoothly from there. */
    a.lowpass=1/(1+distance*0.035f);
    /* Doppler, the textbook ratio with 343 m/s for the speed of sound and the
     * closing speeds projected onto the line between the two. The source speed
     * is clamped well below Mach 1 so the denominator can never reach zero,
     * and the ratio itself is clamped to an octave either way -- past that it
     * stops being a Doppler shift and starts being a bug the player hears. */
    a.doppler=snd_clamp((343+min_vec3_dot(f->velocity,d))/(343+snd_clamp(min_vec3_dot(v->velocity,d),-300,300)),0.5f,2);
    /* Occlusion. Every box the direct ray passes through takes a bite out of
     * gain and a bigger one out of the highs, weighted by how much of the ray
     * is inside it (capped at a metre) and by how much the material lets
     * through. Deliberately cumulative rather than "first hit wins": two walls
     * should muffle more than one. Boxes are not sorted and no ray is stopped
     * early, so cost is linear in box_count and independent of the layout. */
    for(uint32_t i=0;i<f->box_count;++i) {
        float entry=0;
        const float inside=ray(f->listener,d,&f->boxes[i],distance,&entry);
        const uint32_t m=f->boxes[i].material;
        const float transmission=m<f->material_count?snd_clamp(f->materials[m].transmission,0,1):0.2f;
        const float weight=snd_clamp(inside,0,1)*(1-transmission);
        a.gain*=1-weight*0.8f; a.lowpass*=1-weight*0.95f;
    }
    const vec3 forward=unit(f->forward);
    const vec3 up=min_vec3_dot(f->up,f->up)>0?unit(f->up):(vec3){0,1,0};
    const vec3 right=unit(min_vec3_cross(forward,up));
    const float x=min_vec3_dot(d,right),z=min_vec3_dot(d,forward);
    /* Panning. `x` is how far right the source is, `z` how far in front, both
     * in the listener's frame. Height is discarded: sndmin places sound around
     * the listener, never above. */
    if(layout==SNDMIN_STEREO) {
        /* Constant-power pan law. The two gains square-sum to one, so total
         * energy is the same wherever the source sits -- a linear pan would
         * dip by 3 dB in the middle, which sweeps audibly as a source
         * crosses. Front and back are indistinguishable in stereo. */
        a.pan[0]=snd_sqrt(0.5f*(1-snd_clamp(x,-1,1)));
        a.pan[1]=snd_sqrt(0.5f*(1+snd_clamp(x,-1,1)));
    } else {
        /* Pairwise vector-base amplitude panning. Walk the speakers in ring
         * order and find the adjacent pair the source direction falls between:
         * solving the 2x2 system gives the two gains whose weighted sum points
         * exactly at the source, and both are non-negative only for the pair
         * that actually encloses it -- which is the test the loop makes. Then
         * normalise to constant power, as above. Only ever two speakers sound,
         * which is what keeps the image tight instead of smeared over the ring.
         * Adjacent enclosing speaker pair on the horizontal unit circle.
         * WAVE order; LFE is never part of the panner. */
        /* Speaker azimuths in degrees, clockwise from straight ahead, indexed
         * in WAVE channel order (FL FR FC LFE BL BR SL SR). The LFE slot's
         * angle is never read. The order arrays list the speakers going once
         * around the ring, which is what makes "i and i+1" an adjacent pair.
         * snd_sin takes cycles, so /360 converts, and +0.25 of a cycle is the
         * cosine -- giving (x right, z forward) to match dx and dz. */
        const float angles[8]={-30,30,0,0,-150,150,-90,90};
        const unsigned order51[5]={4,0,2,1,5},order71[7]={4,6,0,2,1,7,5};
        const unsigned *order=layout==SNDMIN_51?order51:order71;
        const unsigned n=layout==SNDMIN_51?5:7;
        const float len=snd_sqrt(x*x+z*z),dx=len>0?x/len:0,dz=len>0?z/len:1;
        for(unsigned i=0;i<n;++i) {
            const unsigned l=order[i],r=order[(i+1)%n];
            const float lx=snd_sin(angles[l]/360),lz=snd_sin(angles[l]/360+0.25f);
            const float rx=snd_sin(angles[r]/360),rz=snd_sin(angles[r]/360+0.25f);
            const float det=lx*rz-rx*lz;
            if(snd_abs(det)<1e-6f) continue;
            const float gl=(dx*rz-rx*dz)/det,gr=(lx*dz-dx*lz)/det;
            if(gl>=-1e-6f&&gr>=-1e-6f) {
                const float norm=snd_sqrt(gl*gl+gr*gr);
                if(norm>0) { a.pan[l]=gl/norm; a.pan[r]=gr/norm; } break;
            }
        }
    }
    /* The probe. Sixteen fixed directions -- the six axes, the eight cube
     * diagonals, two more diagonals in the horizontal plane -- cast from the
     * listener to find the nearest surface in each. Fixed and not random on
     * purpose: a stochastic probe would make the room shimmer frame to frame
     * and would break replay. Sixteen is coarse, and the point is not an
     * accurate room but a stable one that changes smoothly as the listener
     * walks; the mixer's 0.1 s ramp absorbs what is left. 250 m is the horizon
     * -- a ray reaching it counts as escaped, not as a very large room. */
    static const vec3 dirs[16]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
        {1,1,1},{-1,1,1},{1,-1,1},{1,1,-1},{-1,-1,1},{-1,1,-1},{1,-1,-1},{-1,-1,-1},{1,0,1},{-1,0,-1}};
    float total=0; unsigned escaped=0;
    for(unsigned i=0;i<16;++i) {
        const vec3 direction=unit(dirs[i]);
        float nearest=250,absorption=0.4f;
        for(uint32_t j=0;j<f->box_count;++j) {
            float entry=0;
            if(ray(f->listener,direction,&f->boxes[j],250,&entry)>0&&entry<nearest) {
                nearest=entry;
                const uint32_t m=f->boxes[j].material;
                absorption=m<f->material_count?snd_clamp(f->materials[m].absorption,0,1):0.4f;
            }
        }
        total+=nearest;
        if(nearest==250) { ++escaped; continue; }
        /* An early reflection: source -> this surface -> listener. Really the
         * mirror path run backwards, which is why the listener's ray is what
         * finds the surface. The tap's delay is how much longer that bent path
         * is than the direct one, in seconds at 343 m/s; the floor of one
         * sample keeps a coincident reflection from landing on top of the
         * direct sound, and the ceiling stays inside SND_ECHO. Gain falls with
         * total path length and with how much the surface absorbed; the
         * lowpass is the surface's dullness, since an absorbent wall eats the
         * highs first. */
        const vec3 hit=min_vec3_add(f->listener,min_vec3_scale(direction,nearest));
        const vec3 to=min_vec3_sub(v->position,hit);
        const float path=nearest+snd_sqrt(min_vec3_dot(to,to));
        sndmin_tap tap={snd_clamp((path-distance)/343,1.0f/48000,0.49f),
            (1-absorption)/(1+path)*0.5f,1-0.9f*absorption};
        /* Keep the four loudest of the sixteen, by insertion: each tap either
         * displaces a quieter one and pushes the rest down, or falls off the
         * end. The mixer relies on the result being sorted loudest first, and
         * on unused slots being left at gain zero. */
        for(unsigned k=0;k<4;++k) if(tap.gain>a.taps[k].gain) {
            const sndmin_tap tmp=a.taps[k]; a.taps[k]=tap; tap=tmp;
        }
    }
    a.mean_free_path=total/16; a.openness=(float)escaped/16;
    /* Bigger room, longer tail, straight off the mean free path -- Sabine's
     * formula proper needs absorbing area and volume, which a box soup does
     * not give honestly. The floor keeps a cupboard from being anechoic. */
    a.decay=snd_clamp(0.2f+a.mean_free_path*0.08f,0.2f,8);
    return a;
}
