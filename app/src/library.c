/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "library.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <zspot/zspot.h>

#include "json_scan.h"
#include "ui.h"
#include "webapi.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define API "https://api.spotify.com/v1"

/* Liked Songs come with complete album objects, about 4 KB per track */
#define RESPONSE_MAX  (160 * 1024)
#define BODY_MAX      2048
#define STACK_SIZE    8192
#define PRIORITY      10
#define TOP_HEADING   "Your Library"

enum request_type {
	REQUEST_OPEN,
	REQUEST_BACK,
	REQUEST_SELECT,
};

struct request {
	enum request_type type;
	int index;
};

struct collection {
	char name[64];
	char subtitle[64];
	/* Playlist id; empty for Liked Songs, which is not a playable context */
	char id[40];
};

/* What the listings on screen refer to; owned by the library thread. */
struct model {
	struct collection collections[UI_LIST_MAX];
	int collection_count;
	bool collections_loaded;

	/* Track listing of collections[current] while one is open */
	int current;
	char track_uris[UI_LIST_MAX][48];
	int track_count;
	bool tracks_shown;
};

K_MSGQ_DEFINE(library_requests, sizeof(struct request), 4, 4);

static struct k_thread library_thread_data;
static struct model *model;
static uint32_t generation;

/*
 * Runs a Web API request. The NUL terminated response is returned in a buffer
 * from the LVGL pool that the caller releases with response_free(); NULL when
 * the request failed, with a text for the user in @p error and the HTTP status
 * (or negative errno) in @p status when that is not NULL.
 */
static char *request(const char *method, const char *url, const char *body, char *error,
		     size_t error_size, int *status_out)
{
	size_t len = 0;
	char *response;
	int status;

	lvgl_lock();
	response = lv_malloc(RESPONSE_MAX);
	lvgl_unlock();
	if (response == NULL) {
		snprintf(error, error_size, "Out of memory");
		return NULL;
	}

	status = webapi_request(method, url, body, (uint8_t *)response, RESPONSE_MAX - 1, &len);
	if (status_out != NULL) {
		*status_out = status;
	}
	if (status >= 200 && status < 300) {
		response[len] = '\0';
		return response;
	}

	if (status == -ENOENT) {
		snprintf(error, error_size, "Library access is not set up (see the README)");
	} else if (status == -EACCES) {
		snprintf(error, error_size, "Spotify rejected the application credentials");
	} else if (status < 0) {
		snprintf(error, error_size, "Spotify could not be reached (%d)", status);
	} else {
		response[MIN(len, 200)] = '\0';
		LOG_WRN("%s %s: HTTP %d %s", method, url, status, response);
		snprintf(error, error_size, "Spotify refused the request (HTTP %d)", status);
	}

	lvgl_lock();
	lv_free(response);
	lvgl_unlock();
	return NULL;
}

static void response_free(char *response)
{
	lvgl_lock();
	lv_free(response);
	lvgl_unlock();
}

static void show_collections(void)
{
	model->tracks_shown = false;

	ui_list_reset(UI_LIST_LIBRARY, ++generation, TOP_HEADING, true, NULL);
	for (int i = 0; i < model->collection_count; i++) {
		ui_list_add(UI_LIST_LIBRARY, generation, model->collections[i].name,
			    model->collections[i].subtitle, 0);
	}
}

static void load_collections(void)
{
	struct collection *collection = &model->collections[0];
	char error[72];
	char owner[40];
	char url[80];
	char *response;

	ui_list_reset(UI_LIST_LIBRARY, ++generation, TOP_HEADING, true, "Loading...");

	/* One row is taken by Liked Songs. */
	snprintf(url, sizeof(url), API "/me/playlists?limit=%d", UI_LIST_MAX - 1);
	response = request("GET", url, NULL, error, sizeof(error), NULL);
	if (response == NULL) {
		ui_list_reset(UI_LIST_LIBRARY, ++generation, TOP_HEADING, true, error);
		return;
	}

	*collection = (struct collection){.name = "Liked Songs", .subtitle = "Playlist"};
	model->collection_count = 1;

	for (const char *item = json_first(json_member(response, "items"));
	     item != NULL && model->collection_count < UI_LIST_MAX; item = json_next(item)) {
		collection = &model->collections[model->collection_count];

		if (!json_string(json_member(item, "id"), collection->id, sizeof(collection->id)) ||
		    !json_string(json_member(item, "name"), collection->name,
				 sizeof(collection->name))) {
			continue;
		}

		if (json_string(json_member(json_member(item, "owner"), "display_name"), owner,
				sizeof(owner))) {
			snprintf(collection->subtitle, sizeof(collection->subtitle),
				 "Playlist \xE2\x80\xA2 %s", owner);
		} else {
			strcpy(collection->subtitle, "Playlist");
		}
		model->collection_count++;
	}
	response_free(response);

	model->collections_loaded = true;
	show_collections();
}

