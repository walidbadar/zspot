/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "credentials.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <psa/internal_trusted_storage.h>

#include <zspot/zspot.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

/* The entry of the credentials in the secure storage ("zs") */
#define CREDENTIALS_UID 0x7a73
/* Where an earlier version of the player kept them in the clear */
#define PLAIN_SETTING   "zspot/credentials"

/* The JSON on its way between the library and the storage, NUL terminated */
static char json[1024];

BUILD_ASSERT(CONFIG_SECURE_STORAGE_ITS_MAX_DATA_SIZE >= sizeof(json) - 1,
	     "The secure storage must take the credentials of a session");

/* Hands the @p len bytes in the buffer to the library. */
static int use(size_t len)
{
	if (len >= sizeof(json)) {
		return -EINVAL;
	}
	json[len] = '\0';
	return zspot_credentials_load_json(json);
}

static void forget_plain(void)
{
	if (settings_subsys_init() == 0) {
		(void)settings_delete(PLAIN_SETTING);
	}
}

/*
 * Credentials that an earlier version stored in the clear are taken over
 * once: used, stored encrypted, and removed from where they were.
 */
static int load_plain(void)
{
	ssize_t len;
	int ret;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* The length of the value, also of one that does not fit; 0 without one */
	len = settings_load_one(PLAIN_SETTING, json, sizeof(json) - 1);
	if (len == 0) {
		return -ENOENT;
	}
	if (len < 0) {
		LOG_ERR("Cannot read the stored credentials (%d)", (int)len);
		return (int)len;
	}

	ret = use(len);
	if (ret != 0) {
		LOG_WRN("The stored credentials cannot be used, removing them");
		forget_plain();
		return ret;
	}

	/* Kept in the clear until they are there encrypted */
	if (psa_its_set(CREDENTIALS_UID, len, json, PSA_STORAGE_FLAG_NONE) == PSA_SUCCESS) {
		LOG_INF("Stored credentials are encrypted from now on");
		forget_plain();
	}
	return 0;
}

int credentials_load(void)
{
	size_t len = 0;
	psa_status_t status;
	int ret;

	status = psa_its_get(CREDENTIALS_UID, 0, sizeof(json), json, &len);
	if (status == PSA_ERROR_DOES_NOT_EXIST) {
		return load_plain();
	}

	if (status != PSA_SUCCESS) {
		/* Also an entry that was altered, or that another device encrypted */
		LOG_WRN("The stored credentials cannot be read (%d), removing them", (int)status);
		ret = -EIO;
	} else {
		ret = use(len);
		if (ret != 0) {
			LOG_WRN("The stored credentials cannot be used, removing them");
		}
	}
	if (ret != 0) {
		(void)credentials_forget();
	}
	return ret;
}

int credentials_store(void)
{
	psa_status_t status;
	int len;

	len = zspot_credentials_save_json(json, sizeof(json));
	if (len <= 0) {
		LOG_ERR("No credentials to store (%d)", len);
		return len < 0 ? len : -ENOENT;
	}

	/*
	 * Called at every start. That does not wear the flash: the secure
	 * storage writes nothing when the entry holds this data already.
	 */
	status = psa_its_set(CREDENTIALS_UID, len, json, PSA_STORAGE_FLAG_NONE);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Cannot store the credentials (%d)", (int)status);
		return -EIO;
	}

	LOG_INF("Credentials kept for the next start");
	return 0;
}

int credentials_forget(void)
{
	psa_status_t status = psa_its_remove(CREDENTIALS_UID);

	forget_plain();

	if (status != PSA_SUCCESS && status != PSA_ERROR_DOES_NOT_EXIST) {
		LOG_ERR("Cannot remove the stored credentials (%d)", (int)status);
		return -EIO;
	}
	return 0;
}
