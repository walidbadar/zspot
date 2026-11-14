/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "port/base62.h"

/* Encodes the 16 bytes of an id; the byte behind the result must stay as it is. */
static const char *encode_id(const uint8_t *id)
{
	static char out[ZSPOT_BASE62_ID_DIGITS + 2];

	memset(out, '#', sizeof(out));
	zspot_base62_encode(id, 16, out, ZSPOT_BASE62_ID_DIGITS);
	zassert_equal(out[ZSPOT_BASE62_ID_DIGITS], '\0');
	zassert_equal(out[ZSPOT_BASE62_ID_DIGITS + 1], '#');
	return out;
}

ZTEST(zspot_base62, test_smallest_and_largest_id)
{
	uint8_t id[16] = {0};

	zassert_str_equal(encode_id(id), "0000000000000000000000");
	id[15] = 1;
	zassert_str_equal(encode_id(id), "0000000000000000000001");
	memset(id, 0xff, sizeof(id));
	zassert_str_equal(encode_id(id), "7N42dgm5tFLK9N8MT7fHC7");
}

/* The alphabet runs from the digits over the lower-case to the upper-case letters. */
ZTEST(zspot_base62, test_ids)
{
	static const uint8_t count[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
	static const uint8_t mixed[16] = {0x5b, 0x6d, 0x0a, 0x3f, 0x9c, 0x2e, 0x4d, 0x17,
					  0xa8, 0xb0, 0xc4, 0xe1, 0xf2, 0x93, 0x74, 0x65};
	/* What the library reads spotify:track:4uLU6hMCjMI75M1A2tKUQC as */
	static const uint8_t track[16] = {0x93, 0xbc, 0x41, 0x4a, 0x60, 0x67, 0x47, 0xb2,
					  0xb6, 0x12, 0x49, 0x1e, 0xf8, 0x3d, 0x5a, 0x3e};

	zassert_str_equal(encode_id(count), "000syw7rIjXKeGogUSqgWP");
	zassert_str_equal(encode_id(mixed), "2Mw8fYX4YKmcMOc8sumlKZ");
	zassert_str_equal(encode_id(track), "4uLU6hMCjMI75M1A2tKUQC");
}

ZTEST(zspot_base62, test_other_lengths)
{
	static const uint8_t two[] = {0x01, 0x00};
	static const uint8_t wide[] = {0x0e, 0xff};
	uint8_t longer[40] = {0};
	char out[8];

	zspot_base62_encode(two, sizeof(two), out, 4);
	zassert_str_equal(out, "0048");

	/* 3839 is "ZV" in two digits, which is all that is asked for */
	zspot_base62_encode(wide, sizeof(wide), out, 2);
	zassert_str_equal(out, "ZV");

	/* More bytes than an id has: the lower ones count */
	longer[39] = 62;
	zspot_base62_encode(longer, sizeof(longer), out, 3);
	zassert_str_equal(out, "010");

	zspot_base62_encode(two, 0, out, 2);
	zassert_str_equal(out, "00");
}

ZTEST_SUITE(zspot_base62, NULL, NULL, NULL, NULL, NULL);
