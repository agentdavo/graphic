/* Offline output and journal replay: the two paths that drive the mixer
 * without a sound card, and the reason sndmin can be tested at all.
 *
 * sndmin_write_output runs the whole engine on the calling thread, in blocks,
 * writing 16-bit PCM as it goes -- so a render costs one WAV's worth of memory
 * whatever its length. sndmin_replay reads a journal back into a fresh context
 * so the same commands produce the same samples. Both are game-thread, both do
 * IO, and neither may be entered on a live context.
 *
 * These WAVs are the project's golden test: 16-bit output is a total order on
 * the float mix, so a hash comparison catches any arithmetic drift at all.
 * That is what -ffp-contract=off -fno-fast-math is protecting. */
#include "sndmin_internal.h"
#include <stdlib.h>
#include <string.h>
/* Little-endian 16-bit, since WAV is little-endian and the host might not be. */
static bool u16(FILE *f,uint16_t x) {
    const unsigned char b[2]={(unsigned char)x,(unsigned char)(x>>8)};
    return jrnl_bytes_write(f,b,2);
}
/* One column of a spectrogram: a 512-point Hann-windowed short-time Fourier
 * transform, giving 256 usable bins from DC to Nyquist, painted bottom-up so
 * low frequencies sit at the bottom. Radix-2 decimation-in-time, which is why
 * the input is written in bit-reversed order first -- after that the butterfly
 * passes run in place. Brightness is log magnitude over a 24-octave floor,
 * read straight out of the float's exponent rather than through a log call.
 *
 * FFT is diagnostic only; golden audio never depends on visualization maths.
 * Nothing here feeds back into the mix, so this is the one place in sndmin
 * where an arithmetic change would be harmless. Unexercised by omega, which
 * always passes a null spectrogram path. */
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
    /* Plain 16-bit PCM for stereo; WAVE_FORMAT_EXTENSIBLE beyond it, because
     * only the extensible header carries a channel mask, and without one a
     * player has to guess which speaker each channel belongs to. */
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
    /* The offline main loop, and the offline substitute for the game/mixer
     * handshake: feed the commands due now, top the stream buffers up, then
     * mix as far as the next event. Blocks are therefore ragged rather than a
     * fixed 800, and that is the point -- a command lands on its exact sample,
     * so a render is not merely deterministic but agrees with what the live
     * path would have produced from the same journal. */
    for(uint64_t at=0;ok&&at<total;) {
        /* Keep future commands out of the queue: stream chunks stamped at at
         * must never be trapped behind a future event. Split at event positions. */
        ok=sndmin_feed(c,at,&index); if(!ok) break;
        sndmin_pump_streams(c,at);
        uint32_t count=800;
        if(index<c->pending_count&&c->pending[index].sample>at&&c->pending[index].sample-at<count) {
            count=(uint32_t)(c->pending[index].sample-at);
        }
        if(total-at<count) count=(uint32_t)(total-at);
        sndmin_mix(c,samples,count);
        for(uint32_t i=0;i<count;++i) {
            float mono=0;
            for(unsigned ch=0;ch<c->channels;++ch) {
                const size_t offset=(size_t)i*c->channels+ch;
                const float x=samples[offset]; mono+=x/(float)c->channels;
                /* Dither. Rounding float to 16-bit without it correlates the
                 * quantisation error with the signal, which is audible on a
                 * fade as a buzz rather than as hiss. Just under half an LSB
                 * of noise decorrelates it. Derived from the sample index and
                 * channel, not from a generator with state, so the dither is
                 * part of the deterministic result and two renders of the same
                 * score hash the same. */
                const float dither=snd_noise(at+i,0x6d2b79f5u+ch)*0.49f;
                const float scaled=snd_clamp(x*32767+dither,-32768,32767);
                /* Round half away from zero. C's cast truncates, which would
                 * bias every sample towards silence. */
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
/* Rebuild a context from a recorded journal: resources first, then every
 * command, staged exactly as the original run staged them. Only legal on a
 * fresh offline context -- the guard below is what enforces that, since
 * replaying into a context that already has sounds or patches would renumber
 * the very handles the recorded commands refer to.
 *
 * Recorded commands are treated as untrusted input, not as data this build
 * wrote: every one goes through sndmin_command_valid, and the frame stamp must
 * agree with the sample. A journal is a file on disk, and a malformed or
 * truncated one must fail the replay rather than reach the mixer. */
bool sndmin_replay(sndmin_ctx *c,const char *path) {
    if(!c||c->failed||!path||!c->desc.offline||c->started||c->sound_count||c->patch_count||c->pending_count||c->stream_count||c->song_count) return false;
    FILE *f=jrnl_open(path,false); if(!f) return false;
    /* Journalling off for the duration, restored below: commands read out of
     * one journal must not be echoed into another the context happens to be
     * recording to. sndmin_submit tests both the pointer and the flag. */
    FILE *saved=c->journal; c->journal=NULL; c->replaying=true;
    bool ok=true; jrnl_packet packet; int result=0;
    uint32_t meta[3]={0}; bool info_seen=false;
    /* The header declares how wide a patch record is. Journals frozen before
     * the orchestral fields existed carry the shorter one; those fields are
     * appended and read back as zero, which is the behaviour those journals
     * recorded. Accepting the declared width keeps the frozen fixtures usable
     * as regressions instead of regenerating them from the code under test. */
    uint32_t patch_bytes=sizeof(sndmin_patch_desc);
    while(ok&&(result=jrnl_next(f,&packet))==1) {
        if(packet.tag==JRNL_AUDIO_INFO) {
            uint32_t info[7];
            ok=!info_seen&&!c->pending_count&&!c->sound_count&&!c->patch_count&&!c->stream_count&&packet.frame==0&&
                packet.bytes==sizeof info&&jrnl_bytes_read(f,info,sizeof info)&&info[0]==1&&info[1]==SNDMIN_RATE&&
                info[2]==SNDMIN_FRAME_SAMPLES&&info[3]==c->channels&&info[4]==sizeof(snd_command)&&
                (info[5]==sizeof(sndmin_patch_desc)||info[5]==SNDMIN_PATCH_BYTES_V1)&&info[6]==0x01020304u;
            if(ok) patch_bytes=info[5];
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
            /* The header declares the width when there is a header; the oldest
             * fixtures were frozen before it existed and declare nothing, so
             * either known width is accepted there. Never a width we do not
             * know: a short read into a zeroed struct is only safe for a
             * layout this build is certain is a prefix of its own. */
            sndmin_patch_desc patch={0};
            ok=(packet.bytes==patch_bytes||(!info_seen&&packet.bytes==SNDMIN_PATCH_BYTES_V1))&&
                packet.bytes<=sizeof patch&&
                jrnl_bytes_read(f,&patch,packet.bytes)&&sndmin_make_patch(c,&patch).id!=0;
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
