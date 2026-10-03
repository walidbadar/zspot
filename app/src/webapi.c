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

#include <zspot/zspot.h>

#include "json_scan.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define TOKEN_URL "https://accounts.spotify.com/api/token"

#define CLIENT_ID     CONFIG_ZSPOT_WEB_CLIENT_ID
#define CLIENT_SECRET CONFIG_ZSPOT_WEB_CLIENT_SECRET
#define REFRESH_TOKEN CONFIG_ZSPOT_WEB_REFRESH_TOKEN

/* "Authorization: Bearer " plus the token; Spotify's are below 400 characters */
static char bearer[600];
static int64_t expires_at_ms;

/* Exchanges the refresh token for an access token. */
static int renew_token(void)
{
	static const char prefix[] = "Authorization: Basic ";
	char credentials[sizeof(CLIENT_ID) + sizeof(CLIENT_SECRET)];
	char basic[sizeof(prefix) + 4 * sizeof(credentials) / 3 + 4];
	char body[sizeof(REFRESH_TOKEN) + 48];
	char response[1024];
	const char *const headers[] = {basic, NULL};
	size_t len = 0;
	int status;

	snprintf(credentials, sizeof(credentials), "%s:%s", CLIENT_ID, CLIENT_SECRET);
	strcpy(basic, prefix);
	if (base64_encode(&basic[strlen(prefix)], sizeof(basic) - strlen(prefix), &len,
			  credentials, strlen(credentials)) != 0) {
		return -ENOMEM;
	}

	/* Spotify's refresh tokens are URL safe, so no escaping is needed. */
	snprintf(body, sizeof(body), "grant_type=refresh_token&refresh_token=%s", REFRESH_TOKEN);

	status = zspot_http_request("POST", TOKEN_URL, headers,
				    "application/x-www-form-urlencoded", body, (uint8_t *)response,
				    sizeof(response) - 1, &len);
	if (status < 0) {
		return status;
	}
	response[len] = '\0';
	if (status != 200) {
		LOG_WRN("Token request: HTTP %d %s", status, response);
		return -EACCES;
	}

	strcpy(bearer, "Authorization: Bearer ");
	if (!json_string(json_member(response, "access_token"), &bearer[strlen(bearer)],
			 sizeof(bearer) - strlen(bearer))) {
		bearer[0] = '\0';
		return -EACCES;
	}

	/* Renew a minute early. */
	expires_at_ms = k_uptime_get() +
			(int64_t)json_uint(json_member(response, "expires_in"), 3600) * 1000 -
			60 * MSEC_PER_SEC;
	LOG_INF("Web API access token renewed");
	return 0;
}

int webapi_request(const char *method, const char *url, const char *body, uint8_t *buf,
		   size_t size, size_t *len)
{
	const char *const headers[] = {bearer, NULL};
	int status;

	if (strlen(CLIENT_ID) == 0 || strlen(CLIENT_SECRET) == 0 || strlen(REFRESH_TOKEN) == 0) {
		return -ENOENT;
	}

	/* A token that Spotify revoked early is renewed once. */
	for (int attempt = 0; attempt < 2; attempt++) {
		if (bearer[0] == '\0' || k_uptime_get() >= expires_at_ms) {
			status = renew_token();
			if (status != 0) {
				return status;
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
	return status;
}
