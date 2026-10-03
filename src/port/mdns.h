/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief DNS-SD advertisement of _spotify-connect._tcp via Zephyr's mDNS responder.
 */

#ifndef ZSPOT_PORT_MDNS_H_
#define ZSPOT_PORT_MDNS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Publishes the service instance; the record is served by the mDNS responder. */
int zspot_mdns_advertise(const char *instance_name, uint16_t port);

/** Stops answering for the service. */
void zspot_mdns_withdraw(void);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_PORT_MDNS_H_ */
