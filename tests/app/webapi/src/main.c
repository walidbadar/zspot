/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

/*
 * The checks, the store and the tokens are internal to webapi.c: build that
 * file into the test (see CMakeLists.txt) and call it there. What it needs of
 * the player is below.
 */
#include "webapi.c"

#define KEY    KEYS_SETTING
/* Made up, in the form of the real ones */
#define ID     "0123456789abcdef0123456789abcdef"
#define SECRET "fedcba9876543210fedcba9876543210"
#define TOKEN  "AQBx-Example_refresh-token_0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP"

#define BASIC  "Authorization: Basic "
#define BEARER "Authorization: Bearer "

/* The keys of a test; the file under test has the ones in use under the name keys */
static struct web_keys test_keys;
#define keys test_keys

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

	zassert_ok(web_keys_forget());
	memset(&test_keys, 0xa5, sizeof(test_keys));
	memset(&http, 0, sizeof(http));

	/* As after a start without stored credentials */
	bearer[0] = '\0';
	expires_at_ms = 0;
	webapi_init();
}

static bool from_form(const char *form)
{
	return web_keys_from_form(&keys, form, strlen(form));
}

ZTEST(zspot_webapi, test_set)
{
	zassert_true(web_keys_set(&keys, ID, SECRET, TOKEN));
	zassert_str_equal(keys.client_id, ID);
	zassert_str_equal(keys.client_secret, SECRET);
	zassert_str_equal(keys.refresh_token, TOKEN);
}

/* What is missing, too long or would need escaping in a request is refused. */
ZTEST(zspot_webapi, test_set_refuses)
{
	static char long_token[WEB_KEYS_TOKEN_MAX + 1];

	memset(long_token, 'a', sizeof(long_token) - 1);

	zassert_false(web_keys_set(&keys, "", SECRET, TOKEN));
	zassert_false(web_keys_set(&keys, ID, "", TOKEN));
	zassert_false(web_keys_set(&keys, ID, SECRET, ""));
	zassert_false(web_keys_set(&keys, NULL, SECRET, TOKEN));
	zassert_false(web_keys_set(&keys, ID, SECRET, long_token));
	zassert_false(web_keys_set(&keys, ID ":", SECRET, TOKEN));
	zassert_false(web_keys_set(&keys, ID, SECRET "&x=y", TOKEN));
	zassert_false(web_keys_set(&keys, ID, SECRET, "to ken"));
	zassert_false(web_keys_set(&keys, ID, SECRET, "tok\"en"));

	/* The longest that fits */
	long_token[WEB_KEYS_TOKEN_MAX - 1] = '\0';
	zassert_true(web_keys_set(&keys, ID, SECRET, long_token));
}

ZTEST(zspot_webapi, test_form)
{
	zassert_true(from_form("client_id=" ID "&client_secret=" SECRET "&refresh_token=" TOKEN));
	zassert_str_equal(keys.client_id, ID);
	zassert_str_equal(keys.client_secret, SECRET);
	zassert_str_equal(keys.refresh_token, TOKEN);

	/* Any order, other fields in between */
	zassert_true(from_form("refresh_token=" TOKEN "&x=1&client_secret=" SECRET
			       "&other=&client_id=" ID "&"));
	zassert_str_equal(keys.client_id, ID);
	zassert_str_equal(keys.refresh_token, TOKEN);
}

ZTEST(zspot_webapi, test_form_refuses)
{
	static const char whole[] = "client_id=" ID "&client_secret=" SECRET "&refresh_token=" TOKEN;

	zassert_false(from_form(""));
	zassert_false(from_form("client_id=" ID "&client_secret=" SECRET));
	zassert_false(from_form("client_id=&client_secret=" SECRET "&refresh_token=" TOKEN));
	zassert_false(from_form("client_id" ID "&client_secret=" SECRET "&refresh_token=" TOKEN));
	/* A name that only begins or ends like one of the three */
	zassert_false(from_form("xclient_id=" ID "&client_secret=" SECRET "&refresh_token=" TOKEN));
	zassert_false(from_form("client_id2=" ID "&client_secret=" SECRET "&refresh_token=" TOKEN));
	/* Escapes are not undone: the values need none */
	zassert_false(from_form("client_id=" ID "%20&client_secret=" SECRET "&refresh_token=" TOKEN));

	/* Only the bytes that are there count */
	zassert_true(web_keys_from_form(&keys, whole, strlen(whole)));
	zassert_false(web_keys_from_form(&keys, whole, strlen("client_id=" ID "&client_secret=")));
}

ZTEST(zspot_webapi, test_nothing_stored)
{
	zassert_equal(web_keys_load(&keys), -ENOENT);
	zassert_ok(web_keys_forget());
}

ZTEST(zspot_webapi, test_store_and_load)
{
	struct web_keys loaded;

	zassert_true(web_keys_set(&keys, ID, SECRET, TOKEN));
	zassert_ok(web_keys_store(&keys));
	zassert_ok(web_keys_store(&keys));

	memset(&loaded, 0x5a, sizeof(loaded));
	zassert_ok(web_keys_load(&loaded));
	zassert_mem_equal(&loaded, &keys, sizeof(keys));

	zassert_ok(web_keys_forget());
	zassert_equal(web_keys_load(&loaded), -ENOENT);
}

