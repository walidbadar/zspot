/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Host side of the native_sim PCM player. The native_sim I2S driver writes
 * the audio to a host file; with CONFIG_ZSPOT_PCM_COMMAND that file is the
 * standard input of a player such as aplay. The player is started here and
 * its pipe is placed at a fixed descriptor number, which the driver opens
 * through /dev/fd/<n> (the driver's output path, a Kconfig default).
 *
 * This file is built in the native simulator's runner context, against the
 * host libc, and must not use the Zephyr API.
 */

#include "pcm_command_bottom.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nsi_tracing.h>

/* Kept open for the lifetime of the simulator */
static FILE *player;

/* Whether the program the command starts with can be found (like a shell does). */
static int program_exists(const char *command)
{
	char program[256];
	size_t len = strcspn(command, " \t");

	if (len == 0 || len >= sizeof(program)) {
		return 0;
	}
	memcpy(program, command, len);
	program[len] = '\0';

	if (strchr(program, '/') != NULL) {
		return access(program, X_OK) == 0;
	}

	const char *path = getenv("PATH");

	if (path == NULL) {
		path = "/usr/local/bin:/usr/bin:/bin";
	}
	while (*path != '\0') {
		char candidate[1024];
		size_t dir_len = strcspn(path, ":");

		if (dir_len + 1 + len < sizeof(candidate)) {
			memcpy(candidate, path, dir_len);
			candidate[dir_len] = '/';
			memcpy(candidate + dir_len + 1, program, len + 1);
			if (access(candidate, X_OK) == 0) {
				return 1;
			}
		}
		path += dir_len;
		if (*path == ':') {
			path++;
		}
	}
	return 0;
}

/* Puts @p fd at @p target; the player must not inherit it from a later popen(). */
static int install(int fd, int target)
{
	if (dup2(fd, target) < 0) {
		return -errno;
	}
	(void)fcntl(target, F_SETFD, FD_CLOEXEC);
	return 0;
}

int pcm_command_start(const char *command, const char *path)
{
	int target;
	char trailing;
	int ret;

	if (sscanf(path, "/dev/fd/%d%c", &target, &trailing) != 1 || target < 3) {
		return -EINVAL;
	}

	if (fcntl(target, F_GETFD) >= 0) {
		nsi_print_warning("Descriptor %d (%s) is already in use: no PCM player\n",
				  target, path);
		return -EBUSY;
	}

	if (!program_exists(command)) {
		ret = -ENOENT;
		goto discard;
	}

	/* A player that exits must not take the simulator down with SIGPIPE. */
	(void)signal(SIGPIPE, SIG_IGN);

	player = popen(command, "w");
	if (player == NULL) {
		ret = -errno;
		goto discard;
	}

	ret = install(fileno(player), target);
	if (ret < 0) {
		pclose(player);
		player = NULL;
		goto discard;
	}
	return 0;

discard:
	/* The driver still finds something to write to: the audio is dropped. */
	{
		int null_fd = open("/dev/null", O_WRONLY);

		if (null_fd >= 0) {
			(void)install(null_fd, target);
			close(null_fd);
		}
	}
	return ret;
}
