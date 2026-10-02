/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/mdns.h"

#include <errno.h>
#include <string.h>

#include <zephyr/net/dns_sd.h>
#include <zephyr/sys/byteorder.h>

/*
 * DNS-SD records live in a read-only iterable section, so the record itself
 * is constant and points at mutable storage for the instance name and the
 * port. A port of zero disables the record until cspot_mdns_advertise().
 */
static char cspot_sd_instance[DNS_SD_INSTANCE_MAX_SIZE + 1] = "cspot";
static uint16_t cspot_sd_port;

static const char cspot_sd_txt[] = "\x0b" "VERSION=1.0"
				   "\x13" "CPath=/spotify_info"
				   "\x08" "Stack=SP";

DNS_SD_REGISTER_SERVICE(cspot_sd, cspot_sd_instance, "_spotify-connect", "_tcp", "local",
			cspot_sd_txt, &cspot_sd_port);

int cspot_mdns_advertise(const char *instance_name, uint16_t port)
{
	if (instance_name == NULL || instance_name[0] == '\0' || port == 0U) {
		return -EINVAL;
	}

	strncpy(cspot_sd_instance, instance_name, DNS_SD_INSTANCE_MAX_SIZE);
	cspot_sd_instance[DNS_SD_INSTANCE_MAX_SIZE] = '\0';
	cspot_sd_port = sys_cpu_to_be16(port);
	return 0;
}

void cspot_mdns_withdraw(void)
{
	cspot_sd_port = 0U;
}
