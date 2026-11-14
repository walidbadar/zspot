/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

/*
 * The tokens are internal to webapi.c: build that file into the test (see
 * CMakeLists.txt) and call it there. What it needs of the player is below.
 */
#include "webapi.c"

#define BASIC  "Authorization: Basic "
#define BEARER "Authorization: Bearer "

/* The HTTP client: what it was asked last and how it answers, request by request */
static struct {
	int calls;
	char url[2][96];
	char header[2][96];
	char body[2][160];
	int status[4];
	const char *response[4];
} http;

int zspot_http_request(const char *method, const char *url, const char *const *headers,
		       const char *content_type, const char *body, uint8_t *buf, size_t size,
		       size_t *len)
{
	int call = http.calls++;
	const char *response = call < ARRAY_SIZE(http.response) ? http.response[call] : NULL;

	if (call < ARRAY_SIZE(http.url)) {
		snprintf(http.url[call], sizeof(http.url[0]), "%s %s", method, url);
		snprintf(http.header[call], sizeof(http.header[0]), "%s",
			 headers != NULL && headers[0] != NULL ? headers[0] : "");
		snprintf(http.body[call], sizeof(http.body[0]), "%s", body != NULL ? body : "");
	}

	*len = response != NULL ? strlen(response) : 0;
	zassert_true(*len <= size);
	memcpy(buf, response, *len);
	return call < ARRAY_SIZE(http.status) ? http.status[call] : -EIO;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(&http, 0, sizeof(http));

	/* As after a start */
	bearer[0] = '\0';
	expires_at_ms = 0;
}

/* The first request gets a token with the keys, and both go out as Spotify expects them. */
ZTEST(zspot_webapi, test_request_gets_a_token_first)
{
	uint8_t buf[32];
	size_t len = 0;

	zassert_true(webapi_configured());

	http.status[0] = 200;
	http.response[0] =
		"{\"access_token\":\"abc123\",\"token_type\":\"Bearer\",\"expires_in\":3600}";
	http.status[1] = 200;
	http.response[1] = "{\"ok\":true}";

	zassert_equal(
		webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf), &len),
		200);
	zassert_equal(http.calls, 2);
	zassert_str_equal(http.url[0], "POST " TOKEN_URL);
	/* base64 of "id:secret" */
	zassert_str_equal(http.header[0], BASIC "aWQ6c2VjcmV0");
	zassert_str_equal(http.body[0],
			  "grant_type=refresh_token&refresh_token=" CONFIG_ZSPOT_WEB_REFRESH_TOKEN);
	zassert_str_equal(http.url[1], "GET https://api.example/v1/me");
	zassert_str_equal(http.header[1], BEARER "abc123");
	zassert_equal(len, strlen("{\"ok\":true}"));

	/* The token is good for an hour: the next request goes out with it */
	http.calls = 0;
	http.status[0] = 204;
	http.response[0] = "";
	zassert_equal(webapi_request("PUT", "https://api.example/v1/me/player/play", "{}", buf,
				     sizeof(buf), &len),
		      204);
	zassert_equal(http.calls, 1);
	zassert_str_equal(http.header[0], BEARER "abc123");
	zassert_str_equal(http.body[0], "{}");
}

/* A token that Spotify no longer takes is replaced once, and the request repeated. */
ZTEST(zspot_webapi, test_request_renews_a_refused_token)
{
	uint8_t buf[32];
	size_t len = 0;

	http.status[0] = 200;
	http.response[0] = "{\"access_token\":\"old\",\"expires_in\":3600}";
	http.status[1] = 401;
	http.response[1] = "{}";
	http.status[2] = 200;
	http.response[2] = "{\"access_token\":\"new\",\"expires_in\":3600}";
	http.status[3] = 200;
	http.response[3] = "{}";

	zassert_equal(
		webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf), &len),
		200);
	zassert_equal(http.calls, 4);
	zassert_str_equal(bearer, BEARER "new");
}

/* Keys that Spotify refuses give no token, and say so. */
ZTEST(zspot_webapi, test_request_with_refused_keys)
{
	uint8_t buf[32];
	size_t len = 0;

	http.status[0] = 400;
	http.response[0] = "{\"error\":\"invalid_grant\"}";

	zassert_equal(
		webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf), &len),
		-EACCES);
	zassert_equal(http.calls, 1);
}

/* Spotify cannot be reached: the error of the client is passed on. */
ZTEST(zspot_webapi, test_request_without_network)
{
	uint8_t buf[32];
	size_t len = 0;

	http.status[0] = -ENETUNREACH;

	zassert_equal(
		webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf), &len),
		-ENETUNREACH);
	zassert_equal(http.calls, 1);
	zassert_str_equal(bearer, "");
}

ZTEST_SUITE(zspot_webapi, NULL, NULL, before, NULL, NULL);
