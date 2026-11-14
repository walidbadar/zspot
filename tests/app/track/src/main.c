/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

/*
 * The store of the last song is inside track.c: build that file into the
 * test (see CMakeLists.txt) and call it there. What it needs of the player
 * is below.
 */
#include "track.c"

#define KEY      "zspot/last"
#define TRACK    "spotify:track:5sWHDYs0csV6RS48xBl0tH"
#define EPISODE  "spotify:episode:5sWHDYs0csV6RS48xBl0tH"
#define PLAYLIST "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M"
#define COVER    "https://i.scdn.co/image/ab67616d0000b273e8b066f70c206551210d902b"

static struct track track;

/* A stored value that is longer than a track */
static struct {
	struct track track;
	uint8_t more[8];
} larger;

static const struct zspot_track_info info = {
	.name = "Title",
	.album = "Album",
	.artist = "Artist",
	.image_url = COVER,
	.uri = TRACK,
	.duration_ms = 215000,
};

/* What the stand-ins were called with */
static bool web_configured;
static char shown_title[UI_TEXT_MAX];
static char shown_headline[UI_TEXT_MAX];
static int shown_paused;
static uint32_t shown_position_ms;
static int play_result;
static int play_count;
static char play_uri[TRACK_URI_MAX];
static char play_context[TRACK_CONTEXT_MAX];
static uint32_t play_position_ms;
static library_play_cb_t play_done;

bool webapi_configured(void)
{
	return web_configured;
}

void ui_show_track(const char *title, const char *artist, const char *image_url,
		   uint32_t duration_ms)
{
	strcpy(shown_title, title);
}

void ui_show_message(const char *headline, const char *detail)
{
	strcpy(shown_headline, headline);
}

void ui_set_paused(bool paused)
{
	shown_paused = paused;
}

int library_play(const char *track_uri, const char *context_uri, uint32_t position_ms,
		 library_play_cb_t done)
{
	play_count++;
	strcpy(play_uri, track_uri);
	strcpy(play_context, context_uri);
	play_position_ms = position_ms;
	play_done = done;
	return play_result;
}

static void set_position(uint32_t position_ms)
{
	shown_position_ms = position_ms;
}

/* Lets the system work queue store what is due. */
static void wait_for_store(void)
{
	struct k_work_sync sync;

	(void)k_work_flush(&save_work, &sync);
}

/* The state of track.c as after a restart */
static void restart(void)
{
	struct k_work_sync sync;

	wait_for_store();
	(void)k_work_cancel_delayable_sync(&start_timeout_work, &sync);
	memset(&current, 0, sizeof(current));
	current_valid = false;
	dirty = false;
	queue_context[0] = '\0';
	atomic_clear(&waiting);
	begun = false;
	show_position = NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	restart();
	zassert_ok(track_forget());
	memset(&track, 0xa5, sizeof(track));

	web_configured = true;
	shown_title[0] = '\0';
	shown_headline[0] = '\0';
	shown_paused = -1;
	shown_position_ms = UINT32_MAX;
	play_result = 0;
	play_count = 0;
	play_done = NULL;
}

/* --- The stored song ------------------------------------------------------- */

ZTEST(zspot_track, test_set)
{
	zassert_true(track_fill(&track, "Title", "Artist", COVER, TRACK, 215000));
	zassert_str_equal(track.title, "Title");
	zassert_str_equal(track.artist, "Artist");
	zassert_str_equal(track.image_url, COVER);
	zassert_str_equal(track.uri, TRACK);
	zassert_str_equal(track.context, "");
	zassert_equal(track.duration_ms, 215000);
	zassert_equal(track.position_ms, 0);

	zassert_true(track_fill(&track, NULL, NULL, NULL, EPISODE, 0));
	zassert_str_equal(track.title, "");
	zassert_str_equal(track.image_url, "");
	zassert_str_equal(track.uri, EPISODE);
}

/* What is not the URI of a track would end up in a request as it is. */
ZTEST(zspot_track, test_set_rejects_other_uris)
{
	zassert_false(track_fill(&track, "T", "A", COVER, NULL, 1));
	zassert_false(track_fill(&track, "T", "A", COVER, "", 1));
	zassert_false(track_fill(&track, "T", "A", COVER, PLAYLIST, 1));
	zassert_false(track_fill(&track, "T", "A", COVER, "spotify:track:a\"b", 1));
	zassert_false(track_fill(&track, "T", "A", COVER, "spotify:track:a b", 1));
	zassert_false(track_fill(&track, "T", "A", COVER,
				 "spotify:track:5sWHDYs0csV6RS48xBl0tH5sWHDYs0csV6RS48xBl0tH", 1));
}

