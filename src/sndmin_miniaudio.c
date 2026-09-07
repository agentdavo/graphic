/* Device layer only; no miniaudio engine, resource manager or decoding.
 *
 * The live counterpart to sndmin_null.c, and the second third-party
 * quarantine: miniaudio is compiled into this one object with -w, and the
 * MA_NO_* switches below cut it down to opening a playback device and calling
 * back. sndmin does its own mixing, synthesis, resampling and decoding, so
 * every one of those subsystems would be a second implementation of something
 * that already exists here -- and a non-deterministic one.
 *
 * The callback below is the audio thread. It runs at whatever period the OS
 * chose, it may not block, and everything it is allowed to touch is described
 * in sndmin_internal.h. Not exercised by any of omega's runs, which are all
 * offline; only the null backend and the offline path are covered there. */
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
/* The audio thread's entry point, and the only place the mixer is entered on a
 * live context. Playback only, so the capture buffer is ignored. Nothing is
 * done here but forward: any work added to this function is work done under
 * the callback's deadline, in a file the analysers do not check. */
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
