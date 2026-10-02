/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/AccessKeyFetcher.h"

#include <cstring>           // for strrchr
#include <initializer_list>  // for initializer_list
#include <map>               // for operator!=, operator==
#include <type_traits>       // for remove_extent_t
#include <vector>            // for vector

#include "core/Context.h"
#include "port/http.h"
#include "port/log.h"
#include "core/MercurySession.h"    // for MercurySession, MercurySession::Res...
#include "port/protobuf.h"
#include "port/protobuf.h"
#include "core/Packet.h"            // for cspot
#include "core/TimeProvider.h"      // for TimeProvider
#include "core/Utils.h"             // for string_format

#include "protobuf/login5.pb.h"  // for LoginRequest

CSPOT_LOG_MODULE_DECLARE();

using namespace cspot;

static std::string CLIENT_ID =
    "65b708073fc0480ea92a077233ca87bd";  // Spotify web client's client id

static std::string SCOPES =
    "streaming,user-library-read,user-library-modify,user-top-read,user-read-"
    "recently-played";  // Required access scopes

AccessKeyFetcher::AccessKeyFetcher(std::shared_ptr<cspot::Context> ctx)
    : ctx(ctx) {}

bool AccessKeyFetcher::isExpired() {
  if (accessKey.empty()) {
    return true;
  }

  if (ctx->timeProvider->getSyncedTimestamp() > expiresAt) {
    return true;
  }

  return false;
}

std::string AccessKeyFetcher::getAccessKey() {
  if (!isExpired()) {
    return accessKey;
  }

  updateAccessKey();

  return accessKey;
}

void AccessKeyFetcher::updateAccessKey() {
  if (keyPending) {
    // Already pending refresh request
    return;
  }

  keyPending = true;

  // Prepare a protobuf login request
  static LoginRequest loginRequest = LoginRequest_init_zero;
  static LoginResponse loginResponse = LoginResponse_init_zero;

  // Assign necessary request fields
  loginRequest.client_info.client_id.funcs.encode = &cspot::nanopb::encodeString;
  loginRequest.client_info.client_id.arg = &CLIENT_ID;

  loginRequest.client_info.device_id.funcs.encode = &cspot::nanopb::encodeString;
  loginRequest.client_info.device_id.arg = &ctx->config.deviceId;

  loginRequest.login_method.stored_credential.username.funcs.encode =
      &cspot::nanopb::encodeString;
  loginRequest.login_method.stored_credential.username.arg =
      &ctx->config.username;

  // Set login method to stored credential
  loginRequest.which_login_method = LoginRequest_stored_credential_tag;
  loginRequest.login_method.stored_credential.data.funcs.encode =
      &cspot::nanopb::encodeVector;
  loginRequest.login_method.stored_credential.data.arg = &ctx->config.authData;

  // Max retry of 3, can receive different hash cat types
  int retryCount = 3;
  bool success = false;

  do {
    auto encodedRequest = pbEncode(LoginRequest_fields, &loginRequest);
    CSPOT_LOG(info, "Access token expired, fetching new one... %d",
              encodedRequest.size());

    // Perform a login5 request, containing the encoded protobuf data
    auto response = cspot::HttpConnection::fetch(
        "POST", "https://login5.spotify.com/v3/login", {}, encodedRequest,
        "application/x-protobuf");

    auto responseBytes = response.body;

    // Deserialize the response
    pbDecode(loginResponse, LoginResponse_fields, responseBytes);

    if (loginResponse.which_response == LoginResponse_ok_tag) {
      // Successfully received an auth token
      CSPOT_LOG(info, "Access token sucessfully fetched");
      success = true;

      accessKey = std::string(loginResponse.response.ok.access_token);

      // Expire in ~30 minutes
      int expiresIn = 3600 / 2;

      if (loginResponse.response.ok.has_access_token_expires_in) {
        expiresIn = loginResponse.response.ok.access_token_expires_in / 2;
      }

      this->expiresAt =
          ctx->timeProvider->getSyncedTimestamp() + (expiresIn * 1000);
    } else {
      CSPOT_LOG(error, "Failed to fetch access token");
    }

    // Free up allocated memory for response
    pb_release(LoginResponse_fields, &loginResponse);

    retryCount--;
  } while (retryCount >= 0 && !success);

  keyPending = false;
}
