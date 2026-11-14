/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include <zspot/zspot.h>

#include "credentials.h"

/* The store logs under the name of the player application */
LOG_MODULE_REGISTER(zspot_player, LOG_LEVEL_INF);

/* Where an earlier version kept the credentials in the clear */
#define PLAIN  "zspot/credentials"
/* The subtree of the secure storage in the settings */
#define SECURE "its"

static const char sample[] =
	"{\"authData\":\"QUJDREVGR0g=\",\"authType\":1,\"username\":\"someone\"}";
static const char other[] = "{\"authData\":\"WA==\",\"authType\":1,\"username\":\"x\"}";
/* More than the store takes */
static char large[1100];

/* The entry of the secure storage as the settings hold it */
static struct {
	int count;
	char name[32];
	uint8_t value[1100];
	size_t len;
} stored;

/* The library: what it holds, what it was handed and how it answers */
static const char *session_json;
static char loaded_json[1200];
static int load_calls;
static int load_result;

int zspot_credentials_save_json(char *buf, size_t size)
{
	size_t len;

	if (session_json == NULL) {
		return -ENOENT;
	}
	len = strlen(session_json);
	if (len + 1 > size) {
		return -ENOMEM;
	}
	memcpy(buf, session_json, len + 1);
	return (int)len;
}

int zspot_credentials_load_json(const char *json)
{
	load_calls++;
	zassert_true(strlen(json) < sizeof(loaded_json));
	strcpy(loaded_json, json);
	return load_result;
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

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(credentials_forget());
	session_json = NULL;
	loaded_json[0] = '\0';
	load_calls = 0;
	load_result = 0;
}

ZTEST(zspot_credentials, test_nothing_stored)
{
	zassert_equal(credentials_load(), -ENOENT);
	zassert_equal(load_calls, 0);
}

ZTEST(zspot_credentials, test_store_and_load)
{
	session_json = sample;
	zassert_ok(credentials_store());

	zassert_ok(credentials_load());
	zassert_equal(load_calls, 1);
	zassert_str_equal(loaded_json, sample);
}

ZTEST(zspot_credentials, test_store_replaces)
{
	session_json = sample;
	zassert_ok(credentials_store());
	/* Shorter than the first: nothing of that may be left behind it */
	session_json = other;
	zassert_ok(credentials_store());
	/* The same again is accepted as well */
	zassert_ok(credentials_store());

	zassert_ok(credentials_load());
	zassert_str_equal(loaded_json, other);
}

ZTEST(zspot_credentials, test_forget)
{
	session_json = sample;
	zassert_ok(credentials_store());
	zassert_ok(credentials_forget());

	zassert_equal(credentials_load(), -ENOENT);
	zassert_equal(load_calls, 0);
	/* Nothing left to forget is not an error */
	zassert_ok(credentials_forget());
}

ZTEST(zspot_credentials, test_without_session)
{
	zassert_equal(credentials_store(), -ENOENT);
	zassert_equal(credentials_load(), -ENOENT);
}

ZTEST(zspot_credentials, test_unusable_are_removed)
{
	session_json = sample;
	zassert_ok(credentials_store());

	load_result = -EINVAL;
	zassert_equal(credentials_load(), -EINVAL);
	zassert_equal(load_calls, 1);

	load_result = 0;
	zassert_equal(credentials_load(), -ENOENT);
	zassert_equal(load_calls, 1);
}

/* The settings hold one entry, and nothing of it can be read. */
ZTEST(zspot_credentials, test_stored_encrypted)
{
	char plain[8];

	session_json = sample;
	zassert_ok(credentials_store());

	zassert_equal(settings_load_one(PLAIN, plain, sizeof(plain)), 0);
	find_stored();
	zassert_equal(stored.count, 1);
	zassert_true(stored.len > strlen(sample));
	zassert_false(holds(stored.value, stored.len, "someone"));
	zassert_false(holds(stored.value, stored.len, "authData"));
	zassert_false(holds(stored.value, stored.len, "QUJDREVGR0g="));
}

/* Storing the same again writes nothing: the entry stays as it is, nonce included. */
ZTEST(zspot_credentials, test_same_is_not_written_again)
{
	uint8_t first[sizeof(stored.value)];
	size_t first_len;

	session_json = sample;
	zassert_ok(credentials_store());
	find_stored();
	first_len = stored.len;
	memcpy(first, stored.value, first_len);

	zassert_ok(credentials_store());
	find_stored();
	zassert_equal(stored.len, first_len);
	zassert_mem_equal(stored.value, first, first_len);
}

/* An entry that was altered is not decrypted into something else: it is refused and removed. */
ZTEST(zspot_credentials, test_altered_are_removed)
{
	session_json = sample;
	zassert_ok(credentials_store());

	find_stored();
	stored.value[stored.len / 2] ^= 0x01;
	zassert_ok(settings_save_one(stored.name, stored.value, stored.len));

	zassert_equal(credentials_load(), -EIO);
	zassert_equal(load_calls, 0);
	zassert_equal(credentials_load(), -ENOENT);
	find_stored();
	zassert_equal(stored.count, 0);
}

/* What an earlier version left in the clear is used once, encrypted and removed. */
ZTEST(zspot_credentials, test_plain_are_taken_over)
{
	char plain[8];

	zassert_ok(settings_save_one(PLAIN, sample, strlen(sample)));

	zassert_ok(credentials_load());
	zassert_equal(load_calls, 1);
	zassert_str_equal(loaded_json, sample);
	zassert_equal(settings_load_one(PLAIN, plain, sizeof(plain)), 0);
	find_stored();
	zassert_equal(stored.count, 1);
	zassert_false(holds(stored.value, stored.len, "someone"));

	/* From the secure storage from now on */
	loaded_json[0] = '\0';
	zassert_ok(credentials_load());
	zassert_str_equal(loaded_json, sample);
}

ZTEST(zspot_credentials, test_unusable_plain_are_removed)
{
	zassert_ok(settings_save_one(PLAIN, sample, strlen(sample)));

	load_result = -EINVAL;
	zassert_equal(credentials_load(), -EINVAL);
	load_result = 0;
	zassert_equal(credentials_load(), -ENOENT);
	find_stored();
	zassert_equal(stored.count, 0);
}

ZTEST(zspot_credentials, test_oversized_plain_are_removed)
{
	memset(large, 'x', sizeof(large));
	zassert_ok(settings_save_one(PLAIN, large, sizeof(large)));

	zassert_equal(credentials_load(), -EINVAL);
	zassert_equal(load_calls, 0);
	zassert_equal(credentials_load(), -ENOENT);
}

/* Forgetting takes both with it. */
ZTEST(zspot_credentials, test_forget_plain)
{
	char plain[8];

	zassert_ok(settings_save_one(PLAIN, sample, strlen(sample)));
	zassert_ok(credentials_forget());
	zassert_equal(settings_load_one(PLAIN, plain, sizeof(plain)), 0);
	zassert_equal(credentials_load(), -ENOENT);
}

ZTEST_SUITE(zspot_credentials, NULL, NULL, before, NULL, NULL);
