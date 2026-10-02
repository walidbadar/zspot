/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/ApResolve.h"

#include <stdexcept>
#include <string_view>

#include "port/http.h"
#include "port/json.h"
#include "port/log.h"

CSPOT_LOG_MODULE_DECLARE();

using namespace cspot;

ApResolve::ApResolve(std::string apOverride) : apOverride(apOverride) {}

std::string ApResolve::fetchFirstApAddress() {
  if (!apOverride.empty()) {
    return apOverride;
  }

  auto response = HttpConnection::fetch("GET", "https://apresolve.spotify.com/");
  std::string_view body(reinterpret_cast<const char*>(response.body.data()),
                        response.body.size());

  std::string address;
  if (!json::firstOfStringArray(body, "ap_list", address)) {
    CSPOT_LOG(error, "Unexpected apresolve response (status %d)",
              response.status);
    throw std::runtime_error("Cannot resolve access point");
  }
  return address;
}
