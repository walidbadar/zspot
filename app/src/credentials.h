/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Spotify credentials kept across restarts (CONFIG_ZSPOT_CREDENTIALS_STORED):
 * the reusable credentials of a session are stored, and the device signs in
 * with them at the next start instead of waiting to be picked in the Spotify
 * app.
 *
 * They go through Zephyr's secure storage, which encrypts and authenticates
 * them (AES-GCM) before they reach the settings. The key is derived from the
 * ID of the device: a copy of the flash does not give them away by itself,
 * but whoever also reads the device ID can work the key out.
 *
 * The functions share a buffer: call them from one thread, after zspot_init().
 */

#ifndef ZSPOT_SAMPLE_CREDENTIALS_H_
#define ZSPOT_SAMPLE_CREDENTIALS_H_

#include <errno.h>

#if defined(CONFIG_ZSPOT_CREDENTIALS_STORED)

/**
 * Hands the stored credentials to the library.
 *
 * @return 0 on success, -ENOENT when none are stored, or another negative
 *         errno. Credentials that the library cannot read are removed.
 */
int credentials_load(void);

/**
 * Stores the credentials the library holds: after zspot_connect(), the
 * reusable ones of the session.
 *
 * @return 0 on success or a negative errno.
 */
int credentials_store(void);

/** Removes the stored credentials, e.g. after Spotify rejected them. */
int credentials_forget(void);

#else

static inline int credentials_load(void)
{
	return -ENOENT;
}

static inline int credentials_store(void)
{
	return -ENOTSUP;
}

static inline int credentials_forget(void)
{
	return 0;
}

#endif /* CONFIG_ZSPOT_CREDENTIALS_STORED */

#endif /* ZSPOT_SAMPLE_CREDENTIALS_H_ */
