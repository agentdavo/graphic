#include "sndmin_internal.h"
#include <stdlib.h>
#include <string.h>
static bool u16(FILE *f,uint16_t x) {
    const unsigned char b[2]={(unsigned char)x,(unsigned char)(x>>8)};
    return jrnl_bytes_write(f,b,2);
}
/* 512-point Hann STFT, 256 frequency bins, time advances left-to-right.
 * FFT is diagnostic only; golden audio never depends on visualization maths. */
static void spectrum(const float *window,unsigned char *rgb,uint32_t width,uint32_t column) {
    float real[512],imag[512]={0};
    for(unsigned i=0;i<512;++i) {
        unsigned reverse=0; for(unsigned k=0;k<9;++k) reverse=(reverse<<1)|((i>>k)&1);
        real[reverse]=window[i]*(0.5f-0.5f*snd_sin((float)i/511+0.25f));
    }
    for(unsigned size=2;size<=512;size*=2) for(unsigned at=0;at<512;at+=size) for(unsigned j=0;j<size/2;++j) {
        const float angle=-(float)j/(float)size,wr=snd_sin(angle+0.25f),wi=snd_sin(angle);
        const unsigned a=at+j,b=a+size/2;
        const float re=real[b]*wr-imag[b]*wi,im=real[b]*wi+imag[b]*wr;
        real[b]=real[a]-re; imag[b]=imag[a]-im; real[a]+=re; imag[a]+=im;
    }
    for(unsigned y=0;y<256;++y) {
        const float power=(real[y]*real[y]+imag[y]*imag[y])/(256*256);
        uint32_t bits; memcpy(&bits,&power,4);
        const float log2=(float)((int)(bits>>23)-127)+(float)(bits&0x7fffffu)/8388608;
        const float brightness=snd_clamp((log2+24)/24,0,1);
        const size_t at=((size_t)(255-y)*width+column)*3;
        rgb[at]=(unsigned char)(255*snd_clamp(brightness*2,0,1));
        rgb[at+1]=(unsigned char)(255*snd_clamp(brightness*2-0.5f,0,1));
        rgb[at+2]=(unsigned char)(255*snd_clamp(brightness*3-2,0,1));
    }
}
bool sndmin_write_output(sndmin_ctx *c,uint32_t frames,const char *wav,const char *png) {
    const uint64_t total=(uint64_t)frames*800,bytes=total*c->channels*2;
    if(bytes>UINT32_MAX-68) return false;
    FILE *f=fopen(wav,"wb"); if(!f) return false;
    const bool surround=c->channels>2;
    bool ok=jrnl_bytes_write(f,"RIFF",4)&&jrnl_u32_write(f,(uint32_t)bytes+(surround?60:36))&&
        jrnl_bytes_write(f,"WAVEfmt ",8)&&jrnl_u32_write(f,surround?40:16)&&u16(f,surround?0xfffe:1)&&
        u16(f,(uint16_t)c->channels)&&jrnl_u32_write(f,48000)&&jrnl_u32_write(f,48000*c->channels*2)&&
        u16(f,(uint16_t)(c->channels*2))&&u16(f,16);
    if(surround) {
        const unsigned char guid[16]={1,0,0,0,0,0,16,0,128,0,0,170,0,56,155,113};
        ok=ok&&u16(f,22)&&u16(f,16)&&jrnl_u32_write(f,c->channels==6?0x3f:0x63f)&&jrnl_bytes_write(f,guid,16);
    }
    ok=ok&&jrnl_bytes_write(f,"data",4)&&jrnl_u32_write(f,(uint32_t)bytes);
    const uint32_t width=total/512<1024?(uint32_t)(total/512):1024;
    unsigned char *rgb=png?calloc((size_t)width*256,3):NULL;
    if(png&&!rgb) ok=false;
    float samples[800*8],history[512]={0}; uint32_t head=0,column=0;
    unsigned char encoded[800*8*2]; size_t index=0;
    for(uint64_t at=0;ok&&at<total;) {
        /* Keep future commands out of the queue: stream chunks stamped at at
         * must never be trapped behind a future event. Split at event positions. */
        ok=sndmin_feed(c,at,&index); if(!ok) break;
        sndmin_pump_streams(c,at);
        uint32_t count=800;
        if(index<c->pending_count&&c->pending[index].sample>at&&c->pending[index].sample-at<count)
            count=(uint32_t)(c->pending[index].sample-at);
        if(total-at<count) count=(uint32_t)(total-at);
        sndmin_mix(c,samples,count);
        for(uint32_t i=0;i<count;++i) {
            float mono=0;
            for(unsigned ch=0;ch<c->channels;++ch) {
                const size_t offset=(size_t)i*c->channels+ch;
                const float x=samples[offset]; mono+=x/(float)c->channels;
                const float dither=snd_noise(at+i,0x6d2b79f5u+ch)*0.49f;
                const float scaled=snd_clamp(x*32767+dither,-32768,32767);
                const int32_t signed_sample=(int32_t)(scaled+(scaled>=0?0.5f:-0.5f));
                const uint16_t value=(uint16_t)signed_sample;
                encoded[offset*2]=(unsigned char)value; encoded[offset*2+1]=(unsigned char)(value>>8);
            }
            history[head++%512]=mono;
            if(rgb&&column<width&&at+i+1>=(uint64_t)(column+1)*total/width) {
                float ordered[512]; for(unsigned k=0;k<512;++k) ordered[k]=history[(head+k)%512];
                spectrum(ordered,rgb,width,column++);
            }
        }
        ok=jrnl_bytes_write(f,encoded,(size_t)count*c->channels*2); at+=count;
    }
    if(fclose(f)!=0) ok=false;
    if(ok&&png) ok=sndmin_png(png,width,256,rgb);
    free(rgb); (void)sndmin_stats_get(c); return ok;
}
bool sndmin_replay(sndmin_ctx *c,const char *path) {
    if(!c||c->failed||!path||!c->desc.offline||c->started||c->sound_count||c->patch_count||c->pending_count||c->stream_count||c->song_count) return false;
    FILE *f=jrnl_open(path,false); if(!f) return false;
    FILE *saved=c->journal; c->journal=NULL; c->replaying=true;
    bool ok=true; jrnl_packet packet; int result=0;
    uint32_t meta[3]={0}; bool info_seen=false;
    while(ok&&(result=jrnl_next(f,&packet))==1) {
        if(packet.tag==JRNL_AUDIO_INFO) {
            uint32_t info[7];
            ok=!info_seen&&!c->pending_count&&!c->sound_count&&!c->patch_count&&!c->stream_count&&packet.frame==0&&
                packet.bytes==sizeof info&&jrnl_bytes_read(f,info,sizeof info)&&info[0]==1&&info[1]==SNDMIN_RATE&&
                info[2]==SNDMIN_FRAME_SAMPLES&&info[3]==c->channels&&info[4]==sizeof(snd_command)&&
                info[5]==sizeof(sndmin_patch_desc)&&info[6]==0x01020304u;
            info_seen=true;
        } else if(packet.tag==JRNL_AUDIO) {
            snd_command cmd={0};
            ok=packet.bytes==sizeof cmd&&jrnl_bytes_read(f,&cmd,sizeof cmd)&&sndmin_command_valid(c,&cmd)&&cmd.sample/800==packet.frame;
            if(ok) sndmin_submit(c,cmd);
        } else if(packet.tag==0x200) {
            ok=packet.bytes==sizeof meta&&jrnl_bytes_read(f,meta,sizeof meta)&&meta[0]==c->sound_count+1&&
                meta[1]>0&&meta[1]<(1u<<26)&&(meta[2]==1||meta[2]==2);
        } else if(packet.tag==0x201) {
            ok=meta[1]>0&&packet.bytes==(uint64_t)meta[1]*meta[2]*4;
            if(ok) {
                float *pcm=malloc(packet.bytes);
                if(!pcm) ok=false;
                else { ok=jrnl_bytes_read(f,pcm,packet.bytes);
                    if(ok) ok=sndmin_make_sound(c,(sndmin_bytes){pcm,packet.bytes},meta[2],48000).id==meta[0];
                    free(pcm); }
            }
            meta[1]=0;
        } else if(packet.tag==0x202) {
            sndmin_patch_desc patch;
            ok=packet.bytes==sizeof patch&&jrnl_bytes_read(f,&patch,sizeof patch)&&sndmin_make_patch(c,&patch).id!=0;
        } else if(packet.tag==0x203) {
            uint32_t stream[3];
            ok=packet.bytes==sizeof stream&&jrnl_bytes_read(f,stream,sizeof stream)&&stream[0]==c->stream_count+1&&
                stream[0]<=SND_STREAMS&&(stream[1]==1||stream[1]==2)&&stream[2]>=8000&&stream[2]<=192000;
            if(ok) { c->stream_mix[c->stream_count].channels=stream[1]; c->stream_mix[c->stream_count++].rate=stream[2]; }
        } else if(packet.tag==JRNL_VIDEO||packet.tag==JRNL_GAME) ok=jrnl_skip(f,packet.bytes);
        else ok=false;
    }
    c->journal=saved; c->replaying=false; fclose(f);
    if(!ok||result!=0||meta[1]) c->failed=true;
    return !c->failed;
}
