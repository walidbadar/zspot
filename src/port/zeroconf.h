/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Spotify zeroconf authentication endpoint (/spotify_info) served by
 *        the Zephyr HTTP server.
 */

#ifndef CSPOT_PORT_ZEROCONF_H_
#define CSPOT_PORT_ZEROCONF_H_

#include <functional>
#include <memory>

namespace cspot
{

class LoginBlob;

/**
 * @brief Starts serving GET/POST /spotify_info on CONFIG_CSPOT_ZEROCONF_PORT.
 *
 * @p on_credentials is invoked from the HTTP server thread once the Spotify
 * app posted valid credentials into @p blob; it must not block.
 */
int zeroconf_start(std::shared_ptr<LoginBlob> blob, std::function<void()> on_credentials);

void zeroconf_stop();

} /* namespace cspot */

#endif /* CSPOT_PORT_ZEROCONF_H_ */
