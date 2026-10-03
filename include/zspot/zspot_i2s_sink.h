/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * cspot-zephyr: ready-made PCM sink on top of the Zephyr I2S driver API.
 * Enabled with CONFIG_ZSPOT_I2S_SINK.
 */
#ifndef ZSPOT_ZSPOT_I2S_SINK_H_
#define ZSPOT_ZSPOT_I2S_SINK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;

/** Configures the I2S controller for TX. */
int zspot_i2s_sink_init(const struct device *i2s_dev, uint32_t sample_rate,
			uint8_t channels, uint8_t bits_per_sample);

/**
 * Queues PCM for playback. Has the zspot_pcm_cb_t signature so it can be
 * passed straight to zspot_connect().
 */
size_t zspot_i2s_sink_write(const uint8_t *pcm, size_t len, void *user_data);

/** Software volume, 0..65535 (65535 = unity gain). */
void zspot_i2s_sink_set_volume(uint16_t volume);

/** Drops queued audio, e.g. on seek or track change. */
void zspot_i2s_sink_flush(void);

/**
 * Stops or resumes the output of the audio buffered ahead
 * (CONFIG_ZSPOT_I2S_BUFFER_MS); without it, playback would go on for that
 * long after the application stopped writing.
 */
void zspot_i2s_sink_set_paused(bool paused);

/**
 * Bytes accepted by zspot_i2s_sink_write() that are not played yet, to derive
 * the audible position from the amount written.
 */
size_t zspot_i2s_sink_buffered(void);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_ZSPOT_I2S_SINK_H_ */
