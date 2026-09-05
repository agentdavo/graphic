/* Third party only; quarantined under the repository's third-party flags. */
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "stb_vorbis.c"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#include "sndmin_io.h"
#include <stdlib.h>
#include <string.h>
struct sndmin_reader { stb_vorbis *vorbis; uint32_t channels; };
sndmin_reader *sndmin_reader_open(const char *path, uint32_t *channels, uint32_t *rate) {
    int error = 0;
    stb_vorbis *v = stb_vorbis_open_filename(path, &error, NULL);
    if (!v) return NULL;
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.channels < 1 || info.channels > 2) { stb_vorbis_close(v); return NULL; }
    sndmin_reader *r = calloc(1, sizeof *r);
    if (!r) { stb_vorbis_close(v); return NULL; }
    r->vorbis = v; r->channels = (uint32_t)info.channels;
    *channels = r->channels; *rate = info.sample_rate; return r;
}
uint32_t sndmin_reader_read(sndmin_reader *r, float *out, uint32_t frames) {
    return (uint32_t)stb_vorbis_get_samples_float_interleaved(r->vorbis, (int)r->channels, out, (int)(frames*r->channels));
}
bool sndmin_reader_rewind(sndmin_reader *r) { return stb_vorbis_seek_start(r->vorbis) != 0; }
void sndmin_reader_close(sndmin_reader *r) { if (r) { stb_vorbis_close(r->vorbis); free(r); } }
float *sndmin_decode(const char *path, uint64_t *frames, uint32_t *channels, uint32_t *rate) {
    unsigned int ch = 0, hz = 0; drwav_uint64 count = 0;
    float *data = drwav_open_file_and_read_pcm_frames_f32(path, &ch, &hz, &count, NULL);
    if (data) { *channels = ch; *rate = hz; *frames = count; return data; }
    sndmin_reader *r = sndmin_reader_open(path, channels, rate);
    if (!r) return NULL;
    const uint32_t n = stb_vorbis_stream_length_in_samples(r->vorbis);
    if (!n || n > (1u<<27)) { sndmin_reader_close(r); return NULL; }
    data = calloc((size_t)n * *channels, sizeof(float));
    uint32_t done = 0;
    if (data) while (done < n) {
        uint32_t got = sndmin_reader_read(r, data + (size_t)done * *channels, n-done);
        if (!got) break;
        done += got;
    }
    sndmin_reader_close(r); *frames = done; return data;
}
bool sndmin_png(const char *p, uint32_t w, uint32_t h, const unsigned char *rgb) {
    return stbi_write_png(p, (int)w, (int)h, 3, rgb, (int)(w*3)) != 0;
}
