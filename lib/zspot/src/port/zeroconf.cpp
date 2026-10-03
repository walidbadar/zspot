/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/zeroconf.h"

#include <zephyr/net/socket.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "core/LoginBlob.h"
#include "core/Utils.h"
#include "port/log.h"
#include "port/thread.h"

ZSPOT_LOG_MODULE_DECLARE();

namespace zspot
{

namespace
{

constexpr size_t MAX_REQUEST = 8192;
constexpr int CLIENT_TIMEOUT_S = 5;

const char *const JSON_OK = "{\"status\":101,\"spotifyError\":0,\"statusString\":\"ERROR-OK\"}";
const char *const JSON_INVALID_ACTION =
	"{\"status\":301,\"spotifyError\":0,\"statusString\":\"ERROR-INVALID-ACTION\"}";
const char *const JSON_INVALID_ARGUMENTS =
	"{\"status\":203,\"spotifyError\":0,\"statusString\":\"ERROR-INVALID-ARGUMENTS\"}";

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

class ZeroconfServer : public Task
{
public:
	ZeroconfServer(std::shared_ptr<cspot::LoginBlob> blob, std::function<void()> on_credentials)
		: Task("zspot_zeroconf", CONFIG_ZSPOT_ZEROCONF_STACK_SIZE, 0),
		  blob_(std::move(blob)), on_credentials_(std::move(on_credentials))
	{
	}

	~ZeroconfServer() override
	{
		stop();
	}

	int start(uint16_t port)
	{
		struct sockaddr_in addr;
		int one = 1;

		listen_fd_ = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listen_fd_ < 0) {
			return -errno;
		}
		zsock_setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if (zsock_bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr),
			       sizeof(addr)) < 0 ||
		    zsock_listen(listen_fd_, 2) < 0) {
			const int err = -errno;

			zsock_close(listen_fd_);
			listen_fd_ = -1;
			return err;
		}

		running_ = true;
		if (!startTask()) {
			stop();
			return -ENOMEM;
		}
		return 0;
	}

	void stop()
	{
		running_ = false;
		if (listen_fd_ >= 0) {
			zsock_close(listen_fd_); /* unblocks accept() */
			listen_fd_ = -1;
		}
		joinTask();
	}

protected:
	void runTask() override
	{
		while (running_) {
			struct timeval timeout = {CLIENT_TIMEOUT_S, 0};
			int client = zsock_accept(listen_fd_, nullptr, nullptr);

			if (client < 0) {
				if (running_) {
					k_msleep(100);
				}
				continue;
			}

			zsock_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
			handle(client);
			zsock_close(client);
		}
	}

private:
	/* Reads the request head and, if announced, the body. */
	bool read_request(int fd, std::string &head, std::string &body)
	{
		std::string data;
		char chunk[512];
		size_t head_end = std::string::npos;

		while (data.size() < MAX_REQUEST) {
			const ssize_t n = zsock_recv(fd, chunk, sizeof(chunk), 0);

			if (n <= 0) {
				return false;
			}
			data.append(chunk, n);
			head_end = data.find("\r\n\r\n");
			if (head_end != std::string::npos) {
				break;
			}
		}
		if (head_end == std::string::npos) {
			return false;
		}

		head = data.substr(0, head_end);
		body = data.substr(head_end + 4);

		size_t content_length = 0;
		std::string lower = head;

		for (auto &c : lower) {
			c = static_cast<char>(tolower(c));
		}
		const size_t cl = lower.find("content-length:");

		if (cl != std::string::npos) {
			content_length = strtoul(lower.c_str() + cl + 15, nullptr, 10);
		}
		if (content_length > MAX_REQUEST) {
			return false;
		}

		while (body.size() < content_length) {
			const ssize_t n = zsock_recv(fd, chunk, sizeof(chunk), 0);

			if (n <= 0) {
				return false;
			}
			body.append(chunk, n);
		}
		body.resize(content_length);
		return true;
	}

	void respond(int fd, int status, const char *reason, const std::string &body)
	{
		char header[160];
		const int len = snprintf(header, sizeof(header),
					 "HTTP/1.1 %d %s\r\n"
					 "Content-Type: application/json\r\n"
					 "Content-Length: %u\r\n"
					 "Connection: close\r\n\r\n",
					 status, reason, static_cast<unsigned int>(body.size()));

		zsock_send(fd, header, len, 0);
		size_t sent = 0;

		while (sent < body.size()) {
			const ssize_t n = zsock_send(fd, body.data() + sent, body.size() - sent, 0);

			if (n <= 0) {
				break;
			}
			sent += n;
		}
	}

	void handle(int fd)
	{
		std::string head;
		std::string body;

		if (!read_request(fd, head, body)) {
			respond(fd, 400, "Bad Request", JSON_INVALID_ARGUMENTS);
			return;
		}

		/* Request line: METHOD SP target SP version */
		const size_t sp1 = head.find(' ');
		const size_t sp2 = head.find(' ', sp1 + 1);

		if (sp1 == std::string::npos || sp2 == std::string::npos) {
			respond(fd, 400, "Bad Request", JSON_INVALID_ARGUMENTS);
			return;
		}

		const std::string method = head.substr(0, sp1);
		std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
		std::string query;
		const size_t qmark = target.find('?');

		if (qmark != std::string::npos) {
			query = target.substr(qmark + 1);
			target = target.substr(0, qmark);
		}

		if (target != "/spotify_info") {
			respond(fd, 404, "Not Found", JSON_INVALID_ACTION);
			return;
		}

		if (method == "GET") {
			respond(fd, 200, "OK", blob_->buildZeroconfInfo());
			return;
		}
		if (method != "POST") {
			respond(fd, 405, "Method Not Allowed", JSON_INVALID_ACTION);
			return;
		}

		auto params = parse_form(body);

		for (auto &kv : parse_form(query)) {
			params.insert(kv);
		}

		if (params["action"] != "addUser") {
			respond(fd, 200, "OK", JSON_INVALID_ACTION);
			return;
		}

		try {
			blob_->loadZeroconfQuery(params);
		} catch (const std::exception &e) {
			LOG_ERR("Zeroconf credentials rejected: %s", e.what());
			respond(fd, 200, "OK", JSON_INVALID_ARGUMENTS);
			return;
		}

		LOG_INF("Received Spotify credentials for %s", blob_->getUserName().c_str());
		respond(fd, 200, "OK", JSON_OK);

		if (on_credentials_) {
			on_credentials_();
		}
	}

	std::shared_ptr<cspot::LoginBlob> blob_;
	std::function<void()> on_credentials_;
	int listen_fd_ = -1;
	volatile bool running_ = false;
};

std::unique_ptr<ZeroconfServer> server;

} /* namespace */

int zeroconf_start(std::shared_ptr<cspot::LoginBlob> blob, std::function<void()> on_credentials)
{
	if (server) {
		return -EALREADY;
	}

	server = std::make_unique<ZeroconfServer>(std::move(blob), std::move(on_credentials));

	const int ret = server->start(CONFIG_ZSPOT_ZEROCONF_PORT);

	if (ret < 0) {
		LOG_ERR("Cannot start zeroconf endpoint (%d)", ret);
		server.reset();
		return ret;
	}

	LOG_INF("Zeroconf endpoint listening on port %u", CONFIG_ZSPOT_ZEROCONF_PORT);
	return 0;
}

void zeroconf_stop()
{
	server.reset();
}

} /* namespace zspot */
