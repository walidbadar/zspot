/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "webapi.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_ZSPOT_CREDENTIALS_STORED)
#include <zephyr/settings/settings.h>
#include <psa/internal_trusted_storage.h>
#endif
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/util.h>

#include <zspot/zspot.h>

#include "json_scan.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

/* The credentials that the build carries, where it does */
#if defined(CONFIG_ZSPOT_WEB_CREDENTIALS_BUILT_IN)
#define BUILD_CLIENT_ID     CONFIG_ZSPOT_WEB_CLIENT_ID
#define BUILD_CLIENT_SECRET CONFIG_ZSPOT_WEB_CLIENT_SECRET
#define BUILD_REFRESH_TOKEN CONFIG_ZSPOT_WEB_REFRESH_TOKEN
#else
#define BUILD_CLIENT_ID     ""
#define BUILD_CLIENT_SECRET ""
#define BUILD_REFRESH_TOKEN ""
#endif

/* The entry of the credentials in the secure storage ("zw") */
#define KEYS_UID           0x7a77
/* Where an earlier version of the player kept them in the clear */
#define KEYS_PLAIN_SETTING "zspot/webapi"
/* Layout of struct keys; stored ones of another layout are not read */
#define KEYS_FORMAT        1
/* Spotify's are 32 characters each */
#define KEYS_ID_MAX        48
#define KEYS_SECRET_MAX    48
/* About 130 characters at present */
#define KEYS_TOKEN_MAX     320

#define TOKEN_URL          "https://accounts.spotify.com/api/token"
#define BASIC_PREFIX       "Authorization: Basic "
#define BEARER_PREFIX      "Authorization: Bearer "
/* The answer to a token request is a few hundred bytes */
#define TOKEN_RESPONSE_MAX 1024

/* The thread of the requests: a TLS handshake runs on its stack */
#define WORK_STACK_SIZE 8192
#define WORK_PRIORITY   10

#define PLAY_URL          "https://api.spotify.com/v1/me/player/play?device_id=%s"
#define PLAY_URL_MAX      120
/* A context, a track and a position */
#define PLAY_BODY_MAX     256
/* Nothing on success, a few lines when Spotify refuses */
#define PLAY_RESPONSE_MAX 512

/* Stored as it is: keys_fill() leaves no stray bytes behind the texts. */
struct keys {
	/* Tells the layout; set by keys_fill() */
	uint32_t format;
	char client_id[KEYS_ID_MAX];
	char client_secret[KEYS_SECRET_MAX];
	char refresh_token[KEYS_TOKEN_MAX];
};

static void play_handler(struct k_work *work);

/* The credentials in use: those of the build, or else the stored ones */
static struct keys keys;
static bool configured;

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

/* Whether @p value can be kept in @p size bytes and sent as it is, without escaping. */
static bool is_usable(const char *value, size_t size)
{
	const char *end = memchr(value, '\0', size);

	if (end == NULL || end == value) {
		return false;
	}
	for (const char *c = value; c < end; c++) {
		if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_') {
			return false;
		}
	}
	return true;
}

/* What was read from the flash is not taken on trust. */
static bool keys_are_valid(const struct keys *k)
{
	return k->format == KEYS_FORMAT && is_usable(k->client_id, sizeof(k->client_id)) &&
	       is_usable(k->client_secret, sizeof(k->client_secret)) &&
	       is_usable(k->refresh_token, sizeof(k->refresh_token));
}

/* Fills in @p k; false when a value is missing, too long or would need escaping. */
static bool keys_fill(struct keys *k, const char *client_id, const char *client_secret,
		      const char *refresh_token)
{
	if (strlen(client_id) >= sizeof(k->client_id) ||
	    strlen(client_secret) >= sizeof(k->client_secret) ||
	    strlen(refresh_token) >= sizeof(k->refresh_token)) {
		return false;
	}

	/* The same credentials give the same bytes, which are not written twice */
	memset(k, 0, sizeof(*k));
	k->format = KEYS_FORMAT;
	strcpy(k->client_id, client_id);
	strcpy(k->client_secret, client_secret);
	strcpy(k->refresh_token, refresh_token);
	return keys_are_valid(k);
}

#if defined(CONFIG_ZSPOT_CREDENTIALS_STORED)

BUILD_ASSERT(CONFIG_SECURE_STORAGE_ITS_MAX_DATA_SIZE >= sizeof(struct keys),
	     "The secure storage must take the Web API credentials");

static void keys_forget_plain(void)
{
	if (settings_subsys_init() == 0) {
		(void)settings_delete(KEYS_PLAIN_SETTING);
	}
}

/* Stores the credentials in use, encrypted: for the builds that come without them. */
static int keys_store(void)
{
	psa_status_t status = psa_its_set(KEYS_UID, sizeof(keys), &keys, PSA_STORAGE_FLAG_NONE);

	if (status != PSA_SUCCESS) {
		LOG_ERR("Cannot store the Web API credentials (%d)", (int)status);
		return -EIO;
	}
	/* Nothing of them is left in the clear */
	keys_forget_plain();
	return 0;
}

