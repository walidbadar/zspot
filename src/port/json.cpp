/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/json.h"

#include "port/json_glue.h"

namespace cspot::json
{

namespace
{

template <class Encoder> std::string encode_with(Encoder encoder)
{
	const int needed = encoder(nullptr, 0);

	if (needed < 0) {
		return {};
	}

	std::string out(static_cast<size_t>(needed) + 1, '\0');

	if (encoder(out.data(), out.size()) < 0) {
		return {};
	}
	out.resize(static_cast<size_t>(needed));
	return out;
}

} /* namespace */

bool firstOfStringArray(std::string_view document, std::string_view key, std::string &out)
{
	/* The Zephyr parser works in place, so parse a private copy. */
	std::string copy(document);
	std::string key_str(key);
	const char *first = nullptr;

	if (cspot_json_first_of_array(key_str.c_str(), copy.data(), copy.size(), &first) != 0) {
		return false;
	}
	out = first;
	return true;
}

bool parseCredentials(std::string_view document, Credentials &out)
{
	std::string copy(document);
	struct cspot_json_credentials parsed = {};

	if (cspot_json_parse_credentials(copy.data(), copy.size(), &parsed) != 0) {
		return false;
	}
	out.auth_data = parsed.auth_data;
	out.auth_type = parsed.auth_type;
	out.username = parsed.username;
	return true;
}

std::string encodeCredentials(const Credentials &credentials)
{
	struct cspot_json_credentials in = {
		credentials.auth_data.c_str(),
		credentials.auth_type,
		credentials.username.c_str(),
	};

	return encode_with([&](char *buf, size_t size) {
		return cspot_json_encode_credentials(&in, buf, size);
	});
}

std::string encodeZeroconfInfo(const ZeroconfInfo &info)
{
	struct cspot_json_zeroconf_info in = {
		info.version.c_str(),          info.library_version.c_str(),
		info.brand_display_name.c_str(), info.model_display_name.c_str(),
		info.availability.c_str(),     info.device_id.c_str(),
		info.remote_name.c_str(),      info.public_key.c_str(),
	};

	return encode_with([&](char *buf, size_t size) {
		return cspot_json_encode_zeroconf_info(&in, buf, size);
	});
}

} /* namespace cspot::json */
