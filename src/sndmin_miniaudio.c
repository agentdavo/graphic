/* Device layer only; no miniaudio engine, resource manager or decoding. */
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "sndmin_plat.h"
#include <stdlib.h>
struct sndmin_device { ma_device device; };
static void output(ma_device *d, void *out, const void *in, ma_uint32 frames) {
    (void)in; sndmin_mix(d->pUserData, out, frames);
}
sndmin_device *sndmin_device_open(sndmin_ctx *ctx, uint32_t channels, uint32_t rate) {
    sndmin_device *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32; config.playback.channels = channels;
    config.sampleRate = rate; config.dataCallback = output; config.pUserData = ctx;
    /* WAVE order: FL FR FC LFE BL BR SL SR. */
    const ma_channel map[8] = {MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_FRONT_CENTER,
        MA_CHANNEL_LFE, MA_CHANNEL_BACK_LEFT, MA_CHANNEL_BACK_RIGHT, MA_CHANNEL_SIDE_LEFT, MA_CHANNEL_SIDE_RIGHT};
    config.playback.pChannelMap = map;
    if (ma_device_init(NULL, &config, &d->device) != MA_SUCCESS) { free(d); return NULL; }
    return d;
}
bool sndmin_device_start(sndmin_device *d) { return ma_device_start(&d->device) == MA_SUCCESS; }
void sndmin_device_stop(sndmin_device *d) { (void)ma_device_stop(&d->device); }
void sndmin_device_close(sndmin_device *d) { ma_device_uninit(&d->device); free(d); }
uint32_t sndmin_device_rate(const sndmin_device *d) { return d->device.sampleRate; }
uint32_t sndmin_device_channels(const sndmin_device *d) { return d->device.playback.channels; }
bool sndmin_device_running(const sndmin_device *d) { return ma_device_is_started(&d->device) != 0; }
const char *sndmin_device_name(const sndmin_device *d) { return d->device.playback.name; }
