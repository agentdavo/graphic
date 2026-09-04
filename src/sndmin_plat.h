#ifndef SNDMIN_PLAT_H
#define SNDMIN_PLAT_H
#include "sndmin.h"
typedef struct sndmin_device sndmin_device;
/* Eight-function device surface; null and miniaudio implement the same ABI. */
sndmin_device *sndmin_device_open(sndmin_ctx *, uint32_t channels, uint32_t rate);
bool sndmin_device_start(sndmin_device *);
void sndmin_device_stop(sndmin_device *);
void sndmin_device_close(sndmin_device *);
uint32_t sndmin_device_rate(const sndmin_device *);
uint32_t sndmin_device_channels(const sndmin_device *);
bool sndmin_device_running(const sndmin_device *);
const char *sndmin_device_name(const sndmin_device *);
/* Internal entry; only device thread, or offline owner. No allocation or IO. */
void sndmin_mix(sndmin_ctx *, float *interleaved, uint32_t samples);
bool sndmin_callback_active(void);
#endif
