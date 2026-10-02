/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "core/LoginBlob.h"
#include "core/MercurySession.h"
#include "core/TimeProvider.h"
#include "port/crypto.h"
#include "port/json.h"
#include "protobuf/authentication.pb.h"  // for AuthenticationType_AUTHE...
#include "protobuf/metadata.pb.h"

namespace cspot {
struct Context {
  struct ConfigState {
    // Setup default bitrate to 160
    AudioFormat audioFormat = AudioFormat::AudioFormat_OGG_VORBIS_160;
    std::string deviceId;
    std::string deviceName;
    std::vector<uint8_t> authData;
    int volume;

    std::string username;
    std::string countryCode;
  };

  ConfigState config;

  std::shared_ptr<TimeProvider> timeProvider;
  std::shared_ptr<cspot::MercurySession> session;

  std::string getCredentialsJson() {
    json::Credentials credentials;
    credentials.auth_data = Crypto::base64Encode(config.authData);
    credentials.auth_type =
        AuthenticationType_AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS;
    credentials.username = config.username;
    return json::encodeCredentials(credentials);
  }

  static std::shared_ptr<Context> createFromBlob(
      std::shared_ptr<LoginBlob> blob) {
    auto ctx = std::make_shared<Context>();
    ctx->timeProvider = std::make_shared<TimeProvider>();

    ctx->session = std::make_shared<MercurySession>(ctx->timeProvider);
    ctx->config.deviceId = blob->getDeviceId();
    ctx->config.deviceName = blob->getDeviceName();
    ctx->config.authData = blob->authData;
    ctx->config.volume = 0;
    ctx->config.username = blob->getUserName();

    return ctx;
  }
};
}  // namespace cspot
