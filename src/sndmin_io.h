/* The decoder boundary. No sndmin translation unit includes a third-party
 * header except the two quarantined ones: everything that parses a file lives
 * behind the six functions below, implemented in sndmin_io.c alone (the other
 * quarantine, sndmin_miniaudio.c, is the device). That is what keeps
 * the vendored decoders out of the engine's warning set and out of its
 * compile flags -- and what makes them replaceable.
 *
 * sndmin_decode reads a whole file at once, for sounds; sndmin_reader_* stream
 * one incrementally, for music. All are game-thread and all block on IO, so
 * none of them may be called from anywhere below the mixer boundary; the
 * decoded PCM crosses to the mixer inside commands, never as a pointer.
 * sndmin_png is here only because the offline spectrogram needs a writer and
 * this is already where the image library is. */
#ifndef SNDMIN_IO_H
#define SNDMIN_IO_H
#include <stdbool.h>
#include <stdint.h>
typedef struct sndmin_reader sndmin_reader;
float *sndmin_decode(const char *, uint64_t *frames, uint32_t *channels, uint32_t *rate);
sndmin_reader *sndmin_reader_open(const char *, uint32_t *channels, uint32_t *rate);
uint32_t sndmin_reader_read(sndmin_reader *, float *, uint32_t frames);
bool sndmin_reader_rewind(sndmin_reader *);
void sndmin_reader_close(sndmin_reader *);
bool sndmin_png(const char *, uint32_t width, uint32_t height, const unsigned char *rgb);
#endif
