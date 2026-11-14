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
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/util.h>

#include <zspot/zspot.h>

#include "json_scan.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define TOKEN_URL "https://accounts.spotify.com/api/token"

#define CLIENT_ID     CONFIG_ZSPOT_WEB_CLIENT_ID
#define CLIENT_SECRET CONFIG_ZSPOT_WEB_CLIENT_SECRET
#define REFRESH_TOKEN CONFIG_ZSPOT_WEB_REFRESH_TOKEN

#define BASIC_PREFIX  "Authorization: Basic "
#define BEARER_PREFIX "Authorization: Bearer "

/* The thread of the requests: a TLS handshake runs on its stack */
#define WORK_STACK_SIZE 8192
#define WORK_PRIORITY   10

#define PLAY_URL          "https://api.spotify.com/v1/me/player/play?device_id=%s"
#define PLAY_URL_MAX      120
/* A context, a track and a position */
#define PLAY_BODY_MAX     256
/* Nothing on success, a few lines when Spotify refuses */
#define PLAY_RESPONSE_MAX 512

static void play_handler(struct k_work *work);

/* One request at a time: they share the token */
static K_MUTEX_DEFINE(lock);
/* "Authorization: Bearer " plus the token; Spotify's are below 400 characters */
static char bearer[600];
static int64_t expires_at_ms;

static struct k_work_q work_queue;
static bool work_queue_started;

/* The track of webapi_play(), from the request until it was sent */
static struct {
	char uri[48];
	char context[64];
	uint32_t position_ms;
	webapi_play_cb_t done;
} play;
static atomic_t play_busy;
static K_WORK_DEFINE(play_work, play_handler);

int webapi_init(void)
{
	const struct k_work_queue_config config = {.name = "webapi"};
	uint8_t *stack;

	if (work_queue_started) {
		return 0;
	}

	/* From the memory of the library, which is external RAM where the board has some */
	stack = zspot_mem_alloc(K_KERNEL_STACK_LEN(WORK_STACK_SIZE) + Z_KERNEL_STACK_OBJ_ALIGN);
	if (stack == NULL) {
		LOG_ERR("No memory for the thread of the Web API");
		return -ENOMEM;
	}

	k_work_queue_init(&work_queue);
	k_work_queue_start(&work_queue,
			   (k_thread_stack_t *)ROUND_UP(stack, Z_KERNEL_STACK_OBJ_ALIGN),
			   WORK_STACK_SIZE, WORK_PRIORITY, &config);
	work_queue_started = true;
	return 0;
}

int webapi_submit(struct k_work *work)
{
	if (!work_queue_started) {
		return -ENODEV;
	}
	return k_work_submit_to_queue(&work_queue, work);
}

/* Exchanges the refresh token for an access token; the caller holds the lock. */
static int renew_token(void)
{
	char credentials[sizeof(CLIENT_ID) + sizeof(CLIENT_SECRET)];
	char basic[sizeof(BASIC_PREFIX) + 4 * sizeof(credentials) / 3 + 4];
	char body[sizeof(REFRESH_TOKEN) + 48];
	char response[1024];
	const char *const headers[] = {basic, NULL};
	size_t len = 0;
	int status;

	snprintf(credentials, sizeof(credentials), "%s:%s", CLIENT_ID, CLIENT_SECRET);
	strcpy(basic, BASIC_PREFIX);
	if (base64_encode(&basic[strlen(BASIC_PREFIX)], sizeof(basic) - strlen(BASIC_PREFIX), &len,
			  credentials, strlen(credentials)) != 0) {
		return -ENOMEM;
	}

	/* Spotify's refresh tokens are URL safe, so no escaping is needed. */
	snprintf(body, sizeof(body), "grant_type=refresh_token&refresh_token=%s", REFRESH_TOKEN);

	status = zspot_http_request("POST", TOKEN_URL, headers, "application/x-www-form-urlencoded",
				    body, (uint8_t *)response, sizeof(response) - 1, &len);
	if (status < 0) {
		return status;
	}
	response[len] = '\0';
	if (status != 200) {
		LOG_WRN("Token request: HTTP %d %s", status, response);
		return -EACCES;
	}

	strcpy(bearer, BEARER_PREFIX);
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

bool webapi_configured(void)
{
	return strlen(CLIENT_ID) > 0 && strlen(CLIENT_SECRET) > 0 && strlen(REFRESH_TOKEN) > 0;
}

int webapi_request(const char *method, const char *url, const char *body, uint8_t *buf, size_t size,
		   size_t *len)
{
	const char *const headers[] = {bearer, NULL};
	int status = -ENOENT;

	if (!webapi_configured()) {
		return -ENOENT;
	}

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

int webapi_start_playback(const char *body)
{
	char url[PLAY_URL_MAX];
	char *response = zspot_mem_alloc(PLAY_RESPONSE_MAX);
	size_t len = 0;
	int status;

	if (response == NULL) {
		return -ENOMEM;
	}

	snprintf(url, sizeof(url), PLAY_URL, zspot_device_id());
	status = webapi_request("PUT", url, body, (uint8_t *)response, PLAY_RESPONSE_MAX - 1, &len);
	if (status >= 200 && status < 300) {
		status = 0;
	} else if (status >= 0) {
		response[len] = '\0';
		LOG_WRN("Cannot start playback: HTTP %d %s", status, response);
		status = -EIO;
	} else {
		LOG_WRN("Cannot start playback (%d)", status);
	}

	zspot_mem_free(response);
	return status;
}

/* Starts the track of webapi_play(), within its playlist or album if it has one. */
static void play_handler(struct k_work *work)
{
	webapi_play_cb_t done = play.done;
	char *body = zspot_mem_alloc(PLAY_BODY_MAX);
	bool accepted = false;

	ARG_UNUSED(work);

	/* The Web API takes an offset into these contexts only */
	if (body != NULL && (strncmp(play.context, "spotify:playlist:", 17) == 0 ||
			     strncmp(play.context, "spotify:album:", 14) == 0)) {
		snprintf(body, PLAY_BODY_MAX,
			 "{\"context_uri\":\"%s\",\"offset\":{\"uri\":\"%s\"},\"position_ms\":%u}",
			 play.context, play.uri, play.position_ms);
		accepted = webapi_start_playback(body) == 0;
	}
	/* Also when the track is no longer part of the playlist */
	if (body != NULL && !accepted) {
		snprintf(body, PLAY_BODY_MAX, "{\"uris\":[\"%s\"],\"position_ms\":%u}", play.uri,
			 play.position_ms);
		accepted = webapi_start_playback(body) == 0;
	}
	zspot_mem_free(body);

	/* Free for the next request before the caller hears of this one */
	atomic_clear(&play_busy);
	done(accepted);
}

int webapi_play(const char *track_uri, const char *context_uri, uint32_t position_ms,
		webapi_play_cb_t done)
{
	if (!work_queue_started) {
		return -ENODEV;
	}
	if (strlen(track_uri) >= sizeof(play.uri) || strlen(context_uri) >= sizeof(play.context)) {
		return -EINVAL;
	}
	/* One at a time */
	if (!atomic_cas(&play_busy, 0, 1)) {
		return -EBUSY;
	}

	strcpy(play.uri, track_uri);
	strcpy(play.context, context_uri);
	play.position_ms = position_ms;
	play.done = done;

	(void)k_work_submit_to_queue(&work_queue, &play_work);
	return 0;
}
