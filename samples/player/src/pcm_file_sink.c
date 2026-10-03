/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * PCM sink for native_sim: appends the decoded audio to a host file, playable
 * with e.g. "aplay -f S16_LE -r 44100 -c 2 /tmp/cspot.pcm".
 */

#include "pcm_file_sink.h"

#include <errno.h>

#include <nsi_host_trampolines.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

/* Host open(2) flags (Linux values). */
#define HOST_O_WRONLY 01
#define HOST_O_CREAT  0100
#define HOST_O_TRUNC  01000
#define HOST_MODE_RW  0644

/*
 * nsi_host_open() has no mode argument, which O_CREAT needs; call the host
 * libc directly for this one function. The symbol resolves to the host libc
 * at link time, like the trampolines do.
 */
extern int open(const char *pathname, int flags, ...);

static int pcm_fd = -1;

int pcm_file_sink_init(const char *path)
{
	pcm_fd = open(path, HOST_O_WRONLY | HOST_O_CREAT | HOST_O_TRUNC, HOST_MODE_RW);
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

	if (pcm_fd < 0) {
		return len;
	}

	long written = nsi_host_write(pcm_fd, pcm, len);

	return written < 0 ? 0 : (size_t)written;
}
