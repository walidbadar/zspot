/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/json_glue.h"

#include <errno.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>

#define MAX_ARRAY 8

struct string_array_doc {
	const char *items[MAX_ARRAY];
	size_t count;
};

static const struct json_obj_descr ap_list_descr[] = {
	JSON_OBJ_DESCR_ARRAY_NAMED(struct string_array_doc, "ap_list", items, MAX_ARRAY, count,
				   JSON_TOK_STRING),
};

static const struct json_obj_descr cdn_url_descr[] = {
	JSON_OBJ_DESCR_ARRAY_NAMED(struct string_array_doc, "cdnurl", items, MAX_ARRAY, count,
				   JSON_TOK_STRING),
};

struct credentials_doc {
	const char *authData;
	int32_t authType;
	const char *username;
};

static const struct json_obj_descr credentials_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct credentials_doc, authData, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct credentials_doc, authType, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct credentials_doc, username, JSON_TOK_STRING),
};

struct zeroconf_info_doc {
	int32_t status;
	const char *statusString;
	const char *version;
	int32_t spotifyError;
	const char *libraryVersion;
	const char *accountReq;
	const char *brandDisplayName;
	const char *modelDisplayName;
	const char *voiceSupport;
	const char *availability;
	int32_t productID;
	const char *tokenType;
	const char *groupStatus;
	const char *resolverVersion;
	const char *scope;
	const char *activeUser;
	const char *deviceID;
	const char *remoteName;
	const char *publicKey;
	const char *deviceType;
};

static const struct json_obj_descr zeroconf_info_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, status, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, statusString, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, spotifyError, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, libraryVersion, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, accountReq, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, brandDisplayName, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, modelDisplayName, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, voiceSupport, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, availability, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, productID, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, tokenType, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, groupStatus, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, resolverVersion, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, scope, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, activeUser, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, deviceID, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, remoteName, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, publicKey, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct zeroconf_info_doc, deviceType, JSON_TOK_STRING),
};

static int encode(const struct json_obj_descr *descr, size_t descr_len, const void *val,
		  char *buf, size_t size)
{
	ssize_t needed = json_calc_encoded_len(descr, descr_len, val);
	int ret;

	if (needed < 0) {
		return (int)needed;
	}
	if (buf == NULL) {
		return (int)needed;
	}
	if ((size_t)needed + 1 > size) {
		return -ENOMEM;
	}

	ret = json_obj_encode_buf(descr, descr_len, val, buf, size);
	if (ret < 0) {
		return ret;
	}
	return (int)needed;
}

int cspot_json_first_of_array(const char *key, char *doc, size_t len, const char **first)
{
	const struct json_obj_descr *descr;
	size_t descr_len;
	struct string_array_doc parsed = {0};
	int ret;

	if (strcmp(key, "ap_list") == 0) {
		descr = ap_list_descr;
		descr_len = ARRAY_SIZE(ap_list_descr);
	} else if (strcmp(key, "cdnurl") == 0) {
		descr = cdn_url_descr;
		descr_len = ARRAY_SIZE(cdn_url_descr);
	} else {
		return -EINVAL;
	}

	ret = json_obj_parse(doc, len, descr, descr_len, &parsed);
	if (ret < 0) {
		return ret;
	}
	if ((ret & 1) == 0 || parsed.count == 0 || parsed.items[0] == NULL) {
		return -ENOENT;
	}

	*first = parsed.items[0];
	return 0;
}

int cspot_json_parse_credentials(char *doc, size_t len, struct cspot_json_credentials *out)
{
	struct credentials_doc parsed = {0};
	int ret;

	ret = json_obj_parse(doc, len, credentials_descr, ARRAY_SIZE(credentials_descr),
			     &parsed);
	if (ret < 0) {
		return ret;
	}
	if ((ret & 0x7) != 0x7) {
		return -ENOENT;
	}

	out->auth_data = parsed.authData;
	out->auth_type = parsed.authType;
	out->username = parsed.username;
	return 0;
}

int cspot_json_encode_credentials(const struct cspot_json_credentials *in, char *buf,
				  size_t size)
{
	struct credentials_doc doc = {
		.authData = in->auth_data,
		.authType = in->auth_type,
		.username = in->username,
	};

	return encode(credentials_descr, ARRAY_SIZE(credentials_descr), &doc, buf, size);
}

int cspot_json_encode_zeroconf_info(const struct cspot_json_zeroconf_info *in, char *buf,
				    size_t size)
{
	struct zeroconf_info_doc doc = {
		.status = 101,
		.statusString = "OK",
		.version = in->version,
		.spotifyError = 0,
		.libraryVersion = in->library_version,
		.accountReq = "PREMIUM",
		.brandDisplayName = in->brand_display_name,
		.modelDisplayName = in->model_display_name,
		.voiceSupport = "NO",
		.availability = in->availability,
		.productID = 0,
		.tokenType = "default",
		.groupStatus = "NONE",
		.resolverVersion = "0",
		.scope = "streaming,client-authorization-universal",
		.activeUser = "",
		.deviceID = in->device_id,
		.remoteName = in->remote_name,
		.publicKey = in->public_key,
		.deviceType = "SPEAKER",
	};

	return encode(zeroconf_info_descr, ARRAY_SIZE(zeroconf_info_descr), &doc, buf, size);
}
