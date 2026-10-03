/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/TimeProvider.h"

#include "port/log.h"
#include "core/Utils.h"       // for extract, getCurrentTimestamp
#ifndef _WIN32
#include <zephyr/net/net_ip.h>

ZSPOT_LOG_MODULE_DECLARE();
#endif

using namespace cspot;

TimeProvider::TimeProvider() {}

void TimeProvider::syncWithPingPacket(const std::vector<uint8_t>& pongPacket) {
  CSPOT_LOG(debug, "Time synced with spotify servers");
  // Spotify's timestamp is in seconds since unix time - convert to millis.
  uint64_t remoteTimestamp =
      ((uint64_t)ntohl(extract<uint32_t>(pongPacket, 0))) * 1000;
  this->timestampDiff = remoteTimestamp - getCurrentTimestamp();
}

unsigned long long TimeProvider::getSyncedTimestamp() {
  return getCurrentTimestamp() + this->timestampDiff;
}
