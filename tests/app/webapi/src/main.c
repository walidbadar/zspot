/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

/*
 * The tokens are internal to webapi.c: build that file into the test (see
 * CMakeLists.txt) and call it there. What it needs of the player is below.
 */
#include "webapi.c"

#define BASIC  "Authorization: Basic "
#define BEARER "Authorization: Bearer "

/* The credentials of the build (see CMakeLists.txt), and others */
#define TOKEN  CONFIG_ZSPOT_WEB_REFRESH_TOKEN
#define SECRET "fedcba9876543210fedcba9876543210"
/* The subtree of the secure storage in the settings */
#define SECURE "its"

#define TRACK    "spotify:track:5sWHDYs0csV6RS48xBl0tH"
#define PLAYLIST "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M"
#define PLAY     "PUT https://api.spotify.com/v1/me/player/play?device_id=device123"
#define TOKEN_OK "{\"access_token\":\"abc123\",\"expires_in\":3600}"

/* The HTTP client: what it was asked last and how it answers, request by request */
static struct {
	int calls;
	char url[4][96];
	char header[4][96];
	char body[4][160];
	int status[4];
	const char *response[4];
	/* The answer waits for http_gate */
	bool hold;
} http;
static K_SEM_DEFINE(http_gate, 0, 1);

/* The entry of the secure storage as the settings hold it */
static struct {
	int count;
	char name[32];
	uint8_t value[600];
	size_t len;
} stored;

/* What webapi_play() reported */
static K_SEM_DEFINE(play_reported, 0, 1);
static bool play_accepted;

/* Longer than a URI of Spotify is */
static const char long_uri[] = "spotify:track:5sWHDYs0csV6RS48xBl0tH5sWHDYs0csV6RS48xBl0tH"
			       "5sWHDYs0csV6RS48xBl0tH";

/* Work given to webapi_submit(), and the thread it ran on */
static void other_handler(struct k_work *work);
static K_WORK_DEFINE(other_work, other_handler);
static K_SEM_DEFINE(other_done, 0, 1);
static k_tid_t other_thread;

static void other_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	other_thread = k_current_get();
	k_sem_give(&other_done);
}

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

	if (http.hold) {
		k_sem_take(&http_gate, K_FOREVER);
	}

	*len = response != NULL ? strlen(response) : 0;
	zassert_true(*len <= size);
	memcpy(buf, response, *len);
	return call < ARRAY_SIZE(http.status) ? http.status[call] : -EIO;
}

/* The memory of the library is the kernel heap here */
void *zspot_mem_alloc(size_t size)
{
	return k_malloc(size);
}

void zspot_mem_free(void *ptr)
{
	k_free(ptr);
}

const char *zspot_device_id(void)
{
	return "device123";
}

static void on_play_done(bool accepted)
{
	play_accepted = accepted;
	k_sem_give(&play_reported);
}

static int read_stored(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
		       void *param)
{
	ARG_UNUSED(param);

	stored.count++;
	snprintf(stored.name, sizeof(stored.name), SECURE "/%s", key);
	zassert_true(len <= sizeof(stored.value));
	stored.len = read_cb(cb_arg, stored.value, len);
	return 0;
}

/* Reads what the secure storage put into the settings. */
static void find_stored(void)
{
	memset(&stored, 0, sizeof(stored));
	zassert_ok(settings_load_subtree_direct(SECURE, read_stored, NULL));
}

static bool holds(const uint8_t *data, size_t len, const char *text)
{
	size_t text_len = strlen(text);

	for (size_t i = 0; i + text_len <= len; i++) {
		if (memcmp(&data[i], text, text_len) == 0) {
			return true;
		}
	}
	return false;
}

