/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/http.h"

#include <zephyr/kernel.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <mbedtls/ssl_ciphersuites.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "port/log.h"

ZSPOT_LOG_MODULE_DECLARE();

namespace zspot
{

/* URL --------------------------------------------------------------------- */

HttpUrl HttpUrl::parse(const std::string &url)
{
	HttpUrl result;
	std::string rest = url;
	const auto scheme_end = rest.find("://");

	if (scheme_end != std::string::npos) {
		result.tls = (rest.substr(0, scheme_end) == "https");
		rest = rest.substr(scheme_end + 3);
	}
	result.port = result.tls ? 443 : 80;

	const auto path_start = rest.find('/');
	std::string authority = rest.substr(0, path_start);

	result.path = path_start == std::string::npos ? "/" : rest.substr(path_start);

	const auto colon = authority.find(':');

	if (colon != std::string::npos) {
		result.port = static_cast<uint16_t>(std::stoi(authority.substr(colon + 1)));
		authority = authority.substr(0, colon);
	}
	result.host = authority;

	if (result.host.empty()) {
		throw std::invalid_argument("Invalid URL");
	}
	return result;
}

/* Connection -------------------------------------------------------------- */

HttpConnection::~HttpConnection()
{
	close();
}

void HttpConnection::close()
{
	if (sock_ >= 0) {
		zsock_close(sock_);
		sock_ = -1;
	}
	used_ = false;
}

/* A lookup that the resolver is too busy for is repeated */
static const int DNS_ATTEMPTS = 10;
static const int DNS_RETRY_DELAY_MS = 200;

void HttpConnection::open(const HttpUrl &url)
{
	struct zsock_addrinfo hints;
	struct zsock_addrinfo *results = nullptr;
	char port_str[8];
	int sock = -1;
	int ret;

	close();

	snprintf(port_str, sizeof(port_str), "%u", url.port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	/*
	 * The resolver handles CONFIG_DNS_NUM_CONCUR_QUERIES lookups at a time
	 * and turns further ones down at once: wait for a free slot.
	 */
	for (int attempt = 0; attempt < DNS_ATTEMPTS; attempt++) {
		ret = zsock_getaddrinfo(url.host.c_str(), port_str, &hints, &results);
		if (ret != DNS_EAI_AGAIN) {
			break;
		}
		k_msleep(DNS_RETRY_DELAY_MS);
	}
	if (ret != 0 || results == nullptr) {
		LOG_ERR("DNS lookup of %s failed (%d)", url.host.c_str(), ret);
		throw std::runtime_error("DNS lookup failed");
	}

	/*
	 * Cipher suites offered, in two rounds. The Spotify hosts are reached
	 * with ECDHE-RSA, as verified; several of them would pick ECDHE-ECDSA as
	 * soon as it is offered. That is only offered when a host accepts
	 * nothing else.
	 */
	static const int rsa_suites[] = {MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
					 MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384};
	static const int ecdsa_suites[] = {MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256};
	const int rounds = url.tls ? 2 : 1;
	int last_errno = 0;

	for (int round = 0; round < rounds && sock < 0; round++) {
		for (struct zsock_addrinfo *ai = results; ai != nullptr; ai = ai->ai_next) {
			struct timeval timeout;

			sock = zsock_socket(ai->ai_family, ai->ai_socktype,
					    url.tls ? static_cast<int>(IPPROTO_TLS_1_2)
						    : static_cast<int>(IPPROTO_TCP));
			if (sock < 0) {
				continue;
			}

			if (url.tls) {
#if CONFIG_ZSPOT_TLS_SEC_TAG >= 0
				static const sec_tag_t sec_tags[] = {CONFIG_ZSPOT_TLS_SEC_TAG};
				int verify = TLS_PEER_VERIFY_REQUIRED;

				zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags,
						 sizeof(sec_tags));
#else
				int verify = TLS_PEER_VERIFY_NONE;
#endif
				zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, url.host.c_str(),
						 url.host.size() + 1);
				zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify,
						 sizeof(verify));
				if (round == 0) {
					zsock_setsockopt(sock, SOL_TLS, TLS_CIPHERSUITE_LIST,
							 rsa_suites, sizeof(rsa_suites));
				} else {
					zsock_setsockopt(sock, SOL_TLS, TLS_CIPHERSUITE_LIST,
							 ecdsa_suites, sizeof(ecdsa_suites));
				}
			}

			timeout.tv_sec = CONFIG_ZSPOT_HTTP_TIMEOUT_MS / 1000;
			timeout.tv_usec = (CONFIG_ZSPOT_HTTP_TIMEOUT_MS % 1000) * 1000;
			zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

			if (zsock_connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
				break;
			}

			last_errno = errno;
			zsock_close(sock);
			sock = -1;
		}
	}
	if (sock < 0) {
		LOG_ERR("Connect to %s:%u failed (errno %d)", url.host.c_str(), url.port,
			last_errno);
	}
	zsock_freeaddrinfo(results);

	if (sock < 0) {
		throw std::runtime_error("Cannot connect to " + url.host);
	}

	sock_ = sock;
	url_ = url;
	used_ = false;
}

