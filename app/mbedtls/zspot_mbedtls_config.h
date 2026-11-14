/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Additions to Zephyr's Mbed TLS configuration (CONFIG_MBEDTLS_USER_CONFIG_FILE).
 */

#ifndef ZSPOT_MBEDTLS_CONFIG_H_
#define ZSPOT_MBEDTLS_CONFIG_H_

/*
 * Session tickets (RFC 5077) with TLS 1.2, which Zephyr only offers with
 * TLS 1.3. The servers of Spotify and of its CDNs resume a session by its
 * ticket, not by its id, so this is what makes
 * CONFIG_NET_SOCKETS_TLS_MAX_CLIENT_SESSION_COUNT take effect.
 */
#define MBEDTLS_SSL_SESSION_TICKETS

#endif /* ZSPOT_MBEDTLS_CONFIG_H_ */
