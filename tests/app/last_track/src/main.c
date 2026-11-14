/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "last_track.h"

/* The store logs under the name of the player application */
LOG_MODULE_REGISTER(zspot_player, LOG_LEVEL_INF);

#define KEY      "zspot/last"
#define TRACK    "spotify:track:5sWHDYs0csV6RS48xBl0tH"
#define EPISODE  "spotify:episode:5sWHDYs0csV6RS48xBl0tH"
#define PLAYLIST "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M"
#define COVER    "https://i.scdn.co/image/ab67616d0000b273e8b066f70c206551210d902b"

static struct last_track track;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(last_track_forget());
	memset(&track, 0xa5, sizeof(track));
}

ZTEST(zspot_last_track, test_set)
{
	zassert_true(last_track_set(&track, "Title", "Artist", COVER, TRACK, 215000));
	zassert_str_equal(track.title, "Title");
	zassert_str_equal(track.artist, "Artist");
	zassert_str_equal(track.image_url, COVER);
	zassert_str_equal(track.uri, TRACK);
	zassert_str_equal(track.context, "");
	zassert_equal(track.duration_ms, 215000);
	zassert_equal(track.position_ms, 0);

	zassert_true(last_track_set(&track, NULL, NULL, NULL, EPISODE, 0));
	zassert_str_equal(track.title, "");
	zassert_str_equal(track.image_url, "");
	zassert_str_equal(track.uri, EPISODE);
}

/* What is not the URI of a track would end up in a request as it is. */
ZTEST(zspot_last_track, test_set_rejects_other_uris)
{
	zassert_false(last_track_set(&track, "T", "A", COVER, NULL, 1));
	zassert_false(last_track_set(&track, "T", "A", COVER, "", 1));
	zassert_false(last_track_set(&track, "T", "A", COVER, PLAYLIST, 1));
	zassert_false(last_track_set(&track, "T", "A", COVER, "spotify:track:a\"b", 1));
	zassert_false(last_track_set(&track, "T", "A", COVER, "spotify:track:a b", 1));
	zassert_false(last_track_set(&track, "T", "A", COVER,
				     "spotify:track:5sWHDYs0csV6RS48xBl0tH5sWHDYs0csV6RS48xBl0tH",
				     1));
}

ZTEST(zspot_last_track, test_long_texts_are_cut)
{
	char title[2 * UI_TEXT_MAX];
	char url[2 * LAST_TRACK_IMAGE_MAX];

	/* Two-byte characters: an odd limit must not split the last one */
	for (size_t i = 0; i + 1 < sizeof(title); i += 2) {
		title[i] = '\xC3';
		title[i + 1] = '\xA4';
	}
	title[sizeof(title) - 2] = '\0';
	memset(url, 'u', sizeof(url) - 1);
	url[sizeof(url) - 1] = '\0';

	zassert_true(last_track_set(&track, title, title, url, TRACK, 1));
	zassert_equal(strlen(track.title), UI_TEXT_MAX - 2);
	zassert_equal((uint8_t)track.title[strlen(track.title) - 1], 0xA4);
	zassert_equal(strlen(track.artist), UI_TEXT_MAX - 2);
	/* Half an address is none */
	zassert_str_equal(track.image_url, "");
}