/* Takes up the stored credentials: 0, -ENOENT without any, or another negative errno. */
static int keys_load(void)
{
	size_t len = 0;
	psa_status_t status = psa_its_get(KEYS_UID, 0, sizeof(keys), &keys, &len);

	if (status == PSA_SUCCESS && len == sizeof(keys) && keys_are_valid(&keys)) {
		return 0;
	}
	if (status != PSA_ERROR_DOES_NOT_EXIST) {
		/* Also an entry that was altered, or that another device encrypted */
		LOG_WRN("The stored Web API credentials cannot be used, removing them");
		(void)psa_its_remove(KEYS_UID);
		return -EINVAL;
	}

	/* Those that an earlier version stored in the clear are taken over once */
	if (settings_subsys_init() == 0 &&
	    settings_load_one(KEYS_PLAIN_SETTING, &keys, sizeof(keys)) == (ssize_t)sizeof(keys) &&
	    keys_are_valid(&keys)) {
		return keys_store();
	}
	keys_forget_plain();
	return -ENOENT;
}

#else /* Nowhere to keep them: only those of the build */

static int keys_store(void)
{
	return -ENOTSUP;
}

static int keys_load(void)
{
	return -ENOENT;
}

#endif /* CONFIG_ZSPOT_CREDENTIALS_STORED */

/*
 * Takes up the credentials of the build, which are stored for the builds that
 * come without them, or else the stored ones.
 */
static void keys_init(const char *client_id, const char *client_secret, const char *refresh_token)
{
	if (keys_fill(&keys, client_id, client_secret, refresh_token)) {
		configured = true;
		if (keys_store() == 0) {
			LOG_WRN("The Web API credentials of this firmware are stored: "
				"flash one that is built without them");
		}
	} else if (keys_load() == 0) {
		configured = true;
		LOG_INF("Web API: using the stored credentials");
	} else {
		configured = false;
		memset(&keys, 0, sizeof(keys));
	}
}

int webapi_init(void)
{
	const struct k_work_queue_config config = {.name = "webapi"};
	uint8_t *stack;

	keys_init(BUILD_CLIENT_ID, BUILD_CLIENT_SECRET, BUILD_REFRESH_TOKEN);

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

/*
 * Exchanges the refresh token for an access token; the caller holds the lock.
 * The texts of the request and the answer take their room from the memory of
 * the library, not from the stack that a TLS handshake runs on.
 */
static int renew_token(void)
{
	/* "<id>:<secret>", its base64 form behind the prefix, the form, the answer */
	const size_t pair_size = sizeof(keys.client_id) + sizeof(keys.client_secret);
	const size_t basic_size = sizeof(BASIC_PREFIX) + 4 * pair_size / 3 + 4;
	const size_t body_size = sizeof(keys.refresh_token) + 48;
	const size_t scratch_size = pair_size + basic_size + body_size + TOKEN_RESPONSE_MAX;
	const char *headers[] = {NULL, NULL};
	char *pair;
	char *basic;
	char *body;
	char *response;
	size_t len = 0;
	int status;

	pair = zspot_mem_alloc(scratch_size);
	if (pair == NULL) {
		return -ENOMEM;
	}
	basic = pair + pair_size;
	body = basic + basic_size;
	response = body + body_size;
	headers[0] = basic;

	snprintf(pair, pair_size, "%s:%s", keys.client_id, keys.client_secret);
	/* The credentials only hold characters that need no escaping here. */
	snprintf(body, body_size, "grant_type=refresh_token&refresh_token=%s", keys.refresh_token);

	strcpy(basic, BASIC_PREFIX);
	if (base64_encode(&basic[strlen(BASIC_PREFIX)], basic_size - strlen(BASIC_PREFIX), &len,
			  pair, strlen(pair)) != 0) {
		status = -ENOMEM;
	} else {
		status = zspot_http_request("POST", TOKEN_URL, headers,
					    "application/x-www-form-urlencoded", body,
					    (uint8_t *)response, TOKEN_RESPONSE_MAX - 1, &len);
	}

	if (status == 200) {
		response[len] = '\0';
		strcpy(bearer, BEARER_PREFIX);
		if (json_string(json_member(response, "access_token"), &bearer[strlen(bearer)],
				sizeof(bearer) - strlen(bearer))) {
			/* Renew a minute early. */
			expires_at_ms =
				k_uptime_get() +
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

	/* The secret and the token do not stay behind in the heap */
	memset(pair, 0, scratch_size);
	zspot_mem_free(pair);

	if (status == 0) {
		LOG_INF("Web API access token renewed");
	}
	return status;
}

bool webapi_configured(void)
{
	return configured;
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
