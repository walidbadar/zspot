/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MAX_VOLUME 65536

// variable weakly set in ZeroconfAuthentificator.cpp
extern char deviceId[];

namespace cspot {
// Hardcoded information sent to spotify servers
const char* const informationString = "cspot-player";
const char* const brandName = "cspot";
const char* const versionString = "cspot-1.1";
const char* const protocolVersion = "2.7.1";
const char* const defaultDeviceName = "CSpot";
const char* const swVersion = "1.0.0";

}  // namespace cspot