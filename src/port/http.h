/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief HTTP(S) client built on Zephyr BSD sockets, TLS sockets and the
 *        Zephyr HTTP client library (subsys/net/lib/http).
 */

#ifndef CSPOT_PORT_HTTP_H_
#define CSPOT_PORT_HTTP_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cspot
{

struct HttpUrl {
	bool tls = false;
	std::string host;
	uint16_t port = 80;
	std::string path = "/";

	static HttpUrl parse(const std::string &url);
};

struct HttpResponse {
	int status = 0;

	bool has_content_length = false;
	size_t content_length = 0;

	bool has_content_range = false;
	size_t range_start = 0;
	size_t range_end = 0;
	size_t range_total = 0;

	std::vector<uint8_t> body;

	/** Total size reported by Content-Range, or Content-Length otherwise. */
	size_t totalLength() const
	{
		return has_content_range ? range_total : content_length;
	}
};

/**
 * @brief A keep-alive HTTP connection.
 *
 * Requests are executed synchronously; the body is collected into
 * HttpResponse::body.
 */
class HttpConnection
{
public:
	/** Header lines without the trailing CRLF, e.g. "Range: bytes=0-100". */
	using Headers = std::vector<std::string>;

	HttpConnection() = default;
	~HttpConnection();

	HttpConnection(const HttpConnection &) = delete;
	HttpConnection &operator=(const HttpConnection &) = delete;

	void open(const HttpUrl &url);
	void close();
	bool isOpen() const
	{
		return sock_ >= 0;
	}

	/**
	 * Performs a request on this connection, opening or re-opening it when
	 * necessary. Throws std::runtime_error when the transfer fails.
	 */
	HttpResponse request(const char *method, const std::string &url,
			     const Headers &headers = {}, const std::vector<uint8_t> &payload = {},
			     const char *content_type = nullptr);

	HttpResponse get(const std::string &url, const Headers &headers = {})
	{
		return request("GET", url, headers);
	}

	/** One-shot helper: connect, request, close. */
	static HttpResponse fetch(const char *method, const std::string &url,
				  const Headers &headers = {},
				  const std::vector<uint8_t> &payload = {},
				  const char *content_type = nullptr);

	static std::string rangeHeader(size_t from, size_t to);
	static std::string lastBytesHeader(size_t bytes);

private:
	int execute(const char *method, const HttpUrl &url, const Headers &headers,
		    const std::vector<uint8_t> &payload, const char *content_type,
		    HttpResponse &response);

	int sock_ = -1;
	HttpUrl url_;
	bool used_ = false;
};

} /* namespace cspot */

#endif /* CSPOT_PORT_HTTP_H_ */
