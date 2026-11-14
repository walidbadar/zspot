/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "credentials.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zspot/zspot.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define KEY "zspot/credentials"

/* The JSON on its way between the library and the settings, NUL terminated */
static char json[1024];

int credentials_load(void)
{
	ssize_t len;
	int ret;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* The length of the value, also of one that does not fit; 0 without one */
	len = settings_load_one(KEY, json, sizeof(json) - 1);
	if (len == 0) {
		return -ENOENT;
	}
	if (len < 0) {
		LOG_ERR("Cannot read the stored credentials (%d)", (int)len);
		return (int)len;
	}

	if ((size_t)len >= sizeof(json)) {
		ret = -EINVAL;
	} else {
		json[len] = '\0';
		ret = zspot_credentials_load_json(json);
	}
	if (ret != 0) {
		LOG_WRN("The stored credentials cannot be used, removing them");
		(void)credentials_forget();
	}
	return ret;
}

int credentials_store(void)
{
	int len;
	int ret;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	len = zspot_credentials_save_json(json, sizeof(json));
	if (len <= 0) {
		LOG_ERR("No credentials to store (%d)", len);
		return len < 0 ? len : -ENOENT;
	}

	/*
	 * Called at every start. That does not wear the flash: NVS and ZMS
	 * write nothing when the value is the one they hold.
	 */
	ret = settings_save_one(KEY, json, len);
	if (ret != 0) {
		LOG_ERR("Cannot store the credentials (%d)", ret);
		return ret;
	}

	LOG_INF("Credentials kept for the next start");
	return 0;
}

int credentials_forget(void)
{
	int ret = settings_subsys_init();

	if (ret == 0) {
		ret = settings_delete(KEY);
	}
	if (ret != 0) {
		LOG_ERR("Cannot remove the stored credentials (%d)", ret);
	}
	return ret;
}
