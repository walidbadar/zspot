/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Spotify zeroconf authentication endpoint (/spotify_info).
 *
 * Served by a minimal HTTP/1.1 responder on Zephyr sockets. Spotify clients
 * only accept replies framed with Content-Length and a full status line,
 * which the Zephyr HTTP server does not produce for dynamic resources.
 */

#ifndef ZSPOT_PORT_ZEROCONF_H_
#define ZSPOT_PORT_ZEROCONF_H_

#include <functional>
#include <memory>

#include "port/sync.h"

namespace cspot
{
class LoginBlob;
} /* namespace cspot */
namespace zspot
{

/**
 * @brief Starts serving GET/POST /spotify_info on CONFIG_ZSPOT_ZEROCONF_PORT.
 *
 * @p on_credentials is invoked from the responder thread once the Spotify
 * app posted valid credentials into @p blob; it must not block. The responder
 * holds @p lock whenever it reads or writes @p blob, and not while it calls
 * @p on_credentials.
 */
int zeroconf_start(std::shared_ptr<cspot::LoginBlob> blob, Mutex *lock,
		   std::function<void()> on_credentials);

void zeroconf_stop();

} /* namespace zspot */

#endif /* ZSPOT_PORT_ZEROCONF_H_ */
