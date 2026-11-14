/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include <zspot/zspot.h>

#include "credentials.h"

/* The store logs under the name of the player application */
LOG_MODULE_REGISTER(zspot_player, LOG_LEVEL_INF);

#define KEY "zspot/credentials"

static const char sample[] =
	"{\"authData\":\"QUJDREVGR0g=\",\"authType\":1,\"username\":\"someone\"}";

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
	static const char other[] = "{\"authData\":\"WA==\",\"authType\":1,\"username\":\"x\"}";

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

ZTEST(zspot_credentials, test_oversized_are_removed)
{
	static char large[1100];

	memset(large, 'x', sizeof(large));
	zassert_ok(settings_save_one(KEY, large, sizeof(large)));

	zassert_equal(credentials_load(), -EINVAL);
	zassert_equal(load_calls, 0);
	zassert_equal(credentials_load(), -ENOENT);
}

ZTEST_SUITE(zspot_credentials, NULL, NULL, before, NULL, NULL);
