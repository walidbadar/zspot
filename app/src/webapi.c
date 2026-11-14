/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "webapi.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_ZSPOT_WEB_SETUP)
#include <zephyr/net/socket.h>
#endif
#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/util.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <zspot/zspot.h>

#include "json_scan.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

/* --- The credentials: checked, parsed, kept in the settings ----------------- */

#define KEYS_SETTING "zspot/webapi"

/* Layout of struct web_keys; stored keys of another one are not read */
#define KEYS_FORMAT 1

/* Whether @p len characters of @p value can be kept in @p size bytes and sent as they are. */
static bool is_usable(const char *value, size_t len, size_t size)
{
	if (len == 0 || len >= size) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		if (!isalnum((unsigned char)value[i]) && value[i] != '-' && value[i] != '_') {
			return false;
		}
	}
	return true;
}

static bool keys_fill(struct web_keys *keys, const char *client_id, size_t id_len,
		const char *client_secret, size_t secret_len, const char *refresh_token,
		size_t token_len)
{
	if (client_id == NULL || client_secret == NULL || refresh_token == NULL ||
	    !is_usable(client_id, id_len, sizeof(keys->client_id)) ||
	    !is_usable(client_secret, secret_len, sizeof(keys->client_secret)) ||
	    !is_usable(refresh_token, token_len, sizeof(keys->refresh_token))) {
		return false;
	}

	/* The same keys give the same bytes, which the settings do not write twice */
	memset(keys, 0, sizeof(*keys));
	keys->format = KEYS_FORMAT;
	memcpy(keys->client_id, client_id, id_len);
	memcpy(keys->client_secret, client_secret, secret_len);
	memcpy(keys->refresh_token, refresh_token, token_len);
	return true;
}

bool web_keys_set(struct web_keys *keys, const char *client_id, const char *client_secret,
		  const char *refresh_token)
{
	return keys_fill(keys, client_id, client_id != NULL ? strlen(client_id) : 0, client_secret,
		   client_secret != NULL ? strlen(client_secret) : 0, refresh_token,
		   refresh_token != NULL ? strlen(refresh_token) : 0);
}

bool web_keys_from_form(struct web_keys *keys, const char *form, size_t len)
{
	static const char *const names[] = {"client_id", "client_secret", "refresh_token"};
	const char *values[ARRAY_SIZE(names)] = {NULL};
	size_t lengths[ARRAY_SIZE(names)] = {0};
	const char *end = form + len;

	for (const char *field = form; field < end;) {
		const char *next = memchr(field, '&', end - field);
		const char *field_end = next != NULL ? next : end;
		const char *equals = memchr(field, '=', field_end - field);

		for (size_t i = 0; equals != NULL && i < ARRAY_SIZE(names); i++) {
			if ((size_t)(equals - field) == strlen(names[i]) &&
			    memcmp(field, names[i], equals - field) == 0) {
				values[i] = equals + 1;
				lengths[i] = field_end - values[i];
			}
		}
		field = field_end + 1;
	}

	return keys_fill(keys, values[0], lengths[0], values[1], lengths[1], values[2], lengths[2]);
}

#if defined(CONFIG_SETTINGS)

/* What was read from the flash is not taken on trust. */
static bool keys_are_valid(const struct web_keys *keys)
{
	const char *const texts[] = {keys->client_id, keys->client_secret, keys->refresh_token};
	const size_t sizes[] = {sizeof(keys->client_id), sizeof(keys->client_secret),
				sizeof(keys->refresh_token)};

	if (keys->format != KEYS_FORMAT) {
		return false;
	}
	for (size_t i = 0; i < ARRAY_SIZE(texts); i++) {
		const char *terminator = memchr(texts[i], '\0', sizes[i]);

		if (terminator == NULL || !is_usable(texts[i], terminator - texts[i], sizes[i])) {
			return false;
		}
	}
	return true;
}

int web_keys_load(struct web_keys *keys)
{
	ssize_t len;
	int ret;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* The length of the value, also of one that does not fit; 0 without one */
	len = settings_load_one(KEYS_SETTING, keys, sizeof(*keys));
	if (len == 0) {
		return -ENOENT;
	}
	if (len < 0) {
		LOG_ERR("Cannot read the Web API credentials (%d)", (int)len);
		return (int)len;
	}
	if (len != sizeof(*keys) || !keys_are_valid(keys)) {
		LOG_WRN("The stored Web API credentials cannot be used, removing them");
		(void)web_keys_forget();
		return -EINVAL;
	}
	return 0;
}

