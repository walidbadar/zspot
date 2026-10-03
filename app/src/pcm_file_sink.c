/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * PCM sink for native_sim: pipes the decoded audio into a host command that
 * plays it (e.g. "aplay -q -f S16_LE -r 44100 -c 2"), or writes it to a host
 * file. The audio is accepted at playback speed, like a DAC would consume it,
 * so that tracks last as long as they should.
 */

#include "pcm_file_sink.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pcm_file_sink_bottom.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define BYTES_PER_MS 176 /* 44.1 kHz, 16 bit, stereo */
/* How far the writer may run ahead of real time */
#define LEAD_MS      500

static int pcm_fd = -1;

/* Start of the current uninterrupted stretch of audio and its length so far */
static int64_t stretch_start_ms;
static int64_t stretch_bytes;

int pcm_file_sink_init(const char *command, const char *path)
{
	if (command[0] != '\0') {
		pcm_fd = pcm_host_open_command(command);
		if (pcm_fd < 0) {
			LOG_ERR("Cannot start \"%s\" for PCM output", command);
			return -EIO;
		}
		LOG_INF("Playing PCM with \"%s\"", command);
		return 0;
	}

	pcm_fd = pcm_host_open_file(path);
	if (pcm_fd < 0) {
		LOG_ERR("Cannot open %s for PCM output", path);
		return -EIO;
	}

	LOG_INF("Writing PCM to %s", path);
	return 0;
}

size_t pcm_file_sink_write(const uint8_t *pcm, size_t len, void *user_data)
{
	ARG_UNUSED(user_data);

	int64_t due_bytes = (k_uptime_get() - stretch_start_ms) * BYTES_PER_MS;
	long written = len;

	if (stretch_bytes + LEAD_MS * BYTES_PER_MS < due_bytes) {
		/* The stream stalled (start, pause, seek): pace from here on. */
		stretch_start_ms = k_uptime_get();
		stretch_bytes = 0;
	} else if (stretch_bytes > due_bytes + LEAD_MS * BYTES_PER_MS) {
		return 0; /* ahead of real time, the player retries shortly */
	}

	/* Without an output the audio is discarded, still at playback speed. */
	if (pcm_fd >= 0) {
		written = pcm_host_write(pcm_fd, pcm, len);
		if (written < 0) {
			/* E.g. the player is not installed or was closed. */
			LOG_ERR("PCM output failed, carrying on without sound");
			pcm_fd = -1;
			written = len;
		}
	}

	stretch_bytes += written;
	return (size_t)written;
}