/* What a token request was authorised with, after one request. */
static const char *basic_of_a_request(void)
{
	uint8_t buf[32];
	size_t len = 0;

	memset(&http, 0, sizeof(http));
	bearer[0] = '\0';
	http.status[0] = 200;
	http.response[0] = TOKEN_OK;
	http.status[1] = 200;
	zassert_equal(webapi_request("GET", "https://api.example/a", NULL, buf, sizeof(buf), &len),
		      200);
	return http.header[0];
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* A device that never had credentials, started with a build that carries some */
	(void)psa_its_remove(KEYS_UID);
	zassert_ok(settings_subsys_init());
	(void)settings_delete(KEYS_PLAIN_SETTING);

	memset(&http, 0, sizeof(http));
	k_sem_reset(&http_gate);
	k_sem_reset(&play_reported);

	/* As after a start */
	bearer[0] = '\0';
	expires_at_ms = 0;
	zassert_ok(webapi_init());
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

/* A track of a playlist is started within it, from where it was paused. */
ZTEST(zspot_webapi, test_play_within_its_playlist)
{
	http.status[0] = 200;
	http.response[0] = TOKEN_OK;
	http.status[1] = 204;

	zassert_ok(webapi_play(TRACK, PLAYLIST, 61000, on_play_done));
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_true(play_accepted);
	zassert_equal(http.calls, 2);
	zassert_str_equal(http.url[1], PLAY);
	zassert_str_equal(http.header[1], BEARER "abc123");
	zassert_str_equal(http.body[1],
			  "{\"context_uri\":\"" PLAYLIST "\",\"offset\":{\"uri\":\"" TRACK
			  "\"},\"position_ms\":61000}");
}

/* Without a context that the Web API takes an offset into, the track is played alone. */
ZTEST(zspot_webapi, test_play_alone)
{
	http.status[0] = 200;
	http.response[0] = TOKEN_OK;
	http.status[1] = 204;

	zassert_ok(webapi_play(TRACK, "spotify:artist:0OdUWJ0sBjDrqHygGUXeCF", 0, on_play_done));
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_true(play_accepted);
	zassert_equal(http.calls, 2);
	zassert_str_equal(http.body[1], "{\"uris\":[\"" TRACK "\"],\"position_ms\":0}");

	http.calls = 0;
	http.status[0] = 204;
	zassert_ok(webapi_play(TRACK, "", 7, on_play_done));
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_true(play_accepted);
	zassert_str_equal(http.body[0], "{\"uris\":[\"" TRACK "\"],\"position_ms\":7}");
}

/* A track that is no longer part of its playlist is played alone. */
ZTEST(zspot_webapi, test_play_alone_when_the_playlist_refuses)
{
	http.status[0] = 200;
	http.response[0] = TOKEN_OK;
	http.status[1] = 404;
	http.response[1] = "{\"error\":{\"status\":404,\"message\":\"Not found\"}}";
	http.status[2] = 204;

	zassert_ok(webapi_play(TRACK, PLAYLIST, 61000, on_play_done));
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_true(play_accepted);
	zassert_equal(http.calls, 3);
	zassert_str_equal(http.url[2], PLAY);
	zassert_str_equal(http.body[2], "{\"uris\":[\"" TRACK "\"],\"position_ms\":61000}");
}

ZTEST(zspot_webapi, test_play_refused)
{
	http.status[0] = 200;
	http.response[0] = TOKEN_OK;
	http.status[1] = 404;
	http.status[2] = 403;

	zassert_ok(webapi_play(TRACK, PLAYLIST, 0, on_play_done));
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_false(play_accepted);
	zassert_equal(http.calls, 3);

	/* The next one is taken again */
	http.calls = 0;
	http.status[0] = 204;
	zassert_ok(webapi_play(TRACK, "", 0, on_play_done));
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_true(play_accepted);
}

ZTEST(zspot_webapi, test_play_one_at_a_time)
{
	http.status[0] = 200;
	http.response[0] = TOKEN_OK;
	http.status[1] = 204;
	http.hold = true;

	zassert_ok(webapi_play(TRACK, "", 0, on_play_done));
	zassert_equal(webapi_play(TRACK, "", 0, on_play_done), -EBUSY);

	/* The token, then the request */
	k_sem_give(&http_gate);
	zassert_equal(k_sem_take(&play_reported, K_MSEC(100)), -EAGAIN);
	k_sem_give(&http_gate);
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_true(play_accepted);
	zassert_equal(http.calls, 2);
}

/* What would not fit is not cut: it would be another track. */
ZTEST(zspot_webapi, test_play_refuses_long_uris)
{
	zassert_equal(webapi_play(long_uri, "", 0, on_play_done), -EINVAL);
	zassert_equal(webapi_play(TRACK, long_uri, 0, on_play_done), -EINVAL);
	zassert_equal(http.calls, 0);
}

/* Work of others runs on the thread of the requests. */
ZTEST(zspot_webapi, test_submit)
{
	zassert_true(webapi_submit(&other_work) >= 0);
	zassert_ok(k_sem_take(&other_done, K_SECONDS(5)));
	zassert_equal(other_thread, k_work_queue_thread_get(&work_queue));
}

/* The build carried them to the device: there they are, and nothing of them can be read. */
ZTEST(zspot_webapi, test_keys_of_the_build_are_stored_encrypted)
{
	struct keys plain;

	zassert_true(webapi_configured());
	find_stored();
	zassert_equal(stored.count, 1);
	zassert_true(stored.len > sizeof(struct keys));
	zassert_false(holds(stored.value, stored.len, "secret"));
	zassert_false(holds(stored.value, stored.len, TOKEN));
	zassert_equal(settings_load_one(KEYS_PLAIN_SETTING, &plain, sizeof(plain)), 0);
}

/* The firmware after it comes without them and finds them on the device. */
ZTEST(zspot_webapi, test_stored_keys_serve_a_build_without)
{
	keys_init("", "", "");
	zassert_true(webapi_configured());
	/* base64 of "id:secret" */
	zassert_str_equal(basic_of_a_request(), BASIC "aWQ6c2VjcmV0");
	zassert_str_equal(http.body[0], "grant_type=refresh_token&refresh_token=" TOKEN);
}

ZTEST(zspot_webapi, test_without_keys)
{
	uint8_t buf[32];
	size_t len = 0;

	zassert_ok(psa_its_remove(KEYS_UID));
	keys_init("", "", "");

	zassert_false(webapi_configured());
	zassert_equal(webapi_request("GET", "https://api.example/a", NULL, buf, sizeof(buf), &len),
		      -ENOENT);
	zassert_equal(http.calls, 0);
	zassert_equal(webapi_play(TRACK, "", 0, on_play_done), 0);
	zassert_ok(k_sem_take(&play_reported, K_SECONDS(5)));
	zassert_false(play_accepted);
}

/* A build that carries others replaces the stored ones. */
ZTEST(zspot_webapi, test_keys_of_a_build_replace_the_stored)
{
	keys_init("second", SECRET, TOKEN);
	keys_init("", "", "");

	/* base64 of "second:fedcba9876543210fedcba9876543210" */
	zassert_str_equal(basic_of_a_request(),
			  BASIC "c2Vjb25kOmZlZGNiYTk4NzY1NDMyMTBmZWRjYmE5ODc2NTQzMjEw");
}

/* Half a set, or what would need escaping in a request, is none: the stored ones stay. */
ZTEST(zspot_webapi, test_unusable_keys_of_a_build)
{
	keys_init("other", "", TOKEN);
	zassert_true(webapi_configured());
	zassert_str_equal(basic_of_a_request(), BASIC "aWQ6c2VjcmV0");

	keys_init("other", "se cret", TOKEN);
	zassert_str_equal(basic_of_a_request(), BASIC "aWQ6c2VjcmV0");

	keys_init("other", "secret\"", TOKEN);
	zassert_str_equal(basic_of_a_request(), BASIC "aWQ6c2VjcmV0");
}

/* An entry that was altered is refused and removed. */
ZTEST(zspot_webapi, test_altered_keys_are_removed)
{
	find_stored();
	stored.value[stored.len / 2] ^= 0x01;
	zassert_ok(settings_save_one(stored.name, stored.value, stored.len));

	keys_init("", "", "");
	zassert_false(webapi_configured());
	find_stored();
	zassert_equal(stored.count, 0);
}

/* What an earlier version left in the clear is used once, encrypted and removed. */
ZTEST(zspot_webapi, test_plain_keys_are_taken_over)
{
	struct keys plain;

	zassert_ok(psa_its_remove(KEYS_UID));
	zassert_true(keys_fill(&plain, "earlier", SECRET, TOKEN));
	zassert_ok(settings_save_one(KEYS_PLAIN_SETTING, &plain, sizeof(plain)));

	keys_init("", "", "");
	zassert_true(webapi_configured());
	zassert_equal(settings_load_one(KEYS_PLAIN_SETTING, &plain, sizeof(plain)), 0);
	find_stored();
	zassert_equal(stored.count, 1);
	zassert_false(holds(stored.value, stored.len, SECRET));

	/* From the secure storage from now on */
	keys_init("", "", "");
	/* base64 of "earlier:fedcba9876543210fedcba9876543210" */
	zassert_str_equal(basic_of_a_request(),
			  BASIC "ZWFybGllcjpmZWRjYmE5ODc2NTQzMjEwZmVkY2JhOTg3NjU0MzIxMA==");
}

/* Once they are stored encrypted, a copy in the clear does not stay either. */
ZTEST(zspot_webapi, test_plain_keys_do_not_stay)
{
	struct keys plain;

	zassert_true(keys_fill(&plain, "earlier", SECRET, TOKEN));
	zassert_ok(settings_save_one(KEYS_PLAIN_SETTING, &plain, sizeof(plain)));

	keys_init("id", "secret", TOKEN);
	zassert_equal(settings_load_one(KEYS_PLAIN_SETTING, &plain, sizeof(plain)), 0);
	zassert_str_equal(basic_of_a_request(), BASIC "aWQ6c2VjcmV0");
}

ZTEST_SUITE(zspot_webapi, NULL, NULL, before, NULL, NULL);
