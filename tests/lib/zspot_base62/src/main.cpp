/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <cstdint>
#include <string>
#include <vector>

#include <zephyr/ztest.h>

#include "core/Utils.h"

/* The 22 digits of the 16 bytes of an id */
static std::string encode_id(const std::vector<uint8_t> &id)
{
	return base62Encode(id, 22);
}

ZTEST(zspot_base62, test_smallest_and_largest_id)
{
	std::vector<uint8_t> id(16, 0);

	zassert_str_equal(encode_id(id).c_str(), "0000000000000000000000");
	id[15] = 1;
	zassert_str_equal(encode_id(id).c_str(), "0000000000000000000001");
	id.assign(16, 0xff);
	zassert_str_equal(encode_id(id).c_str(), "7N42dgm5tFLK9N8MT7fHC7");
}

/* The alphabet runs from the digits over the lower-case to the upper-case letters. */
ZTEST(zspot_base62, test_ids)
{
	const std::vector<uint8_t> count = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
	const std::vector<uint8_t> mixed = {0x5b, 0x6d, 0x0a, 0x3f, 0x9c, 0x2e, 0x4d, 0x17,
					    0xa8, 0xb0, 0xc4, 0xe1, 0xf2, 0x93, 0x74, 0x65};
	/* What the library reads spotify:track:4uLU6hMCjMI75M1A2tKUQC as */
	const std::vector<uint8_t> track = {0x93, 0xbc, 0x41, 0x4a, 0x60, 0x67, 0x47, 0xb2,
					    0xb6, 0x12, 0x49, 0x1e, 0xf8, 0x3d, 0x5a, 0x3e};

	zassert_str_equal(encode_id(count).c_str(), "000syw7rIjXKeGogUSqgWP");
	zassert_str_equal(encode_id(mixed).c_str(), "2Mw8fYX4YKmcMOc8sumlKZ");
	zassert_str_equal(encode_id(track).c_str(), "4uLU6hMCjMI75M1A2tKUQC");
}

ZTEST(zspot_base62, test_other_lengths)
{
	std::vector<uint8_t> longer(40, 0);

	zassert_str_equal(base62Encode({0x01, 0x00}, 4).c_str(), "0048");
	/* 3839 is "ZV" in two digits, which is all that is asked for */
	zassert_str_equal(base62Encode({0x0e, 0xff}, 2).c_str(), "ZV");
	/* More bytes than an id has */
	longer[39] = 62;
	zassert_str_equal(base62Encode(longer, 3).c_str(), "010");
	zassert_str_equal(base62Encode({}, 2).c_str(), "00");
	zassert_str_equal(base62Encode({0x01}, 0).c_str(), "");
}

ZTEST_SUITE(zspot_base62, NULL, NULL, NULL, NULL, NULL);
