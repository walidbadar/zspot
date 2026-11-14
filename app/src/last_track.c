/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "last_track.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define KEY "zspot/last"

/* Layout of struct last_track; a stored track of another one is not read */
#define FORMAT 1

/*
 * The URIs go into the JSON of a Web API request as they are: only what
 * needs no escaping there.
 */
static bool is_uri(const char *text, size_t size)
{
	const char *end = memchr(text, '\0', size);
	size_t len;

	if (end == NULL || strncmp(text, "spotify:", 8) != 0) {
		return false;
	}
	len = end - text;
	for (size_t i = 0; i < len; i++) {
		if (!isalnum((unsigned char)text[i]) && text[i] != ':') {
			return false;
		}
	}
	return true;
}

static bool is_track_uri(const char *uri, size_t size)
{
	return is_uri(uri, size) && (strncmp(uri, "spotify:track:", 14) == 0 ||
				     strncmp(uri, "spotify:episode:", 16) == 0);
}

/* Copies up to the terminator, without splitting a UTF-8 sequence. */
static void copy_text(char *out, size_t size, const char *text)
{
	size_t len = MIN(strlen(text), size - 1);

	while (len > 0 && ((uint8_t)text[len] & 0xC0) == 0x80) {
		len--;
	}
	memcpy(out, text, len);
	out[len] = '\0';
}

bool last_track_set(struct last_track *track, const char *title, const char *artist,
		    const char *image_url, const char *uri, uint32_t duration_ms)
{
	if (uri == NULL || !is_track_uri(uri, sizeof(track->uri))) {
		return false;
	}

	/* The same track gives the same bytes, which the settings do not write twice */
	memset(track, 0, sizeof(*track));
	track->format = FORMAT;
	copy_text(track->title, sizeof(track->title), title != NULL ? title : "");
	copy_text(track->artist, sizeof(track->artist), artist != NULL ? artist : "");
	strcpy(track->uri, uri);
	track->duration_ms = duration_ms;

	/* A cover whose address is too long is not shown */
	if (image_url != NULL && strlen(image_url) < sizeof(track->image_url)) {
		strcpy(track->image_url, image_url);
	}
	return true;
}

void last_track_set_context(struct last_track *track, const char *context)
{
	memset(track->context, 0, sizeof(track->context));
	if (context != NULL && is_uri(context, sizeof(track->context))) {
		strcpy(track->context, context);
	}
}

/* What was read from the flash is not taken on trust. */
static bool is_valid(struct last_track *track)
{
	track->title[sizeof(track->title) - 1] = '\0';
	track->artist[sizeof(track->artist) - 1] = '\0';
	track->image_url[sizeof(track->image_url) - 1] = '\0';

	return track->format == FORMAT && is_track_uri(track->uri, sizeof(track->uri)) &&
	       (track->context[0] == '\0' || is_uri(track->context, sizeof(track->context))) &&
	       track->position_ms <= track->duration_ms;
}

int last_track_load(struct last_track *track)
{
	ssize_t len;
	int ret;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* The length of the value, also of one that does not fit; 0 without one */
	len = settings_load_one(KEY, track, sizeof(*track));
	if (len == 0) {
		return -ENOENT;
	}
	if (len < 0) {
		LOG_ERR("Cannot read the last track (%d)", (int)len);
		return (int)len;
	}
	if (len != sizeof(*track) || !is_valid(track)) {
		LOG_WRN("The stored last track cannot be used, removing it");
		(void)last_track_forget();
		return -EINVAL;
	}
	return 0;
}

int last_track_store(const struct last_track *track)
{
	int ret;

	if (track->format != FORMAT) {
		return -EINVAL;
	}

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* NVS and ZMS write nothing when the value is the one they hold. */
	ret = settings_save_one(KEY, track, sizeof(*track));
	if (ret != 0) {
		LOG_ERR("Cannot store the last track (%d)", ret);
	}
	return ret;
}

int last_track_forget(void)
{
	int ret = settings_subsys_init();

	if (ret == 0) {
		ret = settings_delete(KEY);
	}
	if (ret != 0) {
		LOG_ERR("Cannot remove the last track (%d)", ret);
	}
	return ret;
}
