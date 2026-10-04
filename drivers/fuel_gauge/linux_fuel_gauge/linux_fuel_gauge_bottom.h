/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LINUX_FUEL_GAUGE_BOTTOM_H
#define LINUX_FUEL_GAUGE_BOTTOM_H

#include <stddef.h>

/* Read an integer sysfs attribute: /sys/class/power_supply/<base_path>/<attr> */
int linux_fuel_gauge_read(const char *base_path, const char *attr, long *value);

/* Read a string sysfs attribute: /sys/class/power_supply/<base_path>/<attr> */
int linux_fuel_gauge_read_buffer(const char *base_path, const char *attr, char *buf,
				 size_t buf_size);

#endif /* LINUX_FUEL_GAUGE_BOTTOM_H */
