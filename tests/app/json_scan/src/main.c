/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "json_scan.h"

/* Shaped like a Web API response: nested objects, arrays, things to skip */
static const char doc[] =
	"{\"href\":\"https://api.example/v1/me/playlists?offset=0&limit=2\","
	" \"limit\" : 2, \"next\":null, \"flags\":[true,false,null],"
	" \"items\":[ {\"id\":\"p1\",\"name\":\"Mix, with ] and } inside\","
	"              \"owner\":{\"display_name\":\"Me\"},\"tracks\":{\"total\":12}},"
	"             {\"id\":\"p2\",\"name\":\"Second \\\"quoted\\\"\",\"owner\":{},"
	"              \"images\":[[1,2],[3]]} ],"
	" \"total\":2}";

static void expect_string(const char *value, const char *text)
{
	char out[64];

	zassert_true(json_string(value, out, sizeof(out)));
	zassert_str_equal(out, text);
}

ZTEST(zspot_json_scan, test_members)
{
	expect_string(json_member(doc, "href"), "https://api.example/v1/me/playlists?offset=0&limit=2");
	zassert_equal(json_uint(json_member(doc, "limit"), 99), 2);
	zassert_equal(json_uint(json_member(doc, "total"), 99), 2);

	/* Absent, and not to be mistaken for the beginning of another name */
	zassert_is_null(json_member(doc, "missing"));
	zassert_is_null(json_member(doc, "lim"));
	zassert_is_null(json_member(doc, "limits"));
	/* Members of inner objects are not those of the outer one */
	zassert_is_null(json_member(doc, "display_name"));
}

ZTEST(zspot_json_scan, test_absent_values_chain)
{
	char out[8] = "x";

	zassert_is_null(json_member(NULL, "a"));
	zassert_is_null(json_member("[1]", "a"));
	zassert_is_null(json_member("\"a\"", "a"));
	zassert_is_null(json_first(NULL));
	zassert_is_null(json_first("{}"));
	zassert_is_null(json_next(NULL));
	zassert_is_null(json_member(json_member(json_member(doc, "nothing"), "here"), "at all"));

	zassert_false(json_string(NULL, out, sizeof(out)));
	zassert_str_equal(out, "");
	zassert_equal(json_uint(NULL, 7), 7);
}

ZTEST(zspot_json_scan, test_arrays)
{
	const char *item = json_first(json_member(doc, "items"));
	const char *flag = json_first(json_member(doc, "flags"));
	int flags = 0;

	expect_string(json_member(item, "id"), "p1");
	/* Brackets, braces and commas in a string do not end anything */
	expect_string(json_member(item, "name"), "Mix, with ] and } inside");
	expect_string(json_member(json_member(item, "owner"), "display_name"), "Me");
	zassert_equal(json_uint(json_member(json_member(item, "tracks"), "total"), 0), 12);

	item = json_next(item);
	expect_string(json_member(item, "id"), "p2");
	expect_string(json_member(item, "name"), "Second \"quoted\"");
	zassert_is_null(json_member(json_member(item, "owner"), "display_name"));
	/* An array of arrays is walked one array at a time */
	zassert_equal(json_uint(json_first(json_next(json_first(json_member(item, "images")))), 0),
		      3);
	zassert_is_null(json_next(item));

	for (; flag != NULL; flag = json_next(flag)) {
		flags++;
	}
	zassert_equal(flags, 3);

	zassert_is_null(json_first("[]"));
	zassert_is_null(json_first("[ ]"));
	zassert_is_null(json_next(json_first("[1]")));
}

ZTEST(zspot_json_scan, test_string_escapes)
{
	expect_string("\"a\\nb\\tc\\rd\"", "a\nb\tc\rd");
	expect_string("\"q\\\"s\\\\l\\/\"", "q\"s\\l/");
	/* Controls that a label cannot show become spaces */
	expect_string("\"x\\by\\fz\"", "x y z");
	expect_string("\"\"", "");
	/* U+00E9, U+20AC and, as a surrogate pair, U+1F600 */
	expect_string("\"caf\\u00e9\"", "caf\xC3\xA9");
	expect_string("\"\\u20AC5\"", "\xE2\x82\xAC" "5");
	expect_string("\"\\ud83d\\ude00!\"", "\xF0\x9F\x98\x80!");
	/* UTF-8 that is there already passes as it is */
	expect_string("\"\xC3\xA4\xE2\x82\xAC\"", "\xC3\xA4\xE2\x82\xAC");
}

ZTEST(zspot_json_scan, test_string_is_cut_between_characters)
{
	char out[6];

	/* "ab" and two characters of two bytes each: the second does not fit */
	zassert_true(json_string("\"ab\xC3\xA4\xC3\xB6\"", out, sizeof(out)));
	zassert_str_equal(out, "ab\xC3\xA4");
	zassert_true(json_string("\"ab\\u00e4\\u00f6\"", out, sizeof(out)));
	zassert_str_equal(out, "ab\xC3\xA4");

	zassert_true(json_string("\"abcdefgh\"", out, sizeof(out)));
	zassert_str_equal(out, "abcde");
	zassert_true(json_string("\"abc\"", out, 1));
	zassert_str_equal(out, "");
}

ZTEST(zspot_json_scan, test_not_a_string)
{
	char out[8] = "x";

	zassert_false(json_string("12", out, sizeof(out)));
	zassert_str_equal(out, "");
	zassert_false(json_string("null", out, sizeof(out)));
	zassert_false(json_string("{\"a\":1}", out, sizeof(out)));
}

ZTEST(zspot_json_scan, test_numbers)
{
	zassert_equal(json_uint("0", 9), 0);
	zassert_equal(json_uint("215000,", 9), 215000);
	zassert_equal(json_uint("4294967295}", 9), UINT32_MAX);
	zassert_equal(json_uint("-1", 9), 9);
	zassert_equal(json_uint("\"12\"", 9), 9);
	zassert_equal(json_uint("null", 9), 9);
	zassert_equal(json_uint("true", 9), 9);
}

/* A response that was cut off, or is not JSON, gives nothing and is not read beyond its end. */
ZTEST(zspot_json_scan, test_malformed)
{
	char out[16];

	zassert_is_null(json_member("{\"a\":\"x", "b"));
	zassert_is_null(json_member("{\"a\":[1,2", "b"));
	zassert_is_null(json_member("{\"a\":{\"b\":1", "c"));
	zassert_is_null(json_member("{\"a\" 1}", "a"));
	zassert_is_null(json_member("{\"a\":1 \"b\":2}", "b"));
	zassert_is_null(json_member("{", "a"));
	zassert_is_null(json_member("{\"a", "a"));
	zassert_is_null(json_next("[1,[2,3"));
	zassert_is_null(json_next("\"abc"));

	/* What there is of a string that has no end */
	zassert_true(json_string("\"abc", out, sizeof(out)));
	zassert_str_equal(out, "abc");
	zassert_true(json_string("\"ab\\", out, sizeof(out)));
	zassert_str_equal(out, "ab");
	zassert_true(json_string("\"ab\\u12", out, sizeof(out)));
	zassert_str_equal(out, "ab");
	/* A found member whose value is cut off is still found */
	expect_string(json_member("{\"a\":\"x", "a"), "x");
}

ZTEST_SUITE(zspot_json_scan, NULL, NULL, NULL, NULL, NULL);
