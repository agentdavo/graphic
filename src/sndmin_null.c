#include "sndmin_plat.h"
#include <stdlib.h>
struct sndmin_device { uint32_t rate, channels; bool running; };
sndmin_device *sndmin_device_open(sndmin_ctx *ctx, uint32_t channels, uint32_t rate) {
    (void)ctx;
    sndmin_device *d = calloc(1, sizeof *d);
    if (d) { d->rate = rate; d->channels = channels; } return d;
}
bool sndmin_device_start(sndmin_device *d) { d->running = true; return true; }
void sndmin_device_stop(sndmin_device *d) { d->running = false; }
void sndmin_device_close(sndmin_device *d) { free(d); }
uint32_t sndmin_device_rate(const sndmin_device *d) { return d->rate; }
uint32_t sndmin_device_channels(const sndmin_device *d) { return d->channels; }
bool sndmin_device_running(const sndmin_device *d) { return d->running; }
const char *sndmin_device_name(const sndmin_device *d) { (void)d; return "null"; }