int web_keys_store(const struct web_keys *keys)
{
	int ret;

	if (!keys_are_valid(keys)) {
		return -EINVAL;
	}

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* NVS and ZMS write nothing when the value is the one they hold. */
	ret = settings_save_one(KEYS_SETTING, keys, sizeof(*keys));
	if (ret != 0) {
		LOG_ERR("Cannot store the Web API credentials (%d)", ret);
	}
	return ret;
}

int web_keys_forget(void)
{
	int ret = settings_subsys_init();

	if (ret == 0) {
		ret = settings_delete(KEYS_SETTING);
	}
	if (ret != 0) {
		LOG_ERR("Cannot remove the Web API credentials (%d)", ret);
	}
	return ret;
}

#else /* Nowhere to keep them: only those of the build */

int web_keys_load(struct web_keys *keys)
{
	ARG_UNUSED(keys);
	return -ENOENT;
}

int web_keys_store(const struct web_keys *keys)
{
	ARG_UNUSED(keys);
	return -ENOTSUP;
}

int web_keys_forget(void)
{
	return 0;
}

#endif /* CONFIG_SETTINGS */

/* --- Tokens and requests ----------------------------------------------------- */

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

/* --- Setup over the network -------------------------------------------------- */

#if defined(CONFIG_ZSPOT_WEB_SETUP)

#define SETUP_STACK_SIZE 4096
#define SETUP_PRIORITY   10
/* The request line, a few headers and the form of about 250 bytes */
#define REQUEST_MAX      1024
#define CLIENT_TIMEOUT_S 5
/* How long credentials are taken after the Wi-Fi settings were opened */
#define ALLOW_MS         (5 * 60 * MSEC_PER_SEC)

static struct k_thread server_thread_data;
/* Uptime until which credentials are taken although some are set; 0 for never */
static atomic_t allowed_until_s;

/*
 * The keys are stored from the system work queue: this thread's stack comes
 * from the LVGL pool, which is external RAM on some boards and out of reach
 * while the flash is written.
 */
static struct web_keys pending_keys;
static int store_result;
static K_SEM_DEFINE(store_done, 0, 1);

static void store_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	store_result = webapi_set_keys(&pending_keys);
	/* Not kept in memory for longer than needed */
	memset(&pending_keys, 0, sizeof(pending_keys));
	k_sem_give(&store_done);
}

static K_WORK_DEFINE(store_work, store_handler);

void websetup_allow(void)
{
	atomic_set(&allowed_until_s, (atomic_val_t)((k_uptime_get() + ALLOW_MS) / MSEC_PER_SEC));
}

static bool is_allowed(void)
{
	return !webapi_configured() ||
	       k_uptime_get() / MSEC_PER_SEC < atomic_get(&allowed_until_s);
}

static void respond(int client, int status, const char *reason, const char *text)
{
	char head[120];
	int len = snprintf(head, sizeof(head),
			   "HTTP/1.1 %d %s\r\n"
			   "Content-Type: text/plain\r\n"
			   "Content-Length: %u\r\n"
			   "Connection: close\r\n\r\n",
			   status, reason, (unsigned int)strlen(text) + 1);

	/* The text is of any length: not through the buffer of the head */
	if (zsock_send(client, head, len, 0) == len &&
	    zsock_send(client, text, strlen(text), 0) == (ssize_t)strlen(text)) {
		(void)zsock_send(client, "\n", 1, 0);
	}
}

/* Whether @p text begins with @p prefix, which is in lower case, in any case. */
static bool begins_with(const char *text, const char *prefix)
{
	for (; *prefix != '\0'; text++, prefix++) {
		if (tolower((unsigned char)*text) != *prefix) {
			return false;
		}
	}
	return true;
}

