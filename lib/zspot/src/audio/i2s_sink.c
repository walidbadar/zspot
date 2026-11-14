/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * zspot: I2S PCM sink using the Zephyr I2S driver API.
 *
 * The PCM always goes through the I2S driver. A codec that needs setting up
 * over a control bus can be passed in addition; it is then configured through
 * the audio codec API and muted while the playback is paused.
 */
#include <zspot/zspot_i2s_sink.h>

#include <errno.h>
#include <string.h>

#if defined(CONFIG_AUDIO_CODEC)
#include <zephyr/audio/codec.h>
#endif
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>


LOG_MODULE_DECLARE(zspot, CONFIG_ZSPOT_LOG_LEVEL);

#define BLOCK_SIZE  CONFIG_ZSPOT_I2S_BLOCK_SIZE
#define BLOCK_COUNT CONFIG_ZSPOT_I2S_BLOCK_COUNT

/*
 * The I2S blocks are DMA memory and only hold a fraction of a second. The
 * player, however, stops delivering whenever it fetches the next piece of the
 * track, for longer than that. With CONFIG_ZSPOT_I2S_BUFFER_MS the PCM first
 * goes into a FIFO that a feeder thread empties into the I2S blocks at
 * playback speed, so the player can decode ahead and the output keeps running
 * through those pauses.
 */
#define BYTES_PER_MS 176 /* 44.1 kHz, 16 bit, stereo; rounded down to whole frames */
#define FIFO_SIZE    (CONFIG_ZSPOT_I2S_BUFFER_MS * BYTES_PER_MS)
/* Collected before the output (re)starts, so that it does not run dry at once */
#define PREBUFFER    MIN(FIFO_SIZE / 2, 500 * BYTES_PER_MS)
/* Input that stopped growing for this long is played out as it is */
#define STALL_MS     300

/* Gain that leaves the samples as they are; gains are fractions of it */
#define GAIN_UNITY 32768U
/*
 * The gain moves by this much per frame: from silence to full volume, or
 * back, in about 20 ms. That goes for a change of the volume and for the
 * start and the end of the output, which would click otherwise.
 */
#define GAIN_STEP (GAIN_UNITY / (44100 * 20 / 1000))

/* With immediate logging a warning is formatted on this stack */
#define FEEDER_STACK_SIZE 4096
/* Above the player thread: the feeder only copies, and must not wait for it */
#define FEEDER_PRIORITY   K_PRIO_PREEMPT(1)

