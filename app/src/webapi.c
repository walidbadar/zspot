/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "webapi.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/base64.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <zspot/zspot.h>

#include "json_scan.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define TOKEN_URL "https://accounts.spotify.com/api/token"

/* The answer to a token request is a few hundred bytes */
#define RESPONSE_MAX 1024

/* Guards the keys and what was obtained with them */
static K_MUTEX_DEFINE(lock);
/* The credentials in use: the stored ones, or else those of the build */
static struct web_keys keys;
static bool configured;

/* "Authorization: Bearer " plus the token; Spotify's are below 400 characters */
static char bearer[600];
static int64_t expires_at_ms;

void webapi_init(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (web_keys_load(&keys) == 0) {
		LOG_INF("Web API: using the stored credentials");
		configured = true;
	} else {
		configured = web_keys_set(&keys, CONFIG_ZSPOT_WEB_CLIENT_ID,
					  CONFIG_ZSPOT_WEB_CLIENT_SECRET,
					  CONFIG_ZSPOT_WEB_REFRESH_TOKEN);
	}
	k_mutex_unlock(&lock);
}

bool webapi_configured(void)
{
	return configured;
}

int webapi_set_keys(const struct web_keys *new_keys)
{
	int ret;

	k_mutex_lock(&lock, K_FOREVER);
	ret = web_keys_store(new_keys);
	if (ret == 0) {
		keys = *new_keys;
		configured = true;
		/* The token in hand belongs to the keys before */
		bearer[0] = '\0';
	}
	k_mutex_unlock(&lock);
	return ret;
}

/*
 * Exchanges the refresh token for an access token; the caller holds the lock.
 * The texts of the request and the answer are too large for the stack next to
 * a TLS handshake: they come from the LVGL pool.
 */
static int renew_token(void)
{
	static const char prefix[] = "Authorization: Basic ";
	/* "<id>:<secret>", its base64 form behind the prefix, the form, the answer */
	const size_t pair_size = sizeof(keys.client_id) + sizeof(keys.client_secret);
	const size_t basic_size = sizeof(prefix) + 4 * pair_size / 3 + 4;
	const size_t body_size = sizeof(keys.refresh_token) + 48;
	char *scratch;
	char *pair;
	char *basic;
	char *body;
	char *response;
	size_t len = 0;
	int status;

	lvgl_lock();
	scratch = lv_malloc(pair_size + basic_size + body_size + RESPONSE_MAX);
	lvgl_unlock();
	if (scratch == NULL) {
		return -ENOMEM;
	}
	pair = scratch;
	basic = pair + pair_size;
	body = basic + basic_size;
	response = body + body_size;

	snprintf(pair, pair_size, "%s:%s", keys.client_id, keys.client_secret);
	/* The keys only hold characters that need no escaping here. */
	snprintf(body, body_size, "grant_type=refresh_token&refresh_token=%s", keys.refresh_token);

	strcpy(basic, prefix);
	if (base64_encode(&basic[strlen(prefix)], basic_size - strlen(prefix), &len, pair,
			  strlen(pair)) != 0) {
		status = -ENOMEM;
	} else {
		const char *const headers[] = {basic, NULL};

		status = zspot_http_request("POST", TOKEN_URL, headers,
					    "application/x-www-form-urlencoded", body,
					    (uint8_t *)response, RESPONSE_MAX - 1, &len);
	}

	if (status == 200) {
		response[len] = '\0';
		strcpy(bearer, "Authorization: Bearer ");
		if (json_string(json_member(response, "access_token"), &bearer[strlen(bearer)],
				sizeof(bearer) - strlen(bearer))) {
			/* Renew a minute early. */
			expires_at_ms = k_uptime_get() +
					(int64_t)json_uint(json_member(response, "expires_in"), 3600) *
						1000 -
					60 * MSEC_PER_SEC;
			status = 0;
		} else {
			bearer[0] = '\0';
			status = -EACCES;
		}
	} else if (status >= 0) {
		response[len] = '\0';
		LOG_WRN("Token request: HTTP %d %s", status, response);
		status = -EACCES;
	}

	lvgl_lock();
	lv_free(scratch);
	lvgl_unlock();

	if (status == 0) {
		LOG_INF("Web API access token renewed");
	}
	return status;
}

int webapi_request(const char *method, const char *url, const char *body, uint8_t *buf,
		   size_t size, size_t *len)
{
	const char *const headers[] = {bearer, NULL};
	int status = -ENOENT;

	if (!webapi_configured()) {
		return -ENOENT;
	}

	/* Held over the request: new keys take the token away with them */
	k_mutex_lock(&lock, K_FOREVER);

	/* A token that Spotify revoked early is renewed once. */
	for (int attempt = 0; attempt < 2; attempt++) {
		if (bearer[0] == '\0' || k_uptime_get() >= expires_at_ms) {
			status = renew_token();
			if (status != 0) {
				break;
			}
		}

		status = zspot_http_request(method, url, headers,
					    body != NULL ? "application/json" : NULL, body, buf,
					    size, len);
		if (status != 401) {
			break;
		}
		bearer[0] = '\0';
	}

	k_mutex_unlock(&lock);
	return status;
}
