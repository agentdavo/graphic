#ifndef SNDMIN_PLAT_H
#define SNDMIN_PLAT_H
#include "sndmin.h"
typedef struct sndmin_device sndmin_device;
/* Eight-function device surface; null and miniaudio implement the same ABI.
 * Exactly one backend is linked -- there is no dispatch and no vtable, so the
 * choice is a build decision, and sndmin_null is what makes a build with no
 * sound card, and no platform audio headers, still a complete engine.
 *
 * These are game-thread calls. The device the backend opens is expected to
 * call sndmin_mix from its own thread between start and stop; everything
 * either side of that boundary is documented in sndmin_internal.h. */
sndmin_device *sndmin_device_open(sndmin_ctx *, uint32_t channels, uint32_t rate);
bool sndmin_device_start(sndmin_device *);
void sndmin_device_stop(sndmin_device *);
void sndmin_device_close(sndmin_device *);
uint32_t sndmin_device_rate(const sndmin_device *);
uint32_t sndmin_device_channels(const sndmin_device *);
bool sndmin_device_running(const sndmin_device *);
const char *sndmin_device_name(const sndmin_device *);
/* Internal entry; only device thread, or offline owner. No allocation or IO.
 * `samples` counts frames, not floats: the buffer holds that many groups of
 * sndmin_device_channels values, interleaved. */
void sndmin_mix(sndmin_ctx *, float *interleaved, uint32_t samples);
/* True on this thread only while inside sndmin_mix; a test harness interposes
 * malloc/fopen/clock and uses it to prove the mixer called none of them. */
bool sndmin_callback_active(void);
#endif
