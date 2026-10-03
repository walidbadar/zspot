/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lyrics.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <zspot/zspot.h>

#include "json_scan.h"
#include "ui.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define API "https://lrclib.net/api/get"

/* A response carries the lyrics in several forms, about 20 KB in total */
#define RESPONSE_MAX (64 * 1024)
/* Longer lyrics are cut off */
#define TEXT_MAX     (12 * 1024)
#define LINES_MAX    200
#define STACK_SIZE   8192
#define PRIORITY     10

struct request {
	atomic_val_t generation;
	char title[UI_TEXT_MAX];
	char artist[UI_TEXT_MAX];
	uint32_t duration_ms;
};

K_MSGQ_DEFINE(lyrics_requests, sizeof(struct request), 1, 4);

/* Bumped by every request; a result of an older generation is stale. */
static atomic_t lyrics_generation;
static struct k_thread lyrics_thread_data;

static void *pool_alloc(size_t size)
{
	void *ptr;

	lvgl_lock();
	ptr = lv_malloc(size);
	lvgl_unlock();
	return ptr;
}

static void pool_free(void *ptr)
{
	lvgl_lock();
	lv_free(ptr);
	lvgl_unlock();
}

/* Appends "&<name>=<value>", percent-encoding the value. */
static void add_parameter(char *url, size_t size, const char *name, const char *value)
{
	size_t len = strlen(url);

	len += snprintf(&url[len], size - len, "%c%s=", strchr(url, '?') == NULL ? '?' : '&', name);
	for (; *value != '\0' && len + 4 < size; value++) {
		if (isalnum((unsigned char)*value) || strchr("-_.~", *value) != NULL) {
			url[len++] = *value;
		} else {
			len += snprintf(&url[len], size - len, "%%%02X", (unsigned char)*value);
		}
	}
	url[len] = '\0';
}

/*
 * Splits @p text, LRC ("[mm:ss.xx] words") when @p synced or plain lines
 * otherwise, into the lines of @p lyrics. The lines point into @p text.
 */
static void split_lines(struct ui_lyrics *lyrics, char *text, bool synced)
{
	lyrics->synced = synced;
	lyrics->count = 0;

	for (char *line = text; line != NULL && lyrics->count < LINES_MAX;) {
		char *next = strchr(line, '\n');
		uint32_t time_ms = 0;

		if (next != NULL) {
			*next++ = '\0';
		}

		if (synced) {
			char *end;
			uint32_t minutes;

			/* Lines without a time stamp are tags such as "[ar:...]". */
			if (line[0] != '[' || !isdigit((unsigned char)line[1])) {
				line = next;
				continue;
			}
			minutes = strtoul(&line[1], &end, 10);
			time_ms = minutes * 60000 + (uint32_t)(strtod(*end == ':' ? end + 1 : end,
								      &end) * 1000);
			line = *end == ']' ? end + 1 : end;
		}
		while (*line == ' ') {
			line++;
		}

		lyrics->lines[lyrics->count].time_ms = time_ms;
		lyrics->lines[lyrics->count].text = line;
		lyrics->count++;
		line = next;
	}
}

/* Returns the HTTP status or a negative errno; the response is NUL terminated. */
static int fetch(const struct request *req, bool with_duration, char *response)
{
	static const char *const headers[] = {
		"User-Agent: zspot (https://github.com/walidbadar/zspot)", NULL};
	char url[512] = API;
	char seconds[12];
	size_t len = 0;
	int status;

	add_parameter(url, sizeof(url), "track_name", req->title);
	add_parameter(url, sizeof(url), "artist_name", req->artist);
	if (with_duration) {
		snprintf(seconds, sizeof(seconds), "%u", (req->duration_ms + 500) / 1000);
		add_parameter(url, sizeof(url), "duration", seconds);
	}

	status = zspot_http_request("GET", url, headers, NULL, NULL, (uint8_t *)response,
				    RESPONSE_MAX - 1, &len);
	if (status >= 0) {
		response[len] = '\0';
	}
	return status;
}

static void lookup(const struct request *req)
{
	struct ui_lyrics *lyrics;
	char *response;
	char *text;
	bool synced;
	int status;

	response = pool_alloc(RESPONSE_MAX);
	if (response == NULL) {
		ui_set_lyrics_status(req->title, "Out of memory");
		return;
	}

	/* The duration picks the right recording; without a match, take any. */
	status = fetch(req, true, response);
	if (status == 404) {
		status = fetch(req, false, response);
	}
	if (status != 200) {
		if (status != 404) {
			LOG_WRN("Lyrics lookup failed (%d)", status);
		}
		ui_set_lyrics_status(req->title, status == 404 ? "No lyrics for this track"
							       : "Lyrics could not be loaded");
		pool_free(response);
		return;
	}

	/* One block: the line table followed by the text the lines point into. */
	lyrics = pool_alloc(sizeof(*lyrics) + LINES_MAX * sizeof(lyrics->lines[0]) + TEXT_MAX);
	if (lyrics == NULL) {
		ui_set_lyrics_status(req->title, "Out of memory");
		pool_free(response);
		return;
	}
	text = (char *)&lyrics->lines[LINES_MAX];

	synced = json_string(json_member(response, "syncedLyrics"), text, TEXT_MAX);
	if (!synced) {
		json_string(json_member(response, "plainLyrics"), text, TEXT_MAX);
	}
	pool_free(response);

	if (text[0] == '\0') {
		ui_set_lyrics_status(req->title, "No lyrics for this track");
		pool_free(lyrics);
		return;
	}

	strcpy(lyrics->title, req->title);
	split_lines(lyrics, text, synced);

	/* The track may have changed during the lookup. */
	if (req->generation != atomic_get(&lyrics_generation) || !ui_show_lyrics(lyrics)) {
		pool_free(lyrics);
	}
}

static void lyrics_thread(void *p1, void *p2, void *p3)
{
	static struct request req;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		/* lyrics_request() purging the queue ends the wait without a message. */
		if (k_msgq_get(&lyrics_requests, &req, K_FOREVER) == 0) {
			lookup(&req);
		}
	}
}

int lyrics_init(void)
{
	uint8_t *stack = pool_alloc(K_KERNEL_STACK_LEN(STACK_SIZE) + Z_KERNEL_STACK_OBJ_ALIGN);

	if (stack == NULL) {
		LOG_ERR("LVGL memory pool too small for the lyrics");
		return -ENOMEM;
	}

	k_thread_create(&lyrics_thread_data,
			(k_thread_stack_t *)ROUND_UP(stack, Z_KERNEL_STACK_OBJ_ALIGN), STACK_SIZE,
			lyrics_thread, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&lyrics_thread_data, "lyrics");
	return 0;
}

void lyrics_request(const char *title, const char *artist, uint32_t duration_ms)
{
	struct request req = {
		.generation = atomic_inc(&lyrics_generation) + 1,
		.duration_ms = duration_ms,
	};

	snprintf(req.title, sizeof(req.title), "%s", title);
	snprintf(req.artist, sizeof(req.artist), "%s", artist);

	k_msgq_purge(&lyrics_requests);
	k_msgq_put(&lyrics_requests, &req, K_NO_WAIT);
}
