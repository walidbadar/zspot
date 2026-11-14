/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

/*
 * The lines are split inside lyrics.c: build that file into the test (see
 * CMakeLists.txt) and call it there. What it needs of the player is below.
 */
#include "lyrics.c"

/* Nothing is fetched or shown in these tests */
int zspot_http_request(const char *method, const char *url, const char *const *headers,
		       const char *content_type, const char *body, uint8_t *buf, size_t size,
		       size_t *len)
{
	return -ENOTCONN;
}

bool ui_show_lyrics(struct ui_lyrics *new_lyrics)
{
	return false;
}

void ui_set_lyrics_status(const char *title, const char *status)
{
}

static struct {
	struct ui_lyrics lyrics;
	struct ui_lyrics_line lines[LINES_MAX];
} parsed;

static char text[2048];

static struct ui_lyrics *split(const char *lrc, bool synced)
{
	memset(&parsed, 0xa5, sizeof(parsed));
	strcpy(text, lrc);
	split_lines(&parsed.lyrics, text, synced);
	return &parsed.lyrics;
}

ZTEST(zspot_lyrics, test_plain)
{
	struct ui_lyrics *l = split("First line\nSecond line\n\nAfter a gap", false);

	zassert_false(l->synced);
	zassert_equal(l->count, 4);
	zassert_str_equal(l->lines[0].text, "First line");
	zassert_str_equal(l->lines[1].text, "Second line");
	zassert_str_equal(l->lines[2].text, "");
	zassert_str_equal(l->lines[3].text, "After a gap");
	zassert_equal(l->lines[0].time_ms, 0);
	zassert_equal(l->lines[3].time_ms, 0);
}

ZTEST(zspot_lyrics, test_synced)
{
	struct ui_lyrics *l = split("[00:12.34] First\n[01:02.50]Second\n[10:00.00]  Third\n[00:05] Whole",
				    true);

	zassert_true(l->synced);
	zassert_equal(l->count, 4);
	zassert_equal(l->lines[0].time_ms, 12340);
	zassert_str_equal(l->lines[0].text, "First");
	zassert_equal(l->lines[1].time_ms, 62500);
	zassert_str_equal(l->lines[1].text, "Second");
	zassert_equal(l->lines[2].time_ms, 600000);
	zassert_str_equal(l->lines[2].text, "Third");
	zassert_equal(l->lines[3].time_ms, 5000);
	zassert_str_equal(l->lines[3].text, "Whole");
}

/* Tags and lines without a time are not lyrics; an instrumental stretch is an empty line. */
ZTEST(zspot_lyrics, test_synced_skips_tags)
{
	struct ui_lyrics *l = split("[ar:Artist]\n[ti:Title]\n\nno time here\n[00:01.00] Sung\n[00:09.00]\n",
				    true);

	zassert_equal(l->count, 2);
	zassert_equal(l->lines[0].time_ms, 1000);
	zassert_str_equal(l->lines[0].text, "Sung");
	zassert_equal(l->lines[1].time_ms, 9000);
	zassert_str_equal(l->lines[1].text, "");
}

ZTEST(zspot_lyrics, test_more_lines_than_room)
{
	static char many[2048];
	struct ui_lyrics *l;
	size_t len = 0;

	/* Lines "0" to "209", each with its line break */
	for (int i = 0; i < LINES_MAX + 10; i++) {
		len += snprintf(&many[len], sizeof(many) - len, "%d\n", i);
	}

	l = split(many, false);
	zassert_equal(l->count, LINES_MAX);
	zassert_str_equal(l->lines[0].text, "0");
	zassert_str_equal(l->lines[LINES_MAX - 1].text, "199");
	/* What did not fit was left as it is */
	zassert_true(strncmp(l->lines[LINES_MAX - 1].text + 4, "200\n201\n", 8) == 0);
}

ZTEST(zspot_lyrics, test_empty_and_odd)
{
	struct ui_lyrics *l = split("", false);

	zassert_equal(l->count, 1);
	zassert_str_equal(l->lines[0].text, "");

	zassert_equal(split("", true)->count, 0);

	/* A time stamp that is not finished still gives a line, not a crash */
	l = split("[01\n[02:\n[03:04", true);
	zassert_equal(l->count, 3);
	zassert_equal(l->lines[0].time_ms, 60000);
	zassert_equal(l->lines[1].time_ms, 120000);
	zassert_equal(l->lines[2].time_ms, 184000);
}

ZTEST_SUITE(zspot_lyrics, NULL, NULL, NULL, NULL, NULL);
