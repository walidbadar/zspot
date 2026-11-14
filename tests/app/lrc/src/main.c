/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "lrc.h"

#define LINES 8

static struct {
	struct ui_lyrics lyrics;
	struct ui_lyrics_line lines[LINES];
} parsed;

static char text[256];

static struct ui_lyrics *split(const char *lrc, bool synced)
{
	memset(&parsed, 0xa5, sizeof(parsed));
	strcpy(text, lrc);
	lrc_split(&parsed.lyrics, text, synced, LINES);
	return &parsed.lyrics;
}

ZTEST(zspot_lrc, test_plain)
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

ZTEST(zspot_lrc, test_synced)
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
ZTEST(zspot_lrc, test_synced_skips_tags)
{
	struct ui_lyrics *l = split("[ar:Artist]\n[ti:Title]\n\nno time here\n[00:01.00] Sung\n[00:09.00]\n",
				    true);

	zassert_equal(l->count, 2);
	zassert_equal(l->lines[0].time_ms, 1000);
	zassert_str_equal(l->lines[0].text, "Sung");
	zassert_equal(l->lines[1].time_ms, 9000);
	zassert_str_equal(l->lines[1].text, "");
}

ZTEST(zspot_lrc, test_more_lines_than_room)
{
	struct ui_lyrics *l = split("1\n2\n3\n4\n5\n6\n7\n8\n9\n10", false);

	zassert_equal(l->count, LINES);
	zassert_str_equal(l->lines[LINES - 1].text, "8");
	/* What did not fit was left as it is */
	zassert_str_equal(text + 16, "9\n10");
}

ZTEST(zspot_lrc, test_empty_and_odd)
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

ZTEST_SUITE(zspot_lrc, NULL, NULL, NULL, NULL, NULL);
