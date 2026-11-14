/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "websetup.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "web_keys.h"
#include "webapi.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define STACK_SIZE       4096
#define PRIORITY         10
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
	stack = lv_malloc(K_KERNEL_STACK_LEN(STACK_SIZE) + Z_KERNEL_STACK_OBJ_ALIGN);
	request = lv_malloc(REQUEST_MAX);
	lvgl_unlock();
	if (stack == NULL || request == NULL) {
		LOG_ERR("LVGL memory pool too small for the Web API setup");
		return -ENOMEM;
	}

	k_thread_create(&server_thread_data,
			(k_thread_stack_t *)ROUND_UP(stack, Z_KERNEL_STACK_OBJ_ALIGN), STACK_SIZE,
			server_thread, request, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&server_thread_data, "websetup");
	return 0;
}
