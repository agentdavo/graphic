/* min_jrnl.h -- the shared container for the journals that make this codebase
 * replayable. CLAUDE.md section 5 calls deterministic replay the most
 * underrated item on the list; this file is the part of it both libraries
 * share, and it is deliberately the least clever file in the tree.
 *
 * What it owns: the file magic, the packet frame, and byte-level read/write
 * with bounds. What it does not own, and must not learn: what any payload
 * means. vkmin and sndmin each write their own records into their own tagged
 * stream, version them independently, and a reader of one can skip the other
 * without being able to parse it. That separation is the whole design -- it is
 * why adding an audio field cannot invalidate a video journal.
 *
 * ---- file layout -----------------------------------------------------------
 *
 *   "JRNL" 01 00 00 00                 8-byte magic; the 1 is the FRAMING
 *                                      version, not the payload's
 *   then zero or more packets, each:
 *   tag  u32                           JRNL_VIDEO / JRNL_AUDIO / ...
 *   frame u32                          the frame the packet belongs to
 *   bytes u32                          payload length, <= JRNL_LIMIT
 *   payload                            `bytes` opaque bytes
 *
 * Every u32 in the magic and the packet frame is written a byte at a time,
 * least significant first, by jrnl_u32_write: the jrnl_packet struct is never
 * fwrite'd, so no padding, alignment or host endianness leaks into the
 * container. A reader on any machine can therefore walk a journal's packets and
 * find the stream it wants even when it cannot parse a single payload.
 *
 * The payloads themselves make no such promise, and vkmin's does not keep it --
 * jrnl_record IS fwrite'd as a struct. See the note on jrnl_record_write.
 *
 * ---- conventions -----------------------------------------------------------
 *
 *  - File IO belongs to the game thread. Nothing here is reentrant and nothing
 *    takes a lock.
 *  - No callbacks and no allocations, with one flagged exception:
 *    jrnl_stream_open creates a tmpfile.
 *  - Every function returns success as a bool and none of them report why. A
 *    false means the stream position is now unspecified: a partially-read
 *    packet leaves the cursor mid-record, so a caller must abandon the file
 *    rather than try the next packet.
 *  - All bounds are checked here because nobody else checks them. This is
 *    exactly the "vkmin's own invention" case CLAUDE.md section 7 carves out
 *    from the no-validation-layer rule -- a truncated journal is a file
 *    format problem, not a Vulkan one.
 */
#ifndef JRNL_H
#define JRNL_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* vkmin's own record inside a JRNL_VIDEO payload: one recorded API call, as an
 * op plus a fixed header, a variable body, and the relocations that turn stored
 * handles back into live ones. min_jrnl frames it and bounds it; only vkmin.c
 * knows what any op means. */
typedef struct { uint32_t op, hdr_bytes, data_bytes, reloc_count; } jrnl_record;
typedef struct { uint32_t offset, kind; } jrnl_reloc;
/* The container's own frame. Written field by field, never as a struct. */
typedef struct { uint32_t tag, frame, bytes; } jrnl_packet;
/* Stream tags. One per producer, so a reader keeps the stream it understands
 * and skips the rest; the values are file format and must never be renumbered.
 * JRNL_LIMIT (512 MiB) is the sanity bound on any single payload -- large
 * enough that nothing legitimate hits it, small enough that a corrupt length
 * field cannot make a reader try to allocate or skip an absurd amount. */
enum { JRNL_VIDEO = 1, JRNL_AUDIO = 2, JRNL_GAME = 3, JRNL_AUDIO_INFO = 0x204, JRNL_LIMIT = 512u << 20 };
static inline bool jrnl_bytes_write(FILE *f, const void *p, size_t n) {
    return n == 0 || (p && fwrite(p, 1, n, f) == n);
}
static inline bool jrnl_bytes_read(FILE *f, void *p, size_t n) {
    return n == 0 || (p && fread(p, 1, n, f) == n);
}
/* The bounds are the point: they are checked before the first byte is written,
 * so a rejected record leaves the stream untouched rather than half-written.
 * The three caps are vkmin's, not the format's -- they exist so a reader can
 * use fixed buffers (see jrnl_record_read's *_cap arguments) instead of
 * trusting a length that came out of a file.
 *
 * Note that `r` itself is fwrite'd as a struct, unlike jrnl_packet, and so are
 * the relocations. A record stream is therefore host-endian and depends on the
 * ABI packing four uint32_t with no padding -- true everywhere this builds, but
 * a property rather than a guarantee, and it does mean a vkmin journal is not
 * portable across endianness the way the container around it is. This is the
 * legacy vkmin record path; a new payload should frame its fields explicitly,
 * as the packet layer does. */
static inline bool jrnl_record_write(FILE *f, const jrnl_record *r, const void *hdr,
                                     const void *data, const jrnl_reloc *relocs) {
    return r->hdr_bytes <= 256 && r->data_bytes <= JRNL_LIMIT && r->reloc_count <= 4096 &&
        jrnl_bytes_write(f, r, sizeof *r) && jrnl_bytes_write(f, hdr, r->hdr_bytes) &&
        jrnl_bytes_write(f, data, r->data_bytes) && jrnl_bytes_write(f, relocs, r->reloc_count*sizeof *relocs);
}
/* The mirror of jrnl_record_write, but the bounds here are the CALLER's buffer
 * sizes rather than the format's constants: the record header has already been
 * read from the file and is not to be trusted, so every length in it is checked
 * against the space that actually exists before a byte is read. A false return
 * means the file disagrees with the caller's buffers and the stream position is
 * now unspecified. */
