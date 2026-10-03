/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief The few JSON documents the protocol exchanges, encoded and parsed
 *        with Zephyr's JSON library (zephyr/data/json.h).
 */

#ifndef ZSPOT_PORT_JSON_H_
#define ZSPOT_PORT_JSON_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace zspot::json
{

/** Reads the first element of the string array at @p key ("ap_list" or "cdnurl"). */
bool firstOfStringArray(std::string_view document, std::string_view key, std::string &out);

/** Stored credentials: {"authData": <base64>, "authType": n, "username": s} */
struct Credentials {
	std::string auth_data;
	int auth_type = 0;
	std::string username;
};

bool parseCredentials(std::string_view document, Credentials &out);
std::string encodeCredentials(const Credentials &credentials);

/** Response to the zeroconf getInfo request. */
struct ZeroconfInfo {
	std::string version;
	std::string library_version;
	std::string brand_display_name;
	std::string model_display_name;
	std::string availability;
	std::string device_id;
	std::string remote_name;
	std::string public_key;
};

std::string encodeZeroconfInfo(const ZeroconfInfo &info);

} /* namespace zspot::json */

#endif /* ZSPOT_PORT_JSON_H_ */