K_MEM_SLAB_DEFINE_STATIC(zspot_i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static struct {
	const struct device *dev;
	const struct device *codec;
	struct k_mutex lock;
	void *block;
	size_t fill;
	bool started;
	int queued;
	uint8_t channels;
	/* Set by the volume; the output follows it from where its gain is */
	volatile uint16_t gain_target;
	uint16_t gain;
} sink = {
	.gain_target = GAIN_UNITY,
};

#if FIFO_SIZE > 0
/* Too large for the internal RAM of the ESP32: placed in PSRAM there. */
#if defined(CONFIG_ESP_SPIRAM)
#define FIFO_SECTION Z_GENERIC_SECTION(.ext_ram.bss.zspot_i2s)
#else
#define FIFO_SECTION
#endif

static uint8_t fifo_memory[FIFO_SIZE] FIFO_SECTION;
static struct ring_buf fifo;
static K_SEM_DEFINE(fifo_data, 0, 1);
static atomic_t sink_paused;
/* The output was paused and has not been brought down to silence yet */
static atomic_t fade_out_due;
/* The output stopped: collect PREBUFFER before it starts again */
static bool prebuffering = true;

static void feeder_thread(void *p1, void *p2, void *p3);
K_THREAD_DEFINE(zspot_i2s_feeder, FEEDER_STACK_SIZE, feeder_thread, NULL, NULL, NULL,
		FEEDER_PRIORITY, 0, K_TICKS_FOREVER);
#endif /* FIFO_SIZE > 0 */

#if defined(CONFIG_AUDIO_CODEC)
/* The I2S controller drives the clocks: the codec is the target on both. */
static int codec_init(const struct device *codec, const struct i2s_config *i2s_cfg)
{
	struct audio_codec_cfg cfg = {
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_cfg.i2s = *i2s_cfg,
		.dai_route = AUDIO_ROUTE_PLAYBACK,
	};
	int ret;

	cfg.dai_cfg.i2s.options = I2S_OPT_FRAME_CLK_TARGET | I2S_OPT_BIT_CLK_TARGET;

	ret = audio_codec_configure(codec, &cfg);
	if (ret < 0) {
		LOG_ERR("Codec configuration failed (%d)", ret);
		return ret;
	}
	audio_codec_start_output(codec);
	return 0;
}

static void codec_set_mute(bool mute)
{
	audio_property_value_t val = {.mute = mute};
	int ret;

	if (sink.codec == NULL) {
		return;
	}
	ret = audio_codec_set_property(sink.codec, AUDIO_PROPERTY_OUTPUT_MUTE, AUDIO_CHANNEL_ALL,
				       val);
	if (ret == 0) {
		ret = audio_codec_apply_properties(sink.codec);
	}
	if (ret < 0) {
		LOG_DBG("Codec mute not applied (%d)", ret);
	}
}
#else
static void codec_set_mute(bool mute)
{
	ARG_UNUSED(mute);
}
#endif /* CONFIG_AUDIO_CODEC */

int zspot_i2s_sink_init(const struct device *i2s_dev, const struct device *codec_dev,
			uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
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
	if (codec_dev != NULL && (!IS_ENABLED(CONFIG_AUDIO_CODEC) || !device_is_ready(codec_dev))) {
		return -ENODEV;
	}

	if (channels == 0 || bits_per_sample != 16) {
		/* The gain works on 16-bit samples */
		return -ENOTSUP;
	}

	k_mutex_init(&sink.lock);
	sink.dev = i2s_dev;
	sink.started = false;
	sink.queued = 0;
	sink.channels = channels;
	sink.gain = 0;

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (ret < 0) {
		LOG_ERR("I2S TX configuration failed (%d)", ret);
		return ret;
	}

#if defined(CONFIG_AUDIO_CODEC)
	if (codec_dev != NULL) {
		ret = codec_init(codec_dev, &cfg);
		if (ret < 0) {
			return ret;
		}
		sink.codec = codec_dev;
	}
#endif

#if FIFO_SIZE > 0
	ring_buf_init(&fifo, sizeof(fifo_memory), fifo_memory);
	k_thread_name_set(zspot_i2s_feeder, "zspot_i2s");
	k_thread_start(zspot_i2s_feeder);
#endif

	LOG_INF("I2S sink ready: %u Hz, %u channels, %u bits, %u ms buffered ahead%s", sample_rate,
		channels, bits_per_sample, (unsigned int)CONFIG_ZSPOT_I2S_BUFFER_MS,
		codec_dev != NULL ? ", with codec" : "");
	return 0;
}

/*
 * Gain for a volume of 0..65535: silence at 0, GAIN_UNITY at 65535 and the
 * cube of the volume in between. Loudness is heard on a logarithmic scale,
 * which a gain in proportion to the volume would squeeze into the bottom of
 * the range.
 */
static uint16_t gain_from_volume(uint16_t volume)
{
	const uint64_t full = 65535U;
	uint64_t cube = (uint64_t)volume * volume * volume;

	/* Rounded; the largest product is just below 2^63 */
	return (uint16_t)((cube * GAIN_UNITY + full * full * full / 2) / (full * full * full));
}

/*
 * Applies a gain to interleaved 16-bit frames, moving it from @p current
 * towards @p target by @p step per frame, so that a change is not heard as a
 * click. Returns the gain of the last frame, to go on with.
 */
static uint16_t gain_apply(uint16_t current, uint16_t target, uint16_t step, int16_t *samples,
			   size_t frames, unsigned int channels)
{
	if (current == target && current == GAIN_UNITY) {
		return current;
	}

	for (size_t frame = 0; frame < frames; frame++) {
		if (current < target) {
			current = (target - current > step) ? current + step : target;
		} else if (current > target) {
			current = (current - target > step) ? current - step : target;
		}

		for (unsigned int channel = 0; channel < channels; channel++) {
			*samples = (int16_t)(((int32_t)*samples * current) >> 15);
			samples++;
		}
	}
	return current;
}

/* Applies the volume to sink.block, or brings the gain down to nothing over it. */
static void apply_gain(bool fade_out)
{
	sink.gain = gain_apply(sink.gain, fade_out ? 0 : sink.gain_target, GAIN_STEP,
				     sink.block, sink.fill / (sink.channels * sizeof(int16_t)),
				     sink.channels);
}

/*
 * An underrun is audible as a gap: the audio was not delivered as fast as it
 * is played. One is expected after a pause or between tracks; a steady stream
 * of them means the player cannot keep up. Reported at most every five
 * seconds, with the number since the last report.
 */
static void report_underrun(void)
{
	static int64_t last_report_ms;
	static unsigned int count;
	int64_t now = k_uptime_get();

	count++;
	if (now - last_report_ms >= 5000) {
		LOG_WRN("I2S output ran dry %u time(s)", count);
		last_report_ms = now;
		count = 0;
	}
}

/*
 * Hands sink.block over to the driver and starts the output when due. With
 * @p fade_out the block ends in silence, for an output that stops after it.
 */
static int submit_block(bool fade_out)
{
	int ret;

	apply_gain(fade_out);

	ret = i2s_write(sink.dev, sink.block, sink.fill);
	if (ret == -EIO) {
		/*
		 * TX underrun: the output ran dry and the controller is in the
		 * ERROR state. Re-arm it and queue the block again, so that the
		 * gap is not followed by lost audio as well. The gain it got
		 * came from where the output was; what follows fades in.
		 */
		report_underrun();
		sink.gain = 0;
		i2s_trigger(sink.dev, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
		sink.started = false;
		sink.queued = 0;
#if FIFO_SIZE > 0
		prebuffering = true;
#endif
		ret = i2s_write(sink.dev, sink.block, sink.fill);
	}
	if (ret < 0) {
		k_mem_slab_free(&zspot_i2s_slab, sink.block);
		sink.block = NULL;
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

#if FIFO_SIZE > 0

static void feeder_thread(void *p1, void *p2, void *p3)
{
	uint32_t last_level = 0;
	int64_t last_growth_ms = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		uint32_t level = ring_buf_size_get(&fifo);
		int64_t now = k_uptime_get();
		bool fade_out = false;
		bool stalled;
		void *block;

		if (level != last_level) {
			last_level = level;
			last_growth_ms = now;
		}
		stalled = now - last_growth_ms >= STALL_MS;

		if (atomic_get(&sink_paused)) {
			/*
			 * The driver plays what it was handed and stops. One more
			 * block ends that in silence instead of at full level.
			 */
			fade_out = atomic_cas(&fade_out_due, 1, 0) && sink.started &&
				   level >= BLOCK_SIZE;
			if (!fade_out) {
				k_sem_take(&fifo_data, K_MSEC(20));
				continue;
			}
		} else if (level == 0 || (prebuffering && level < PREBUFFER && !stalled) ||
			   (level < BLOCK_SIZE && !stalled)) {
			/*
			 * Wait for more unless there is a whole block to send, or
			 * the input stopped and what is left has to be played out.
			 */
			k_sem_take(&fifo_data, K_MSEC(20));
			continue;
		}

		/* Paces the feeder: a block only becomes free once it was played. */
		if (k_mem_slab_alloc(&zspot_i2s_slab, &block, K_MSEC(100)) != 0) {
			continue;
		}

		k_mutex_lock(&sink.lock, K_FOREVER);
		sink.block = block;
		sink.fill = ring_buf_get(&fifo, block, BLOCK_SIZE);
		if (sink.fill == 0) {
			/* Flushed in the meantime */
			k_mem_slab_free(&zspot_i2s_slab, block);
			sink.block = NULL;
		} else {
			prebuffering = false;
			(void)submit_block(fade_out);
		}
		last_level = ring_buf_size_get(&fifo);
		k_mutex_unlock(&sink.lock);
	}
}

size_t zspot_i2s_sink_write(const uint8_t *pcm, size_t len, void *user_data)
{
	size_t written;

	ARG_UNUSED(user_data);

	if (sink.dev == NULL) {
		return len; /* no output configured: discard */
	}

	/* Whole stereo frames only, whatever fits; the player retries the rest. */
	k_mutex_lock(&sink.lock, K_FOREVER);
	written = ring_buf_put(&fifo, pcm, MIN(len, ring_buf_space_get(&fifo)) & ~3U);
	k_mutex_unlock(&sink.lock);

	k_sem_give(&fifo_data);
	return written;
}

void zspot_i2s_sink_set_paused(bool paused)
{
	atomic_set(&fade_out_due, paused);
	atomic_set(&sink_paused, paused);
	k_sem_give(&fifo_data);
	codec_set_mute(paused);
}

size_t zspot_i2s_sink_buffered(void)
{
	return ring_buf_size_get(&fifo);
}

#else /* FIFO_SIZE == 0: the caller writes straight into the I2S blocks */

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

		if (sink.fill == BLOCK_SIZE && submit_block(false) < 0) {
			break;
		}
	}

	k_mutex_unlock(&sink.lock);
	return written;
}

void zspot_i2s_sink_set_paused(bool paused)
{
	codec_set_mute(paused);
}

size_t zspot_i2s_sink_buffered(void)
{
	return 0;
}

#endif /* FIFO_SIZE > 0 */

void zspot_i2s_sink_set_volume(uint16_t volume)
{
	sink.gain_target = gain_from_volume(volume);
}

void zspot_i2s_sink_flush(void)
{
	if (sink.dev == NULL) {
		return;
	}

	k_mutex_lock(&sink.lock, K_FOREVER);
#if FIFO_SIZE > 0
	ring_buf_reset(&fifo);
	prebuffering = true;
#endif
	if (sink.block != NULL) {
		k_mem_slab_free(&zspot_i2s_slab, sink.block);
		sink.block = NULL;
	}
	/* Also blocks that are queued while the output has not started yet */
	if (sink.started || sink.queued > 0) {
		i2s_trigger(sink.dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		sink.started = false;
	}
	sink.queued = 0;
	/* What comes next fades in */
	sink.gain = 0;
	k_mutex_unlock(&sink.lock);
}
