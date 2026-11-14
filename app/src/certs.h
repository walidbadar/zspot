/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * The root certificates that the servers are verified against (app/certs),
 * handed to Zephyr under the tags that the library names for its TLS
 * connections: CONFIG_ZSPOT_TLS_SEC_TAG and the CONFIG_ZSPOT_TLS_SEC_TAG_COUNT
 * that follow. With a tag of -1 nothing is verified, and nothing registered.
 */

#ifndef ZSPOT_SAMPLE_CERTS_H_
#define ZSPOT_SAMPLE_CERTS_H_

/**
 * Registers the root certificates. Call before the first connection.
 *
 * @return 0 on success or a negative errno
 */
int certs_register(void);

#endif /* ZSPOT_SAMPLE_CERTS_H_ */