static inline bool jrnl_record_read(FILE *f, const jrnl_record *r, void *hdr, size_t hdr_cap,
                                    void *data, size_t data_cap, jrnl_reloc *relocs, size_t reloc_cap) {
    return r->hdr_bytes <= hdr_cap && r->data_bytes <= data_cap && r->reloc_count <= reloc_cap &&
        jrnl_bytes_read(f, hdr, r->hdr_bytes) && jrnl_bytes_read(f, data, r->data_bytes) &&
        (r->reloc_count == 0 || (relocs && fread(relocs, sizeof *relocs, r->reloc_count, f) == r->reloc_count));
}
/* Explicit little-endian, so the file says the same thing on a big-endian host.
 * The shift-and-mask is the whole portability story of the container; do not
 * "simplify" either of these to an fwrite of the uint32_t. */
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
/* Opens for read or write and gets the magic right in both directions: writing
 * emits it, reading demands it. Returning NULL on a magic mismatch rather than
 * a positioned stream means no caller can accidentally treat a foreign file as
 * an empty journal. The caller owns the FILE and must fclose it. */
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
/* Three-valued because "no more packets" and "the file is broken" are different
 * answers and a bool would merge them -- a truncated journal would then look
 * like a short but valid one, which is the failure a replay harness must never
 * silently accept. 1 = packet header read, 0 = clean EOF, -1 = malformed or
 * truncated. The one-byte peek is what distinguishes a clean end from a header
 * that starts and then runs out. On 1, the payload has NOT been consumed: read
 * it or jrnl_skip it before calling again. */
static inline int jrnl_next(FILE *f, jrnl_packet *p) {
    const int c=fgetc(f);
    if (c==EOF) return feof(f)?0:-1;
    if (ungetc(c,f)==EOF || !jrnl_u32_read(f,&p->tag) || !jrnl_u32_read(f,&p->frame) ||
        !jrnl_u32_read(f,&p->bytes) || p->bytes>JRNL_LIMIT) return -1;
    return 1;
}
/* Reads and discards rather than fseek'ing. Deliberate: fseek past the end of a
 * truncated file succeeds, and the next jrnl_next would then report a clean EOF
 * on a file that was actually cut short. Reading makes the truncation surface. */
static inline bool jrnl_skip(FILE *f, uint32_t bytes) {
    unsigned char scratch[1024];
    while (bytes) { const uint32_t n=bytes>sizeof scratch?(uint32_t)sizeof scratch:bytes;
        if (!jrnl_bytes_read(f,scratch,n)) return false;
        bytes-=n;
    } return true;
}
/* Extract one library's byte stream, concatenated, into a temporary file that
 * an existing single-stream replay implementation can read unchanged. This is
 * the adapter that let multi-stream journals arrive without rewriting either
 * library's replay path.
 *
 * Two file shapes go in and one comes out:
 *   - a file WITHOUT the JRNL magic is a legacy single-stream journal. It is
 *     already exactly what the caller wants, so it is rewound and returned as
 *     is, `tag` ignored. This is why the magic is read with fread and seek
 *     rather than jrnl_open: a mismatch is a valid outcome here, not an error.
 *   - a file WITH the magic is walked packet by packet; matching payloads are
 *     copied out and everything else is skipped.
 *
 * Returns NULL if the file is malformed, truncated, or simply contains no
 * packet with that tag -- an empty result would otherwise replay as a zero-frame
 * success. The returned FILE is owned by the caller and must be fclosed; for a
 * shared journal it is a tmpfile, which the C runtime removes on close.
 *
 * The one function here that allocates. Every failure path closes both the
 * source and the partially-written temporary before returning NULL; the caller
 * is never handed a half-copied stream. */
static inline FILE *jrnl_stream_open(const char *path,uint32_t tag) {
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    unsigned char magic[8];
    const size_t got=fread(magic,1,8,f);
    if(fseek(f,0,SEEK_SET)!=0) { fclose(f); return NULL; }
    const unsigned char shared[8]={'J','R','N','L',1,0,0,0};
    if(got!=8||memcmp(magic,shared,8)!=0) return f;
    if(!jrnl_bytes_read(f,magic,8)) { fclose(f); return NULL; }
    FILE *out=tmpfile(); if(!out) { fclose(f); return NULL; }
    jrnl_packet p; int result; bool ok=true,found=false;
    uint32_t last_frame=0;
    while(ok&&(result=jrnl_next(f,&p))==1) {
        if(p.tag!=tag) { ok=jrnl_skip(f,p.bytes); continue; }
        /* Video packets follow submission order, so a frame number going
         * backwards means the file is scrambled and the extraction is abandoned
         * -- replaying it would silently produce a wrong image rather than an
         * error. Scheduled audio may legitimately contain future notes before
         * earlier stream chunks, so it gets no such check. */
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
    if(fseek(out,0,SEEK_SET)!=0) { fclose(out); return NULL; }
    return out;
}
#endif
