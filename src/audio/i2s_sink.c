/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * cspot-zephyr: I2S PCM sink using the Zephyr I2S driver API.
 */
#include <zspot/zspot_i2s_sink.h>

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zspot, CONFIG_ZSPOT_LOG_LEVEL);

#define BLOCK_SIZE  CONFIG_ZSPOT_I2S_BLOCK_SIZE
#define BLOCK_COUNT CONFIG_ZSPOT_I2S_BLOCK_COUNT

K_MEM_SLAB_DEFINE_STATIC(zspot_i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static struct {
	const struct device *dev;
	struct k_mutex lock;
	void *block;
	size_t fill;
	bool started;
	int queued;
	uint16_t gain_q15;
} sink = {
	.gain_q15 = 32767,
};

int zspot_i2s_sink_init(const struct device *i2s_dev, uint32_t sample_rate,
			uint8_t channels, uint8_t bits_per_sample)
{
	struct i2s_config cfg = {
		.word_size = bits_per_sample,
		.channels = channels,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER,
		.frame_clk_freq = sample_rate,
		.mem_slab = &zspot_i2s_slab,
		.block_size = BLOCK_SIZE,
		.timeout = CONFIG_ZSPOT_I2S_TIMEOUT_MS,
	};
	int ret;

	if (i2s_dev == NULL || !device_is_ready(i2s_dev)) {
		return -ENODEV;
	}

	k_mutex_init(&sink.lock);
	sink.dev = i2s_dev;
	sink.started = false;
	sink.queued = 0;

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (ret < 0) {
		LOG_ERR("I2S TX configuration failed (%d)", ret);
		return ret;
	}

	LOG_INF("I2S sink ready: %u Hz, %u channels, %u bits", sample_rate, channels,
		bits_per_sample);
	return 0;
}

static void apply_gain(int16_t *samples, size_t count)
{
	if (sink.gain_q15 >= 32767) {
		return;
	}
	for (size_t i = 0; i < count; i++) {
		samples[i] = (int16_t)(((int32_t)samples[i] * sink.gain_q15) >> 15);
	}
}

/* Hands the current block to the driver; returns negative errno on failure. */
static int submit_block(void)
{
	int ret;

	apply_gain(sink.block, sink.fill / sizeof(int16_t));

	ret = i2s_write(sink.dev, sink.block, sink.fill);
	if (ret < 0) {
		k_mem_slab_free(&zspot_i2s_slab, sink.block);
		sink.block = NULL;
		if (ret == -EIO) {
			/* TX underrun: the controller is in ERROR state, re-arm it. */
			LOG_DBG("I2S underrun, re-arming");
			i2s_trigger(sink.dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			sink.started = false;
			sink.queued = 0;
			return 0;
		}
		return ret;
	}

	sink.block = NULL;
	sink.queued++;

	if (!sink.started && sink.queued >= BLOCK_COUNT / 2) {
		ret = i2s_trigger(sink.dev, I2S_DIR_TX, I2S_TRIGGER_START);
		if (ret < 0) {
			LOG_ERR("I2S start failed (%d)", ret);
			return ret;
		}
		sink.started = true;
	}
	return 0;
}

size_t zspot_i2s_sink_write(const uint8_t *pcm, size_t len, void *user_data)
{
	size_t written = 0;

	ARG_UNUSED(user_data);

	if (sink.dev == NULL) {
		return len; /* no output configured: discard */
	}

	k_mutex_lock(&sink.lock, K_FOREVER);

	while (written < len) {
		if (sink.block == NULL) {
			if (k_mem_slab_alloc(&zspot_i2s_slab, &sink.block,
					     K_MSEC(CONFIG_ZSPOT_I2S_TIMEOUT_MS)) != 0) {
				break; /* output is full, let the player retry */
			}
			sink.fill = 0;
		}

		size_t chunk = MIN(len - written, BLOCK_SIZE - sink.fill);

		memcpy((uint8_t *)sink.block + sink.fill, pcm + written, chunk);
		sink.fill += chunk;
		written += chunk;

		if (sink.fill == BLOCK_SIZE && submit_block() < 0) {
			break;
		}
	}

	k_mutex_unlock(&sink.lock);
	return written;
}

void zspot_i2s_sink_set_volume(uint16_t volume)
{
	sink.gain_q15 = volume >> 1;
}

void zspot_i2s_sink_flush(void)
{
	if (sink.dev == NULL) {
		return;
	}

	k_mutex_lock(&sink.lock, K_FOREVER);

	if (sink.block != NULL) {
		k_mem_slab_free(&zspot_i2s_slab, sink.block);
		sink.block = NULL;
	}
	if (sink.started) {
		i2s_trigger(sink.dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		sink.started = false;
	}
	sink.queued = 0;

	k_mutex_unlock(&sink.lock);
}
