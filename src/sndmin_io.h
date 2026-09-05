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