/* Reads the head and the body that it announces; returns the body, or NULL. */
static char *read_request(int client, char *request, size_t size, size_t *body_len)
{
	size_t len = 0;
	size_t content_length = 0;
	char *body = NULL;
	char *header;

	while (len < size - 1) {
		ssize_t n = zsock_recv(client, &request[len], size - 1 - len, 0);

		if (n <= 0) {
			return NULL;
		}
		len += n;
		request[len] = '\0';

		if (body == NULL) {
			body = strstr(request, "\r\n\r\n");
			if (body == NULL) {
				continue;
			}
			body += 4;

			/* Header names come in any case */
			for (header = request; header < body; header++) {
				if (begins_with(header, "\r\ncontent-length:")) {
					content_length = strtoul(header + 17, NULL, 10);
					break;
				}
			}
		}
		if (len - (body - request) >= content_length) {
			*body_len = content_length;
			return body;
		}
	}
	return NULL;
}

static void handle(int client, char *request)
{
	size_t body_len = 0;
	char *body = read_request(client, request, REQUEST_MAX, &body_len);

	if (body == NULL) {
		respond(client, 400, "Bad Request", "Request not understood, or too long");
	} else if (strncmp(request, "GET /webapi ", 12) == 0) {
		respond(client, 200, "OK",
			webapi_configured() ? "Web API credentials are set"
					    : "No Web API credentials yet");
	} else if (strncmp(request, "POST /webapi ", 13) != 0) {
		respond(client, 404, "Not Found", "Only /webapi is here");
	} else if (!is_allowed()) {
		respond(client, 403, "Forbidden",
			"Credentials are set already. To replace them, open the Wi-Fi settings "
			"on the device (hold the Wi-Fi symbol) and send them again");
	} else if (!web_keys_from_form(&pending_keys, body, body_len)) {
		respond(client, 400, "Bad Request",
			"Expected client_id, client_secret and refresh_token");
	} else {
		k_sem_reset(&store_done);
		k_work_submit(&store_work);
		k_sem_take(&store_done, K_FOREVER);

		if (store_result == 0) {
			LOG_INF("Web API credentials received and stored");
			/* One set per opening of the settings */
			atomic_clear(&allowed_until_s);
			respond(client, 200, "OK", "Stored");
		} else {
			respond(client, 500, "Internal Server Error", "Could not store them");
		}
	}

	/* Do not leave the secrets in the pool */
	memset(request, 0, REQUEST_MAX);
}

static void server_thread(void *p1, void *p2, void *p3)
{
	char *request = p1;
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(CONFIG_ZSPOT_WEB_SETUP_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int server = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int one = 1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (server >= 0) {
		(void)zsock_setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	}
	if (server < 0 ||
	    zsock_bind(server, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
	    zsock_listen(server, 1) != 0) {
		LOG_ERR("Web API setup: cannot listen on port %d (errno %d)",
			CONFIG_ZSPOT_WEB_SETUP_PORT, errno);
		return;
	}
	LOG_INF("Web API setup on port %d", CONFIG_ZSPOT_WEB_SETUP_PORT);

	while (true) {
		struct timeval timeout = {.tv_sec = CLIENT_TIMEOUT_S};
		int client = zsock_accept(server, NULL, NULL);

		if (client < 0) {
			k_msleep(100);
			continue;
		}
		(void)zsock_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		(void)zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		handle(client, request);
		zsock_close(client);
	}
}

int websetup_init(void)
{
	uint8_t *stack;
	char *request;

	/* Like the rest of the UI, live in the LVGL pool. */
	lvgl_lock();
	stack = lv_malloc(K_KERNEL_STACK_LEN(SETUP_STACK_SIZE) + Z_KERNEL_STACK_OBJ_ALIGN);
	request = lv_malloc(REQUEST_MAX);
	lvgl_unlock();
	if (stack == NULL || request == NULL) {
		LOG_ERR("LVGL memory pool too small for the Web API setup");
		return -ENOMEM;
	}

	k_thread_create(&server_thread_data,
			(k_thread_stack_t *)ROUND_UP(stack, Z_KERNEL_STACK_OBJ_ALIGN), SETUP_STACK_SIZE,
			server_thread, request, NULL, NULL, SETUP_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&server_thread_data, "websetup");
	return 0;
}

#endif /* CONFIG_ZSPOT_WEB_SETUP */
