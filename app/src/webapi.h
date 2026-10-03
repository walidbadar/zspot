/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Spotify Web API access with the user's own developer application. The
 * session's access token is issued to Spotify's own client and is rate
 * limited on the public Web API (HTTP 429), so requests are authorised with
 * a token obtained through OAuth instead: CONFIG_ZSPOT_SAMPLE_WEB_CLIENT_ID,
 * _CLIENT_SECRET and _REFRESH_TOKEN, the latter produced once by
 * scripts/spotify_authorize.py.
 */

#ifndef ZSPOT_SAMPLE_WEBAPI_H_
#define ZSPOT_SAMPLE_WEBAPI_H_

#include <stddef.h>
#include <stdint.h>

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

#endif /* ZSPOT_SAMPLE_WEBAPI_H_ */