static void open_collection(int index)
{
	const struct collection *collection = &model->collections[index];
	bool liked = collection->id[0] == '\0';
	/* Spotify renamed a playlist's "tracks" to "items"; try the old name too. */
	static const char *const names[][2] = {{"items", "item"}, {"tracks", "track"}};
	char url[200];
	char error[72];
	char title[96];
	char artist[96];
	char *response = NULL;
	int status;

	model->current = index;
	model->track_count = 0;
	model->tracks_shown = true;
	ui_list_reset(UI_LIST_LIBRARY, ++generation, collection->name, false, "Loading...");

	if (liked) {
		snprintf(url, sizeof(url), API "/me/tracks?market=from_token&limit=%d",
			 UI_LIST_MAX);
		response = request("GET", url, NULL, error, sizeof(error), NULL);
	}
	for (size_t i = 0; !liked && response == NULL && i < ARRAY_SIZE(names); i++) {
		snprintf(url, sizeof(url),
			 API "/playlists/%s/%s?market=from_token&limit=%d"
			 "&fields=items(%s(name,uri,duration_ms,artists(name)))",
			 collection->id, names[i][0], UI_LIST_MAX, names[i][1]);
		response = request("GET", url, NULL, error, sizeof(error), &status);
		/* Only an unknown endpoint is worth the other spelling. */
		if (status != 403 && status != 404 && status != 410) {
			break;
		}
	}
	if (response == NULL) {
		ui_list_reset(UI_LIST_LIBRARY, ++generation, collection->name, false, error);
		return;
	}

	ui_list_reset(UI_LIST_LIBRARY, ++generation, collection->name, false, NULL);
	for (const char *item = json_first(json_member(response, "items"));
	     item != NULL && model->track_count < UI_LIST_MAX; item = json_next(item)) {
		const char *track = json_member(item, "track");

		if (track == NULL) {
			track = json_member(item, "item");
		}
		/* Entries that are no longer available come as null. */
		if (!json_string(json_member(track, "uri"), model->track_uris[model->track_count],
				 sizeof(model->track_uris[0])) ||
		    !json_string(json_member(track, "name"), title, sizeof(title))) {
			continue;
		}
		json_string(json_member(json_first(json_member(track, "artists")), "name"), artist,
			    sizeof(artist));

		ui_list_add(UI_LIST_LIBRARY, generation, title, artist,
			    json_uint(json_member(track, "duration_ms"), 0));
		model->track_count++;
	}
	response_free(response);

	if (model->track_count == 0) {
		ui_list_reset(UI_LIST_LIBRARY, ++generation, collection->name, false, "Nothing to play here");
	}
}

/* Starts playback of a listed track on this device, within its collection. */
static void play_track(int index)
{
	const struct collection *collection = &model->collections[model->current];
	char url[120];
	char error[72];
	char *response;
	char *body;
	int len;

	lvgl_lock();
	body = lv_malloc(BODY_MAX);
	lvgl_unlock();
	if (body == NULL) {
		return;
	}

	if (collection->id[0] != '\0') {
		snprintf(body, BODY_MAX,
			 "{\"context_uri\":\"spotify:playlist:%s\",\"offset\":{\"uri\":\"%s\"}}",
			 collection->id, model->track_uris[index]);
	} else {
		/* Liked Songs: hand over the listed tracks themselves. */
		len = snprintf(body, BODY_MAX, "{\"uris\":[");
		for (int i = 0; i < model->track_count; i++) {
			len += snprintf(&body[len], BODY_MAX - len, "%s\"%s\"", i > 0 ? "," : "",
					model->track_uris[i]);
		}
		snprintf(&body[len], BODY_MAX - len, "],\"offset\":{\"position\":%d}}", index);
	}

	snprintf(url, sizeof(url), API "/me/player/play?device_id=%s", zspot_device_id());
	response = request("PUT", url, body, error, sizeof(error), NULL);
	if (response != NULL) {
		response_free(response);
	} else {
		LOG_WRN("Cannot start playback: %s", error);
	}

	lvgl_lock();
	lv_free(body);
	lvgl_unlock();
}

static void library_thread(void *p1, void *p2, void *p3)
{
	struct request req;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_msgq_get(&library_requests, &req, K_FOREVER);

		switch (req.type) {
		case REQUEST_OPEN:
			load_collections();
			break;
		case REQUEST_BACK:
			if (model->collections_loaded) {
				show_collections();
			} else {
				load_collections();
			}
			break;
		case REQUEST_SELECT:
			if (model->tracks_shown) {
				if (req.index >= 0 && req.index < model->track_count) {
					play_track(req.index);
				}
			} else if (req.index >= 0 && req.index < model->collection_count) {
				open_collection(req.index);
			}
			break;
		}
	}
}

int library_init(void)
{
	uint8_t *stack;

	/* Like the rest of the UI, live in the LVGL pool. */
	lvgl_lock();
	stack = lv_malloc(K_KERNEL_STACK_LEN(STACK_SIZE) + Z_KERNEL_STACK_OBJ_ALIGN);
	model = lv_zalloc(sizeof(*model));
	lvgl_unlock();
	if (stack == NULL || model == NULL) {
		LOG_ERR("LVGL memory pool too small for the library");
		return -ENOMEM;
	}

	k_thread_create(&library_thread_data,
			(k_thread_stack_t *)ROUND_UP(stack, Z_KERNEL_STACK_OBJ_ALIGN), STACK_SIZE,
			library_thread, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&library_thread_data, "library");
	return 0;
}

static void submit(enum request_type type, int index)
{
	const struct request req = {.type = type, .index = index};

	if (model != NULL && k_msgq_put(&library_requests, &req, K_NO_WAIT) != 0) {
		LOG_WRN("Library busy, request dropped");
	}
}

void library_open(void)
{
	submit(REQUEST_OPEN, 0);
}

void library_back(void)
{
	submit(REQUEST_BACK, 0);
}

void library_select(int index)
{
	submit(REQUEST_SELECT, index);
}