ZTEST(zspot_track, test_long_texts_are_cut)
{
	char title[2 * UI_TEXT_MAX];
	char url[2 * TRACK_IMAGE_MAX];

	/* Two-byte characters: an odd limit must not split the last one */
	for (size_t i = 0; i + 1 < sizeof(title); i += 2) {
		title[i] = '\xC3';
		title[i + 1] = '\xA4';
	}
	title[sizeof(title) - 2] = '\0';
	memset(url, 'u', sizeof(url) - 1);
	url[sizeof(url) - 1] = '\0';

	zassert_true(track_fill(&track, title, title, url, TRACK, 1));
	zassert_equal(strlen(track.title), UI_TEXT_MAX - 2);
	zassert_equal((uint8_t)track.title[strlen(track.title) - 1], 0xA4);
	zassert_equal(strlen(track.artist), UI_TEXT_MAX - 2);
	/* Half an address is none */
	zassert_str_equal(track.image_url, "");
}

ZTEST(zspot_track, test_context)
{
	char longer[2 * TRACK_CONTEXT_MAX];

	zassert_true(track_fill(&track, "T", "A", COVER, TRACK, 1));

	track_fill_context(&track, PLAYLIST);
	zassert_str_equal(track.context, PLAYLIST);
	track_fill_context(&track, NULL);
	zassert_str_equal(track.context, "");

	track_fill_context(&track, PLAYLIST);
	track_fill_context(&track, "spotify:search:a\"b");
	zassert_str_equal(track.context, "");

	memset(longer, 'a', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = '\0';
	memcpy(longer, "spotify:", 8);
	track_fill_context(&track, longer);
	zassert_str_equal(track.context, "");
}

ZTEST(zspot_track, test_nothing_stored)
{
	zassert_equal(track_load(&track), -ENOENT);
	/* Nothing left to forget is not an error */
	zassert_ok(track_forget());
}

ZTEST(zspot_track, test_store_and_load)
{
	struct track loaded;

	zassert_true(track_fill(&track, "Title", "Artist", COVER, TRACK, 215000));
	track_fill_context(&track, PLAYLIST);
	track.position_ms = 61000;
	zassert_ok(track_store(&track));
	/* The same again is accepted as well */
	zassert_ok(track_store(&track));

	memset(&loaded, 0x5a, sizeof(loaded));
	zassert_ok(track_load(&loaded));
	zassert_mem_equal(&loaded, &track, sizeof(track));

	zassert_ok(track_forget());
	zassert_equal(track_load(&loaded), -ENOENT);
}

/* Filling in a track again leaves nothing of the one before in its bytes. */
ZTEST(zspot_track, test_set_is_reproducible)
{
	struct track other;

	memset(&other, 0x5a, sizeof(other));
	zassert_true(track_fill(&track, "A long title", "A long artist", COVER, EPISODE, 9));
	track_fill_context(&track, PLAYLIST);
	zassert_true(track_fill(&track, "T", "A", "", TRACK, 1));
	zassert_true(track_fill(&other, "T", "A", "", TRACK, 1));
	zassert_mem_equal(&track, &other, sizeof(track));
}

ZTEST(zspot_track, test_store_rejects_unset)
{
	zassert_equal(track_store(&track), -EINVAL);
	zassert_equal(track_load(&track), -ENOENT);
}

static void store_raw(const struct track *raw, size_t size)
{
	struct track loaded;

	zassert_ok(settings_save_one(KEY, raw, size));
	zassert_equal(track_load(&loaded), -EINVAL);
	/* It was removed */
	zassert_equal(track_load(&loaded), -ENOENT);
}

ZTEST(zspot_track, test_unusable_are_removed)
{
	struct track raw;

	zassert_true(track_fill(&track, "Title", "Artist", COVER, TRACK, 215000));

	/* Another layout */
	raw = track;
	raw.format++;
	store_raw(&raw, sizeof(raw));

	/* Another size */
	store_raw(&track, sizeof(track) - 4);
	memset(&larger, 0, sizeof(larger));
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
ZTEST(zspot_track, test_unterminated_texts)
{
	struct track raw;
	struct track loaded;

	zassert_true(track_fill(&track, "Title", "Artist", COVER, TRACK, 215000));
	raw = track;
	memset(raw.title, 't', sizeof(raw.title));
	memset(raw.image_url, 'u', sizeof(raw.image_url));
	zassert_ok(settings_save_one(KEY, &raw, sizeof(raw)));

	zassert_ok(track_load(&loaded));
	zassert_equal(strlen(loaded.title), sizeof(loaded.title) - 1);
	zassert_equal(strlen(loaded.image_url), sizeof(loaded.image_url) - 1);
	zassert_str_equal(loaded.uri, TRACK);
}

/* --- What plays, and the song after a restart ------------------------------- */

/* A track that was just started is stored at once, with what it is played from. */
ZTEST(zspot_track, test_started_track_is_stored)
{
	track_set_context(PLAYLIST);
	track_set(&info, 0, true);
	wait_for_store();

	zassert_ok(track_load(&track));
	zassert_str_equal(track.title, "Title");
	zassert_str_equal(track.uri, TRACK);
	zassert_str_equal(track.context, PLAYLIST);
	zassert_equal(track.position_ms, 0);
}

/* The flash is left alone while audio goes out: the track waits for a pause. */
ZTEST(zspot_track, test_track_waits_for_a_pause)
{
	track_set(&info, 0, false);
	wait_for_store();
	zassert_equal(track_load(&track), -ENOENT);

	track_set_position(61000);
	wait_for_store();
	zassert_ok(track_load(&track));
	zassert_str_equal(track.uri, TRACK);
	zassert_str_equal(track.context, "");
	zassert_equal(track.position_ms, 61000);

	/* Never behind the end */
	track_set_position(info.duration_ms + 5);
	wait_for_store();
	zassert_ok(track_load(&track));
	zassert_equal(track.position_ms, info.duration_ms);
}

/* What cannot be started again is not kept. */
ZTEST(zspot_track, test_other_than_tracks_are_not_stored)
{
	struct zspot_track_info other = info;

	other.uri = "spotify:local:file";
	track_set(&other, 0, true);
	track_set_position(1000);
	wait_for_store();
	zassert_equal(track_load(&track), -ENOENT);
}

ZTEST(zspot_track, test_show_and_start)
{
	track_set_context(PLAYLIST);
	track_set(&info, 0, true);
	track_set_position(61000);
	restart();

	zassert_false(track_waiting());
	zassert_true(track_show(set_position));
	zassert_true(track_waiting());
	zassert_str_equal(shown_title, "Title");
	zassert_equal(shown_paused, 1);
	zassert_equal(shown_position_ms, 61000);

	/* The slider moves the start, also across the next restart */
	zassert_true(track_seek(5000));
	zassert_equal(shown_position_ms, 5000);
	wait_for_store();
	zassert_ok(track_load(&track));
	zassert_equal(track.position_ms, 5000);

	/* Nothing to pause yet */
	zassert_true(track_pause(true));
	zassert_equal(play_count, 0);

	zassert_true(track_pause(false));
	zassert_equal(play_count, 1);
	zassert_str_equal(play_uri, TRACK);
	zassert_str_equal(play_context, PLAYLIST);
	zassert_equal(play_position_ms, 5000);
	zassert_equal(shown_paused, 0);

	/* Spotify took the request: the song waits until the player starts it */
	play_done(true);
	zassert_true(track_waiting());
	track_playback_begun();
	zassert_false(track_waiting());

	/* From here on the taps are for the player */
	zassert_false(track_pause(false));
	zassert_false(track_seek(1000));
	zassert_equal(play_count, 1);
}

ZTEST(zspot_track, test_nothing_to_show)
{
	zassert_false(track_show(set_position));
	zassert_false(track_waiting());
	zassert_str_equal(shown_title, "");
}

/* Without the Web API the song could not be started. */
ZTEST(zspot_track, test_not_shown_without_web_api)
{
	track_set(&info, 0, true);
	restart();

	web_configured = false;
	zassert_false(track_show(set_position));
	zassert_false(track_waiting());
}

/* The app was faster than the sign-in: what it started stays on the screen. */
ZTEST(zspot_track, test_not_shown_once_playback_began)
{
	track_set(&info, 0, true);
	restart();

	track_playback_begun();
	zassert_false(track_show(set_position));
	zassert_str_equal(shown_title, "");
}

ZTEST(zspot_track, test_start_refused)
{
	track_set(&info, 0, true);
	restart();
	zassert_true(track_show(set_position));

	/* Spotify said no */
	zassert_true(track_pause(false));
	play_done(false);
	zassert_false(track_waiting());
	zassert_str_equal(shown_headline, "Cannot play the last song");
}

ZTEST(zspot_track, test_start_not_possible)
{
	track_set(&info, 0, true);
	restart();
	zassert_true(track_show(set_position));

	/* A request is on its way already: the song keeps waiting */
	play_result = -EBUSY;
	zassert_true(track_pause(false));
	zassert_true(track_waiting());
	zassert_str_equal(shown_headline, "");

	play_result = -ENOMEM;
	zassert_true(track_pause(false));
	zassert_false(track_waiting());
	zassert_str_equal(shown_headline, "Cannot play the last song");
}

ZTEST_SUITE(zspot_track, NULL, NULL, before, NULL, NULL);