namespace
{

int on_response(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
	auto *out = static_cast<HttpResponse *>(user_data);

	if (rsp->body_frag_start != nullptr && rsp->body_frag_len > 0) {
		out->body.insert(out->body.end(), rsp->body_frag_start,
				 rsp->body_frag_start + rsp->body_frag_len);
	}

	if (final_data == HTTP_DATA_FINAL) {
		out->status = rsp->http_status_code;
		out->has_content_length = rsp->cl_present;
		out->content_length = rsp->content_length;
		/*
		 * cr_present is only a transient parser flag in Zephyr's HTTP
		 * client and is already cleared here; the parsed values remain.
		 */
		out->range_start = rsp->content_range.start;
		out->range_end = rsp->content_range.end;
		out->range_total = rsp->content_range.total;
		out->has_content_range = rsp->content_range.total > 0;
	}
	return 0;
}

enum http_method to_method(const char *method)
{
	if (strcmp(method, "POST") == 0) {
		return HTTP_POST;
	}
	if (strcmp(method, "PUT") == 0) {
		return HTTP_PUT;
	}
	if (strcmp(method, "HEAD") == 0) {
		return HTTP_HEAD;
	}
	return HTTP_GET;
}

} /* namespace */

int HttpConnection::execute(const char *method, const HttpUrl &url, const Headers &headers,
			    const std::vector<uint8_t> &payload, const char *content_type,
			    HttpResponse &response)
{
	std::vector<std::string> lines;
	std::vector<const char *> header_fields;
	std::vector<uint8_t> recv_buf(CONFIG_ZSPOT_HTTP_RECV_BUF_SIZE);
	struct http_request req;

	lines.reserve(headers.size());
	header_fields.reserve(headers.size() + 1);
	for (const auto &header : headers) {
		lines.push_back(header + "\r\n");
	}
	for (const auto &line : lines) {
		header_fields.push_back(line.c_str());
	}
	header_fields.push_back(nullptr);

	response.body.clear();

	memset(&req, 0, sizeof(req));
	req.method = to_method(method);
	req.url = url.path.c_str();
	req.host = url.host.c_str();
	req.protocol = "HTTP/1.1";
	req.response = on_response;
	req.header_fields = header_fields.data();
	req.recv_buf = recv_buf.data();
	req.recv_buf_len = recv_buf.size();
	if (!payload.empty()) {
		req.payload = reinterpret_cast<const char *>(payload.data());
		req.payload_len = payload.size();
	}
	req.content_type_value = content_type;

	return http_client_req(sock_, &req, CONFIG_ZSPOT_HTTP_TIMEOUT_MS, &response);
}

HttpResponse HttpConnection::request(const char *method, const std::string &url,
				     const Headers &headers, const std::vector<uint8_t> &payload,
				     const char *content_type)
{
	HttpUrl parsed = HttpUrl::parse(url);
	HttpResponse response;
	int ret;

	if (!isOpen() || parsed.host != url_.host || parsed.port != url_.port ||
	    parsed.tls != url_.tls) {
		open(parsed);
	}

	ret = execute(method, parsed, headers, payload, content_type, response);
	if (ret < 0 && used_) {
		/* The keep-alive connection was dropped by the server; retry once. */
		LOG_DBG("Keep-alive request failed (%d), reconnecting", ret);
		open(parsed);
		ret = execute(method, parsed, headers, payload, content_type, response);
	}

	if (ret < 0) {
		LOG_ERR("%s %s failed (%d)", method, parsed.host.c_str(), ret);
		close();
		throw std::runtime_error("HTTP request failed");
	}

	LOG_DBG("%s %s%s -> %d, %u bytes (ret %d)", method, parsed.host.c_str(),
		parsed.path.substr(0, 40).c_str(), response.status,
		static_cast<unsigned int>(response.body.size()), ret);
	used_ = true;
	return response;
}

HttpResponse HttpConnection::fetch(const char *method, const std::string &url,
				   const Headers &headers, const std::vector<uint8_t> &payload,
				   const char *content_type)
{
	HttpConnection connection;

	return connection.request(method, url, headers, payload, content_type);
}

std::string HttpConnection::rangeHeader(size_t from, size_t to)
{
	return "Range: bytes=" + std::to_string(from) + "-" + std::to_string(to);
}

std::string HttpConnection::lastBytesHeader(size_t bytes)
{
	return "Range: bytes=-" + std::to_string(bytes);
}

} /* namespace zspot */
