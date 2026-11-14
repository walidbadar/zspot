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

#include "web_keys.h"

/* The store logs under the name of the player application */
LOG_MODULE_REGISTER(zspot_player, LOG_LEVEL_INF);

#define KEY    "zspot/webapi"
/* Made up, in the form of the real ones */
#define ID     "0123456789abcdef0123456789abcdef"
#define SECRET "fedcba9876543210fedcba9876543210"
#define TOKEN  "AQBx-Example_refresh-token_0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP"

static struct web_keys keys;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(web_keys_forget());
	memset(&keys, 0xa5, sizeof(keys));
}

static bool from_form(const char *form)
{
	return web_keys_from_form(&keys, form, strlen(form));
}

ZTEST(zspot_web_keys, test_set)
{
	zassert_true(web_keys_set(&keys, ID, SECRET, TOKEN));
	zassert_str_equal(keys.client_id, ID);
	zassert_str_equal(keys.client_secret, SECRET);
	zassert_str_equal(keys.refresh_token, TOKEN);
}

/* What is missing, too long or would need escaping in a request is refused. */
ZTEST(zspot_web_keys, test_set_refuses)
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

ZTEST(zspot_web_keys, test_form)
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

ZTEST(zspot_web_keys, test_form_refuses)
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

ZTEST(zspot_web_keys, test_nothing_stored)
{
	zassert_equal(web_keys_load(&keys), -ENOENT);
	zassert_ok(web_keys_forget());
}

ZTEST(zspot_web_keys, test_store_and_load)
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

ZTEST(zspot_web_keys, test_store_refuses_unset)
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

ZTEST(zspot_web_keys, test_unusable_are_removed)
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

ZTEST_SUITE(zspot_web_keys, NULL, NULL, before, NULL, NULL);
