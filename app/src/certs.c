/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "certs.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#if CONFIG_ZSPOT_TLS_SEC_TAG >= 0

static const unsigned char digicert_global_root_g2[] = {
#include "certs/digicert_global_root_g2.der.inc"
};

static const unsigned char starfield_root_g2[] = {
#include "certs/starfield_root_g2.der.inc"
};

static const unsigned char digicert_global_root_g3[] = {
#include "certs/digicert_global_root_g3.der.inc"
};

static const unsigned char globalsign_root_r3[] = {
#include "certs/globalsign_root_r3.der.inc"
};

static const unsigned char gts_root_r4[] = {
#include "certs/gts_root_r4.der.inc"
};

static const struct {
	const unsigned char *der;
	size_t len;
} roots[] = {
	{digicert_global_root_g2, sizeof(digicert_global_root_g2)},
	{starfield_root_g2, sizeof(starfield_root_g2)},
	{digicert_global_root_g3, sizeof(digicert_global_root_g3)},
	{globalsign_root_r3, sizeof(globalsign_root_r3)},
	{gts_root_r4, sizeof(gts_root_r4)},
};

/* The library offers this many tags to a connection */
BUILD_ASSERT(ARRAY_SIZE(roots) == CONFIG_ZSPOT_TLS_SEC_TAG_COUNT,
	     "CONFIG_ZSPOT_TLS_SEC_TAG_COUNT is not the number of root certificates");

int certs_register(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(roots); i++) {
		int ret = tls_credential_add(CONFIG_ZSPOT_TLS_SEC_TAG + i,
					     TLS_CREDENTIAL_CA_CERTIFICATE, roots[i].der,
					     roots[i].len);

		if (ret != 0) {
			LOG_ERR("Cannot register root certificate %u (%d)", (unsigned int)i, ret);
			return ret;
		}
	}
	return 0;
}

#else

int certs_register(void)
{
	return 0;
}

#endif /* CONFIG_ZSPOT_TLS_SEC_TAG >= 0 */
