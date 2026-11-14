/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * The credentials for the Spotify Web API (see webapi.h): the client ID and
 * the secret of the user's developer application and the refresh token of the
 * authorised user. Checked, parsed from the form that the setup endpoint
 * receives, and kept in the settings.
 */

#ifndef ZSPOT_SAMPLE_WEB_KEYS_H_
#define ZSPOT_SAMPLE_WEB_KEYS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Spotify's are 32 characters each, with the terminator 33 */
#define WEB_KEYS_ID_MAX     48
#define WEB_KEYS_SECRET_MAX 48
/** About 130 characters at present */
#define WEB_KEYS_TOKEN_MAX  320

/** Stored as it is: web_keys_set() leaves no stray bytes behind the texts. */
struct web_keys {
	/** Tells the layout; set by web_keys_set() */
	uint32_t format;
	char client_id[WEB_KEYS_ID_MAX];
	char client_secret[WEB_KEYS_SECRET_MAX];
	char refresh_token[WEB_KEYS_TOKEN_MAX];
};

/**
 * Fills in @p keys.
 *
 * @return false when a value is missing, too long or has characters other
 *         than letters, digits, '-' and '_', which is what the requests take
 *         without escaping; @p keys is not usable then.
 */
bool web_keys_set(struct web_keys *keys, const char *client_id, const char *client_secret,
		  const char *refresh_token);

/**
 * Fills in @p keys from "client_id=...&client_secret=...&refresh_token=...",
 * in any order; other fields are ignored. @p len bytes are looked at.
 *
 * @return false as web_keys_set() does
 */
bool web_keys_from_form(struct web_keys *keys, const char *form, size_t len);

/**
 * Reads the stored keys.
 *
 * @return 0 on success, -ENOENT when none are stored, or another negative
 *         errno. Stored keys that cannot be used are removed.
 */
int web_keys_load(struct web_keys *keys);

/**
 * Stores @p keys, which web_keys_set() or web_keys_from_form() filled in.
 * Writes to the flash: not from a thread whose stack is in external RAM.
 *
 * @return 0 on success or a negative errno
 */
int web_keys_store(const struct web_keys *keys);

/** Removes the stored keys. */
int web_keys_forget(void);

#endif /* ZSPOT_SAMPLE_WEB_KEYS_H_ */
