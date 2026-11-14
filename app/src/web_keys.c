/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "web_keys.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define KEY "zspot/webapi"

/* Layout of struct web_keys; stored keys of another one are not read */
#define FORMAT 1

/* Whether @p len characters of @p value can be kept in @p size bytes and sent as they are. */
static bool is_usable(const char *value, size_t len, size_t size)
{
	if (len == 0 || len >= size) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		if (!isalnum((unsigned char)value[i]) && value[i] != '-' && value[i] != '_') {
			return false;
		}
	}
	return true;
}

static bool set(struct web_keys *keys, const char *client_id, size_t id_len,
		const char *client_secret, size_t secret_len, const char *refresh_token,
		size_t token_len)
{
	if (client_id == NULL || client_secret == NULL || refresh_token == NULL ||
	    !is_usable(client_id, id_len, sizeof(keys->client_id)) ||
	    !is_usable(client_secret, secret_len, sizeof(keys->client_secret)) ||
	    !is_usable(refresh_token, token_len, sizeof(keys->refresh_token))) {
		return false;
	}

	/* The same keys give the same bytes, which the settings do not write twice */
	memset(keys, 0, sizeof(*keys));
	keys->format = FORMAT;
	memcpy(keys->client_id, client_id, id_len);
	memcpy(keys->client_secret, client_secret, secret_len);
	memcpy(keys->refresh_token, refresh_token, token_len);
	return true;
}

bool web_keys_set(struct web_keys *keys, const char *client_id, const char *client_secret,
		  const char *refresh_token)
{
	return set(keys, client_id, client_id != NULL ? strlen(client_id) : 0, client_secret,
		   client_secret != NULL ? strlen(client_secret) : 0, refresh_token,
		   refresh_token != NULL ? strlen(refresh_token) : 0);
}

bool web_keys_from_form(struct web_keys *keys, const char *form, size_t len)
{
	static const char *const names[] = {"client_id", "client_secret", "refresh_token"};
	const char *values[ARRAY_SIZE(names)] = {NULL};
	size_t lengths[ARRAY_SIZE(names)] = {0};
	const char *end = form + len;

	for (const char *field = form; field < end;) {
		const char *next = memchr(field, '&', end - field);
		const char *field_end = next != NULL ? next : end;
		const char *equals = memchr(field, '=', field_end - field);

		for (size_t i = 0; equals != NULL && i < ARRAY_SIZE(names); i++) {
			if ((size_t)(equals - field) == strlen(names[i]) &&
			    memcmp(field, names[i], equals - field) == 0) {
				values[i] = equals + 1;
				lengths[i] = field_end - values[i];
			}
		}
		field = field_end + 1;
	}

	return set(keys, values[0], lengths[0], values[1], lengths[1], values[2], lengths[2]);
}

#if defined(CONFIG_SETTINGS)

/* What was read from the flash is not taken on trust. */
static bool is_valid(const struct web_keys *keys)
{
	const char *const texts[] = {keys->client_id, keys->client_secret, keys->refresh_token};
	const size_t sizes[] = {sizeof(keys->client_id), sizeof(keys->client_secret),
				sizeof(keys->refresh_token)};

	if (keys->format != FORMAT) {
		return false;
	}
	for (size_t i = 0; i < ARRAY_SIZE(texts); i++) {
		const char *terminator = memchr(texts[i], '\0', sizes[i]);

		if (terminator == NULL || !is_usable(texts[i], terminator - texts[i], sizes[i])) {
			return false;
		}
	}
	return true;
}

int web_keys_load(struct web_keys *keys)
{
	ssize_t len;
	int ret;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* The length of the value, also of one that does not fit; 0 without one */
	len = settings_load_one(KEY, keys, sizeof(*keys));
	if (len == 0) {
		return -ENOENT;
	}
	if (len < 0) {
		LOG_ERR("Cannot read the Web API credentials (%d)", (int)len);
		return (int)len;
	}
	if (len != sizeof(*keys) || !is_valid(keys)) {
		LOG_WRN("The stored Web API credentials cannot be used, removing them");
		(void)web_keys_forget();
		return -EINVAL;
	}
	return 0;
}

int web_keys_store(const struct web_keys *keys)
{
	int ret;

	if (!is_valid(keys)) {
		return -EINVAL;
	}

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* NVS and ZMS write nothing when the value is the one they hold. */
	ret = settings_save_one(KEY, keys, sizeof(*keys));
	if (ret != 0) {
		LOG_ERR("Cannot store the Web API credentials (%d)", ret);
	}
	return ret;
}

int web_keys_forget(void)
{
	int ret = settings_subsys_init();

	if (ret == 0) {
		ret = settings_delete(KEY);
	}
	if (ret != 0) {
		LOG_ERR("Cannot remove the Web API credentials (%d)", ret);
	}
	return ret;
}

#else /* Nowhere to keep them: only those of the build */

int web_keys_load(struct web_keys *keys)
{
	ARG_UNUSED(keys);
	return -ENOENT;
}

int web_keys_store(const struct web_keys *keys)
{
	ARG_UNUSED(keys);
	return -ENOTSUP;
}

int web_keys_forget(void)
{
	return 0;
}

#endif /* CONFIG_SETTINGS */
