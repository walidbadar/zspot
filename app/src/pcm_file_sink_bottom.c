/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Host side of the native_sim PCM sink. This file is built in the native
 * simulator's runner context, against the host libc, and must not use the
 * Zephyr API.
 */

#include "pcm_file_sink_bottom.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int pcm_host_open_file(const char *path)
{
	return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

int pcm_host_open_command(const char *command)
{
	FILE *pipe;
	int fd;

	/* A player that exits must not take the simulator down with SIGPIPE. */
	signal(SIGPIPE, SIG_IGN);

	pipe = popen(command, "w");
	if (pipe == NULL) {
		return -1;
	}

	/*
	 * The simulator runs all Zephyr threads on one host thread, so a write
	 * that waits for the player would stall the whole system.
	 */
	fd = fileno(pipe);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

long pcm_host_write(int fd, const void *data, unsigned long len)
{
	long written = write(fd, data, len);

	if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
		return 0;
	}
	return written;
}
