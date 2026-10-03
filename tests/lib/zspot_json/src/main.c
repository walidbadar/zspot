/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "port/json_glue.h"

ZTEST(zspot_json, test_first_of_string_array)
{
	char doc[] = "{\"ap_list\":[\"ap-gew1.spotify.com:4070\",\"ap-gue1.spotify.com:443\"]}";
	const char *first = NULL;

	zassert_ok(zspot_json_first_of_array("ap_list", doc, strlen(doc), &first));
	zassert_str_equal(first, "ap-gew1.spotify.com:4070");
}

ZTEST(zspot_json, test_first_of_string_array_ignores_other_keys)
{
	char doc[] = "{\"result\":\"CDN\",\"cdnurl\":[\"https://a.example/x\"],\"fileid\":\"ab\"}";
	const char *first = NULL;

	zassert_ok(zspot_json_first_of_array("cdnurl", doc, strlen(doc), &first));
	zassert_str_equal(first, "https://a.example/x");
}

ZTEST(zspot_json, test_first_of_string_array_missing)
{
	char doc[] = "{\"other\":[\"x\"]}";
	const char *first = NULL;

	zassert_equal(zspot_json_first_of_array("ap_list", doc, strlen(doc), &first), -ENOENT);
	zassert_equal(zspot_json_first_of_array("unknown", doc, strlen(doc), &first), -EINVAL);
}

ZTEST(zspot_json, test_credentials_round_trip)
{
	const struct zspot_json_credentials in = {
		.auth_data = "QUJD", .auth_type = 1, .username = "someone",
	};
	char buf[128];
	int len = zspot_json_encode_credentials(&in, buf, sizeof(buf));
	struct zspot_json_credentials out = {0};

	zassert_true(len > 0);
	zassert_equal(len, (int)strlen(buf));
	zassert_equal(zspot_json_encode_credentials(&in, NULL, 0), len);
	zassert_ok(zspot_json_parse_credentials(buf, len, &out));
	zassert_str_equal(out.auth_data, "QUJD");
	zassert_equal(out.auth_type, 1);
	zassert_str_equal(out.username, "someone");
}

ZTEST(zspot_json, test_credentials_buffer_too_small)
{
	const struct zspot_json_credentials in = {
		.auth_data = "QUJD", .auth_type = 1, .username = "someone",
	};
	char buf[8];

	zassert_equal(zspot_json_encode_credentials(&in, buf, sizeof(buf)), -ENOMEM);
}

ZTEST(zspot_json, test_zeroconf_info)
{
	const struct zspot_json_zeroconf_info in = {
		.version = "2.7.1", .library_version = "1.0.0", .brand_display_name = "zspot",
		.model_display_name = "ZSpot", .availability = "", .device_id = "abc",
		.remote_name = "ZSpot", .public_key = "a2V5",
	};
	char buf[512];
	int len = zspot_json_encode_zeroconf_info(&in, buf, sizeof(buf));

	zassert_true(len > 0);
	zassert_not_null(strstr(buf, "\"status\":101"));
	zassert_not_null(strstr(buf, "\"publicKey\":\"a2V5\""));
	zassert_not_null(strstr(buf, "\"deviceType\":\"SPEAKER\""));
}

ZTEST_SUITE(zspot_json, NULL, NULL, NULL, NULL, NULL);