ZTEST(zspot_webapi, test_store_refuses_unset)
{
	zassert_equal(web_keys_store(&keys), -EINVAL);
	zassert_equal(web_keys_load(&keys), -ENOENT);
}

static void store_raw(const void *raw, size_t size)
{
	struct web_keys loaded;

	zassert_ok(settings_save_one(KEY, raw, size));
	zassert_equal(web_keys_load(&loaded), -EINVAL);
	/* They were removed */
	zassert_equal(web_keys_load(&loaded), -ENOENT);
}

ZTEST(zspot_webapi, test_unusable_are_removed)
{
	struct web_keys raw;

	zassert_true(web_keys_set(&keys, ID, SECRET, TOKEN));

	raw = keys;
	raw.format++;
	store_raw(&raw, sizeof(raw));

	store_raw(&keys, sizeof(keys) - 4);

	raw = keys;
	raw.client_secret[3] = '"';
	store_raw(&raw, sizeof(raw));

	raw = keys;
	raw.client_id[0] = '\0';
	store_raw(&raw, sizeof(raw));

	/* A text without an end */
	raw = keys;
	memset(raw.refresh_token, 'a', sizeof(raw.refresh_token));
	store_raw(&raw, sizeof(raw));
}

/* Without credentials nothing is asked for. */
ZTEST(zspot_webapi, test_request_without_keys)
{
	uint8_t buf[32];
	size_t len;

	zassert_false(webapi_configured());
	zassert_equal(webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf),
				     &len),
		      -ENOENT);
	zassert_equal(http.calls, 0);
}

/* The first request gets a token with the keys, and both go out as Spotify expects them. */
ZTEST(zspot_webapi, test_request_gets_a_token_first)
{
	uint8_t buf[32];
	size_t len = 0;

	zassert_true(web_keys_set(&keys, "id", "secret", TOKEN));
	zassert_ok(webapi_set_keys(&keys));
	zassert_true(webapi_configured());

	http.status[0] = 200;
	http.response[0] = "{\"access_token\":\"abc123\",\"token_type\":\"Bearer\",\"expires_in\":3600}";
	http.status[1] = 200;
	http.response[1] = "{\"ok\":true}";

	zassert_equal(webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf),
				     &len),
		      200);
	zassert_equal(http.calls, 2);
	zassert_str_equal(http.url[0], "POST " TOKEN_URL);
	/* base64 of "id:secret" */
	zassert_str_equal(http.header[0], BASIC "aWQ6c2VjcmV0");
	zassert_str_equal(http.body[0], "grant_type=refresh_token&refresh_token=" TOKEN);
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

	zassert_true(web_keys_set(&keys, "id", "secret", TOKEN));
	zassert_ok(webapi_set_keys(&keys));

	http.status[0] = 200;
	http.response[0] = "{\"access_token\":\"old\",\"expires_in\":3600}";
	http.status[1] = 401;
	http.response[1] = "{}";
	http.status[2] = 200;
	http.response[2] = "{\"access_token\":\"new\",\"expires_in\":3600}";
	http.status[3] = 200;
	http.response[3] = "{}";

	zassert_equal(webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf),
				     &len),
		      200);
	zassert_equal(http.calls, 4);
	zassert_str_equal(bearer, BEARER "new");
}

/* Keys that Spotify refuses give no token, and say so. */
ZTEST(zspot_webapi, test_request_with_refused_keys)
{
	uint8_t buf[32];
	size_t len = 0;

	zassert_true(web_keys_set(&keys, "id", "secret", TOKEN));
	zassert_ok(webapi_set_keys(&keys));

	http.status[0] = 400;
	http.response[0] = "{\"error\":\"invalid_grant\"}";

	zassert_equal(webapi_request("GET", "https://api.example/v1/me", NULL, buf, sizeof(buf),
				     &len),
		      -EACCES);
	zassert_equal(http.calls, 1);
}

/* New keys take the place of the old ones, their token included, and are there after a start. */
ZTEST(zspot_webapi, test_set_keys)
{
	uint8_t buf[32];
	size_t len = 0;

	zassert_true(web_keys_set(&keys, "first", "secret", TOKEN));
	zassert_ok(webapi_set_keys(&keys));
	http.status[0] = 200;
	http.response[0] = "{\"access_token\":\"one\",\"expires_in\":3600}";
	http.status[1] = 200;
	zassert_equal(webapi_request("GET", "https://api.example/a", NULL, buf, sizeof(buf), &len),
		      200);

	zassert_true(web_keys_set(&keys, "second", "secret", TOKEN));
	zassert_ok(webapi_set_keys(&keys));
	memset(&http, 0, sizeof(http));
	http.status[0] = 200;
	http.response[0] = "{\"access_token\":\"two\",\"expires_in\":3600}";
	http.status[1] = 200;
	zassert_equal(webapi_request("GET", "https://api.example/a", NULL, buf, sizeof(buf), &len),
		      200);
	/* base64 of "second:secret" */
	zassert_str_equal(http.header[0], BASIC "c2Vjb25kOnNlY3JldA==");
	zassert_str_equal(http.header[1], BEARER "two");

	/* As after a restart */
	configured = false;
	webapi_init();
	zassert_true(webapi_configured());
}

ZTEST_SUITE(zspot_webapi, NULL, NULL, before, NULL, NULL);
