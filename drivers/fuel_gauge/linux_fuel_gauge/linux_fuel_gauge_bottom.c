/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <nsi_tracing.h>

#define POWER_SUPPLY_NODE "/sys/class/power_supply"

int linux_fuel_gauge_read_buffer(const char *base_path, const char *attr, char *buf,
				 size_t buf_size)
{
	char path[sizeof(POWER_SUPPLY_NODE) + strlen(base_path) + strlen(attr) + 2];
	int fd;
	int ret;

	(void)snprintf(path, sizeof(path), POWER_SUPPLY_NODE "/%s/%s", base_path, attr);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		nsi_print_warning("Failed to open %s: %s\n", path, strerror(errno));
		return -1;
	}

	ret = read(fd, buf, buf_size - 1);
	if (ret <= 0) {
		nsi_print_warning("Read error on %s: %s\n", path,
				  (ret < 0) ? strerror(errno) : "no data");
		close(fd);
		return -1;
	}

	close(fd);

	if (buf[ret - 1] == '\n') {
		ret--;
	}

	buf[ret] = '\0';

	return 0;
}

int linux_fuel_gauge_read(const char *base_path, const char *attr, long *value)
{
	char buf[32];

	if (linux_fuel_gauge_read_buffer(base_path, attr, buf, sizeof(buf)) != 0) {
		return -1;
	}

	*value = strtol(buf, NULL, 10);

	return 0;
}
