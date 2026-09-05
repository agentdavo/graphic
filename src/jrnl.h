/* jrnl.h -- tagged plain-data packets. File IO belongs to the game thread.
 * Little-endian framing; payload schemas/versioning belong to the libraries.
 * A packet carries library tag, frame and bytes. No callbacks or allocations.
 * Legacy vkmin records use the same bounded body writer below. */
#ifndef JRNL_H
#define JRNL_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint32_t op, hdr_bytes, data_bytes, reloc_count; } jrnl_record;
typedef struct { uint32_t offset, kind; } jrnl_reloc;
typedef struct { uint32_t tag, frame, bytes; } jrnl_packet;
enum { JRNL_VIDEO = 1, JRNL_AUDIO = 2, JRNL_GAME = 3, JRNL_AUDIO_INFO = 0x204, JRNL_LIMIT = 512u << 20 };
static inline bool jrnl_bytes_write(FILE *f, const void *p, size_t n) {
    return n == 0 || (p && fwrite(p, 1, n, f) == n);
}
static inline bool jrnl_bytes_read(FILE *f, void *p, size_t n) {
    return n == 0 || (p && fread(p, 1, n, f) == n);
}
static inline bool jrnl_record_write(FILE *f, const jrnl_record *r, const void *hdr,
                                     const void *data, const jrnl_reloc *relocs) {
    return r->hdr_bytes <= 256 && r->data_bytes <= JRNL_LIMIT && r->reloc_count <= 4096 &&
        jrnl_bytes_write(f, r, sizeof *r) && jrnl_bytes_write(f, hdr, r->hdr_bytes) &&
        jrnl_bytes_write(f, data, r->data_bytes) && jrnl_bytes_write(f, relocs, r->reloc_count*sizeof *relocs);
}
static inline bool jrnl_record_read(FILE *f, const jrnl_record *r, void *hdr, size_t hdr_cap,
                                    void *data, size_t data_cap, jrnl_reloc *relocs, size_t reloc_cap) {
    return r->hdr_bytes <= hdr_cap && r->data_bytes <= data_cap && r->reloc_count <= reloc_cap &&
        jrnl_bytes_read(f, hdr, r->hdr_bytes) && jrnl_bytes_read(f, data, r->data_bytes) &&
        jrnl_bytes_read(f, relocs, r->reloc_count*sizeof *relocs);
}
static inline bool jrnl_u32_write(FILE *f, uint32_t v) {
    unsigned char b[4];
    for (unsigned i=0; i<4; ++i) b[i]=(unsigned char)(v>>(8*i));
    return jrnl_bytes_write(f,b,4);
}
static inline bool jrnl_u32_read(FILE *f, uint32_t *v) {
    unsigned char b[4];
    if (!jrnl_bytes_read(f,b,4)) return false;
    *v=0; for (unsigned i=0;i<4;++i) *v |= (uint32_t)b[i]<<(8*i);
    return true;
}
static inline FILE *jrnl_open(const char *path, bool write) {
    FILE *f=fopen(path,write?"wb":"rb");
    if (!f) return NULL;
    const unsigned char magic[8]={'J','R','N','L',1,0,0,0};
    unsigned char read[8];
    const bool ok=write ? jrnl_bytes_write(f,magic,8) :
        (jrnl_bytes_read(f,read,8) && memcmp(magic,read,8)==0);
    if (!ok) { fclose(f); return NULL; } return f;
}
static inline bool jrnl_begin(FILE *f, jrnl_packet p) {
    return p.bytes <= JRNL_LIMIT && jrnl_u32_write(f,p.tag) && jrnl_u32_write(f,p.frame) &&
        jrnl_u32_write(f,p.bytes);
}
static inline bool jrnl_write(FILE *f, jrnl_packet p, const void *data) {
    return jrnl_begin(f,p) && jrnl_bytes_write(f,data,p.bytes);
}
/* 1 = packet header, 0 = clean EOF, -1 = malformed/truncated. Read/skip payload next. */
static inline int jrnl_next(FILE *f, jrnl_packet *p) {
    const int c=fgetc(f);
    if (c==EOF) return feof(f)?0:-1;
    if (ungetc(c,f)==EOF || !jrnl_u32_read(f,&p->tag) || !jrnl_u32_read(f,&p->frame) ||
        !jrnl_u32_read(f,&p->bytes) || p->bytes>JRNL_LIMIT) return -1;
    return 1;
}
static inline bool jrnl_skip(FILE *f, uint32_t bytes) {
    unsigned char scratch[1024];
    while (bytes) { const uint32_t n=bytes>sizeof scratch?(uint32_t)sizeof scratch:bytes;
        if (!jrnl_bytes_read(f,scratch,n)) return false;
        bytes-=n;
    } return true;
}
/* Extract one library's byte stream for an existing replay implementation.
 * Legacy files pass through. Shared packets are checked, never fseek-skipped
 * beyond EOF. The returned FILE is owned by the caller. */
static inline FILE *jrnl_stream_open(const char *path,uint32_t tag) {
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    unsigned char magic[8];
    const size_t got=fread(magic,1,8,f); rewind(f);
    const unsigned char shared[8]={'J','R','N','L',1,0,0,0};
    if(got!=8||memcmp(magic,shared,8)!=0) return f;
    if(!jrnl_bytes_read(f,magic,8)) { fclose(f); return NULL; }
    FILE *out=tmpfile(); if(!out) { fclose(f); return NULL; }
    jrnl_packet p; int result; bool ok=true,found=false;
    uint32_t last_frame=0;
    while(ok&&(result=jrnl_next(f,&p))==1) {
        if(p.tag!=tag) { ok=jrnl_skip(f,p.bytes); continue; }
        /* Video packets follow submission order. Scheduled audio may contain
         * future notes before earlier stream chunks, so it has no such rule. */
        if(tag==JRNL_VIDEO&&found&&p.frame<last_frame) { ok=false; break; }
        last_frame=p.frame;
        found=true; unsigned char data[4096];
        while(ok&&p.bytes) {
            const uint32_t n=p.bytes>sizeof data?(uint32_t)sizeof data:p.bytes;
            ok=jrnl_bytes_read(f,data,n)&&jrnl_bytes_write(out,data,n); p.bytes-=n;
        }
    }
    fclose(f);
    if(!ok||result<0||!found) { fclose(out); return NULL; }
    rewind(out); return out;
}
#endif
