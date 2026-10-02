/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief C glue around zephyr/data/json.h. The descriptor macros use mixed
 *        designated initialisers, which C++ rejects, so they live in C.
 */

#ifndef CSPOT_PORT_JSON_GLUE_H_
#define CSPOT_PORT_JSON_GLUE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reads the first element of the string array at @p key.
 *
 * @param key   "ap_list" or "cdnurl".
 * @param doc   Document, modified in place by the parser.
 * @param len   Document length.
 * @param first Receives a pointer into @p doc.
 * @return 0 on success, negative errno otherwise.
 */
int cspot_json_first_of_array(const char *key, char *doc, size_t len, const char **first);

struct cspot_json_credentials {
	const char *auth_data;
	int32_t auth_type;
	const char *username;
};

/** Parses {"authData": s, "authType": n, "username": s}; @p doc is modified in place. */
int cspot_json_parse_credentials(char *doc, size_t len, struct cspot_json_credentials *out);

/**
 * @brief Encodes credentials.
 *
 * @return The encoded length (excluding NUL). When @p buf is NULL only the
 *         length is computed. Negative errno on failure.
 */
int cspot_json_encode_credentials(const struct cspot_json_credentials *in, char *buf,
				  size_t size);

struct cspot_json_zeroconf_info {
	const char *version;
	const char *library_version;
	const char *brand_display_name;
	const char *model_display_name;
	const char *availability;
	const char *device_id;
	const char *remote_name;
	const char *public_key;
};

/** Encodes the zeroconf getInfo response; same contract as the credentials encoder. */
int cspot_json_encode_zeroconf_info(const struct cspot_json_zeroconf_info *in, char *buf,
				    size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CSPOT_PORT_JSON_GLUE_H_ */