ZTEST(zspot_last_track, test_context)
{
	char longer[2 * LAST_TRACK_CONTEXT_MAX];

	zassert_true(last_track_set(&track, "T", "A", COVER, TRACK, 1));

	last_track_set_context(&track, PLAYLIST);
	zassert_str_equal(track.context, PLAYLIST);
	last_track_set_context(&track, NULL);
	zassert_str_equal(track.context, "");

	last_track_set_context(&track, PLAYLIST);
	last_track_set_context(&track, "spotify:search:a\"b");
	zassert_str_equal(track.context, "");

	memset(longer, 'a', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = '\0';
	memcpy(longer, "spotify:", 8);
	last_track_set_context(&track, longer);
	zassert_str_equal(track.context, "");
}

ZTEST(zspot_last_track, test_nothing_stored)
{
	zassert_equal(last_track_load(&track), -ENOENT);
	/* Nothing left to forget is not an error */
	zassert_ok(last_track_forget());
}

ZTEST(zspot_last_track, test_store_and_load)
{
	struct last_track loaded;

	zassert_true(last_track_set(&track, "Title", "Artist", COVER, TRACK, 215000));
	last_track_set_context(&track, PLAYLIST);
	track.position_ms = 61000;
	zassert_ok(last_track_store(&track));
	/* The same again is accepted as well */
	zassert_ok(last_track_store(&track));

	memset(&loaded, 0x5a, sizeof(loaded));
	zassert_ok(last_track_load(&loaded));
	zassert_mem_equal(&loaded, &track, sizeof(track));

	zassert_ok(last_track_forget());
	zassert_equal(last_track_load(&loaded), -ENOENT);
}

/* Filling in a track again leaves nothing of the one before in its bytes. */
ZTEST(zspot_last_track, test_set_is_reproducible)
{
	struct last_track other;

	memset(&other, 0x5a, sizeof(other));
	zassert_true(last_track_set(&track, "A long title", "A long artist", COVER, EPISODE, 9));
	last_track_set_context(&track, PLAYLIST);
	zassert_true(last_track_set(&track, "T", "A", "", TRACK, 1));
	zassert_true(last_track_set(&other, "T", "A", "", TRACK, 1));
	zassert_mem_equal(&track, &other, sizeof(track));
}

ZTEST(zspot_last_track, test_store_rejects_unset)
{
	zassert_equal(last_track_store(&track), -EINVAL);
	zassert_equal(last_track_load(&track), -ENOENT);
}

static void store_raw(const struct last_track *raw, size_t size)
{
	struct last_track loaded;

	zassert_ok(settings_save_one(KEY, raw, size));
	zassert_equal(last_track_load(&loaded), -EINVAL);
	/* It was removed */
	zassert_equal(last_track_load(&loaded), -ENOENT);
}

ZTEST(zspot_last_track, test_unusable_are_removed)
{
	static struct {
		struct last_track track;
		uint8_t more[8];
	} larger;
	struct last_track raw;

	zassert_true(last_track_set(&track, "Title", "Artist", COVER, TRACK, 215000));

	/* Another layout */
	raw = track;
	raw.format++;
	store_raw(&raw, sizeof(raw));

	/* Another size */
	store_raw(&track, sizeof(track) - 4);
	larger.track = track;
	store_raw(&larger.track, sizeof(larger));

	/* Not a track */
	raw = track;
	strcpy(raw.uri, PLAYLIST);
	store_raw(&raw, sizeof(raw));
	raw = track;
	memset(raw.uri, 'x', sizeof(raw.uri));
	store_raw(&raw, sizeof(raw));

	/* A context that would break the request */
	raw = track;
	strcpy(raw.context, "spotify:\"x");
	store_raw(&raw, sizeof(raw));

	/* Paused behind the end */
	raw = track;
	raw.position_ms = raw.duration_ms + 1;
	store_raw(&raw, sizeof(raw));
}

/* Texts without an end in the flash get one. */
ZTEST(zspot_last_track, test_unterminated_texts)
{
	struct last_track raw;
	struct last_track loaded;

	zassert_true(last_track_set(&track, "Title", "Artist", COVER, TRACK, 215000));
	raw = track;
	memset(raw.title, 't', sizeof(raw.title));
	memset(raw.image_url, 'u', sizeof(raw.image_url));
	zassert_ok(settings_save_one(KEY, &raw, sizeof(raw)));

	zassert_ok(last_track_load(&loaded));
	zassert_equal(strlen(loaded.title), sizeof(loaded.title) - 1);
	zassert_equal(strlen(loaded.image_url), sizeof(loaded.image_url) - 1);
	zassert_str_equal(loaded.uri, TRACK);
}

ZTEST_SUITE(zspot_last_track, NULL, NULL, before, NULL, NULL);
