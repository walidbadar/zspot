/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Spotify Web API access with the user's own developer application. The
 * session's access token is issued to Spotify's own client and is rate
 * limited on the public Web API (HTTP 429), so requests are authorised with
 * a token obtained through OAuth instead: with a client ID, a client secret
 * and a refresh token, the latter produced once by
 * scripts/spotify_authorize.py.
 *
 * These credentials are the ones that the script sent to the device, or else
 * those of the build (CONFIG_ZSPOT_WEB_CLIENT_ID, _CLIENT_SECRET and
 * _REFRESH_TOKEN). The script sends them to an HTTP endpoint
 * (CONFIG_ZSPOT_WEB_SETUP), so that they need not be part of the firmware:
 *
 *   POST /webapi   client_id=...&client_secret=...&refresh_token=...
 *   GET  /webapi   says whether credentials are set
 *
 * Nothing is ever given out. Credentials are taken while the device has
 * none, or for a few minutes after the Wi-Fi settings were opened on its
 * screen: to replace them takes somebody at the device. They cross the
 * local network unencrypted.
 */

#ifndef ZSPOT_SAMPLE_WEBAPI_H_
#define ZSPOT_SAMPLE_WEBAPI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- The credentials ------------------------------------------------------- */

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

/* --- Requests -------------------------------------------------------------- */

/**
 * Takes up the stored credentials, or those of the build. Reads the flash:
 * call from the main thread, before anything else here.
 */
void webapi_init(void);

/** Whether there are credentials for the requests. */
bool webapi_configured(void);

/**
 * Stores @p keys and uses them from now on. Writes to the flash: not from a
 * thread whose stack is in external RAM.
 *
 * @return 0 on success or a negative errno; the keys in use stay then
 */
int webapi_set_keys(const struct web_keys *keys);

/**
 * Performs a Web API request, fetching or renewing the access token first
 * when necessary. Not reentrant: call from one thread only.
 *
 * @param body  JSON request body or NULL
 * @param buf   receives the response body, not NUL terminated
 * @param len   receives the number of bytes stored in @p buf
 * @return the HTTP status code, -ENOENT when the application credentials are
 *         not configured, -EACCES when Spotify rejected them, or another
 *         negative errno.
 */
int webapi_request(const char *method, const char *url, const char *body, uint8_t *buf,
		   size_t size, size_t *len);

/* --- Setup over the network ------------------------------------------------ */

#if defined(CONFIG_ZSPOT_WEB_SETUP)

/** Starts the endpoint. Call once the network is up and after ui_init(). */
int websetup_init(void);

/** The Wi-Fi settings were opened: take new credentials for a while. */
void websetup_allow(void);

#else

static inline int websetup_init(void)
{
	return 0;
}

static inline void websetup_allow(void)
{
}

#endif /* CONFIG_ZSPOT_WEB_SETUP */

#endif /* ZSPOT_SAMPLE_WEBAPI_H_ */
