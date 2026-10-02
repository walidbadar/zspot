/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/zeroconf.h"

#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include <cerrno>
#include <map>
#include <stdexcept>
#include <string>

#include "core/LoginBlob.h"
#include "core/Utils.h"
#include "port/log.h"

CSPOT_LOG_MODULE_DECLARE();

namespace cspot
{

namespace
{

std::shared_ptr<LoginBlob> blob_ref;
std::function<void()> credentials_cb;
std::string request_body;
std::string response_body;

const struct http_header json_header = {"Content-Type", "application/json"};

std::map<std::string, std::string> parse_form(const std::string &body)
{
	std::map<std::string, std::string> params;
	size_t pos = 0;

	while (pos < body.size()) {
		size_t end = body.find('&', pos);

		if (end == std::string::npos) {
			end = body.size();
		}

		const std::string pair = body.substr(pos, end - pos);
		const size_t eq = pair.find('=');

		if (eq != std::string::npos) {
			params[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
		}
		pos = end + 1;
	}
	return params;
}

void respond(struct http_response_ctx *response, const std::string &body)
{
	response_body = body;
	response->status = HTTP_200_OK;
	response->headers = &json_header;
	response->header_count = 1;
	response->body = reinterpret_cast<const uint8_t *>(response_body.data());
	response->body_len = response_body.size();
	response->final_chunk = true;
}

int spotify_info_handler(struct http_client_ctx *client, enum http_transaction_status status,
			 const struct http_request_ctx *request,
			 struct http_response_ctx *response, void *)
{
	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		request_body.clear();
		return 0;
	}

	if (request->data != nullptr && request->data_len > 0) {
		request_body.append(reinterpret_cast<const char *>(request->data),
				    request->data_len);
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0; /* wait for the rest of the body */
	}

	if (blob_ref == nullptr) {
		respond(response, "{\"status\":402,\"spotifyError\":0,"
				  "\"statusString\":\"ERROR-NOT-INITIALISED\"}");
		return 0;
	}

	if (client->method != HTTP_POST) {
		/* GET ?action=getInfo */
		respond(response, blob_ref->buildZeroconfInfo());
		return 0;
	}

	auto params = parse_form(request_body);

	request_body.clear();

	if (params["action"] != "addUser") {
		respond(response, "{\"status\":301,\"spotifyError\":0,"
				  "\"statusString\":\"ERROR-INVALID-ACTION\"}");
		return 0;
	}

	try {
		blob_ref->loadZeroconfQuery(params);
	} catch (const std::exception &e) {
		LOG_ERR("Zeroconf credentials rejected: %s", e.what());
		respond(response, "{\"status\":203,\"spotifyError\":0,"
				  "\"statusString\":\"ERROR-INVALID-ARGUMENTS\"}");
		return 0;
	}

	LOG_INF("Received Spotify credentials for %s", blob_ref->getUserName().c_str());
	respond(response, "{\"status\":101,\"spotifyError\":0,\"statusString\":\"ERROR-OK\"}");

	if (credentials_cb) {
		credentials_cb();
	}
	return 0;
}

struct http_resource_detail_dynamic spotify_info_detail = {
	.common = {
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
	},
	.cb = spotify_info_handler,
	.holder = nullptr,
	.user_data = nullptr,
};

uint16_t zeroconf_port = CONFIG_CSPOT_ZEROCONF_PORT;

} /* namespace */
} /* namespace cspot */

HTTP_SERVICE_DEFINE(cspot_zeroconf_service, nullptr, &cspot::zeroconf_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, 2, nullptr, nullptr, nullptr);

HTTP_RESOURCE_DEFINE(cspot_zeroconf_resource, cspot_zeroconf_service, "/spotify_info",
		     &cspot::spotify_info_detail);

int cspot::zeroconf_start(std::shared_ptr<LoginBlob> blob, std::function<void()> on_credentials)
{
	int ret;

	blob_ref = std::move(blob);
	credentials_cb = std::move(on_credentials);

	ret = http_server_start();
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Cannot start HTTP server (%d)", ret);
		return ret;
	}

	LOG_INF("Zeroconf endpoint listening on port %u", zeroconf_port);
	return 0;
}

void cspot::zeroconf_stop()
{
	http_server_stop();
	credentials_cb = nullptr;
}
