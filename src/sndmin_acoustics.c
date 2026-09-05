#include "sndmin.h"
#include "sndmin_dsp.h"
#include <float.h>
/* Slab intersection, including starts inside geometry; returns segment length. */
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
static vec3 unit(vec3 v) {
    const float d=snd_sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return d>0?vkmin_vec3_scale(v,1/d):(vec3){0,0,-1};
}
sndmin_acoustic sndmin_acoustics(const sndmin_frame_desc *f,const sndmin_voice_desc *v,sndmin_layout layout) {
    sndmin_acoustic a={0};
    const vec3 delta=vkmin_vec3_sub(v->position,f->listener),d=unit(delta);
    const float distance=snd_sqrt(vkmin_vec3_dot(delta,delta));
    const float min=v->min_radius>0?v->min_radius:1,max=v->max_radius>min?v->max_radius:100;
    a.gain=distance<=min?1:(distance>=max?0:min/distance*(max-distance)/(max-min));
    a.lowpass=1/(1+distance*0.035f);
    a.doppler=snd_clamp((343+vkmin_vec3_dot(f->velocity,d))/(343+snd_clamp(vkmin_vec3_dot(v->velocity,d),-300,300)),0.5f,2);
    for(uint32_t i=0;i<f->box_count;++i) {
        float entry=0;
        const float inside=ray(f->listener,d,&f->boxes[i],distance,&entry);
        const uint32_t m=f->boxes[i].material;
        const float transmission=m<f->material_count?snd_clamp(f->materials[m].transmission,0,1):0.2f;
        const float weight=snd_clamp(inside,0,1)*(1-transmission);
        a.gain*=1-weight*0.8f; a.lowpass*=1-weight*0.95f;
    }
    const vec3 forward=unit(f->forward);
    const vec3 up=vkmin_vec3_dot(f->up,f->up)>0?unit(f->up):(vec3){0,1,0};
    const vec3 right=unit(vkmin_vec3_cross(forward,up));
    const float x=vkmin_vec3_dot(d,right),z=vkmin_vec3_dot(d,forward);
    if(layout==SNDMIN_STEREO) {
        a.pan[0]=snd_sqrt(0.5f*(1-snd_clamp(x,-1,1)));
        a.pan[1]=snd_sqrt(0.5f*(1+snd_clamp(x,-1,1)));
    } else {
        /* Adjacent enclosing speaker pair on the horizontal unit circle.
         * WAVE order; LFE is never part of the panner. */
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
        const vec3 hit=vkmin_vec3_add(f->listener,vkmin_vec3_scale(direction,nearest));
        const vec3 to=vkmin_vec3_sub(v->position,hit);
        const float path=nearest+snd_sqrt(vkmin_vec3_dot(to,to));
        sndmin_tap tap={snd_clamp((path-distance)/343,1.0f/48000,0.49f),
            (1-absorption)/(1+path)*0.5f,1-0.9f*absorption};
        for(unsigned k=0;k<4;++k) if(tap.gain>a.taps[k].gain) {
            const sndmin_tap tmp=a.taps[k]; a.taps[k]=tap; tap=tmp;
        }
    }
    a.mean_free_path=total/16; a.openness=(float)escaped/16;
    a.decay=snd_clamp(0.2f+a.mean_free_path*0.08f,0.2f,8);
    return a;
}
