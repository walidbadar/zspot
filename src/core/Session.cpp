/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/Session.h"

#include <limits.h>     // for CHAR_BIT
#include <cstdint>      // for uint8_t
#include <functional>   // for __base
#include <memory>       // for shared_ptr, unique_ptr, make_unique
#include <type_traits>  // for remove_extent_t
#include <utility>      // for move

#include "core/ApResolve.h"          // for ApResolve, cspot
#include "core/AuthChallenges.h"     // for AuthChallenges
#include "port/log.h"
#include "core/LoginBlob.h"          // for LoginBlob
#include "core/Packet.h"             // for Packet
#include "core/PlainConnection.h"    // for PlainConnection, timeoutCallback
#include "core/ShannonConnection.h"  // for ShannonConnection

#include "port/protobuf.h"
#include "pb_decode.h"
#include "protobuf/authentication.pb.h"

ZSPOT_LOG_MODULE_DECLARE();


using namespace cspot;

Session::Session() {
  this->challenges = std::make_unique<cspot::AuthChallenges>();
}

Session::~Session() {}

void Session::connect(std::unique_ptr<cspot::PlainConnection> connection) {
  this->conn = std::move(connection);
  conn->timeoutHandler = [this]() {
    return this->triggerTimeout();
  };
  auto helloPacket = this->conn->sendPrefixPacket(
      {0x00, 0x04}, this->challenges->prepareClientHello());
  auto apResponse = this->conn->recvPacket();
  CSPOT_LOG(info, "Received APHello response");

  auto solvedHello = this->challenges->solveApHello(helloPacket, apResponse);

  conn->sendPrefixPacket({}, solvedHello);
  CSPOT_LOG(debug, "Received shannon keys");

  // Generates the public and priv key
  this->shanConn = std::make_shared<ShannonConnection>();

  // Init shanno-encrypted connection
  this->shanConn->wrapConnection(this->conn, challenges->shanSendKey,
                                 challenges->shanRecvKey);
}

void Session::connectWithRandomAp() {
  auto apResolver = std::make_unique<ApResolve>("");
  auto conn = std::make_unique<cspot::PlainConnection>();
  conn->timeoutHandler = [this]() {
    return this->triggerTimeout();
  };

  auto apAddr = apResolver->fetchFirstApAddress();

  CSPOT_LOG(debug, "Connecting with AP <%s>", apAddr.c_str());
  conn->connect(apAddr);

  this->connect(std::move(conn));
}

std::vector<uint8_t> Session::authenticate(std::shared_ptr<LoginBlob> blob) {
  // save auth blob for reconnection purposes
  authBlob = blob;
  // prepare authentication request proto
  auto data = challenges->prepareAuthPacket(blob->authData, blob->authType,
                                            deviceId, blob->username);

  // Send login request
  this->shanConn->sendPacket(LOGIN_REQUEST_COMMAND, data);

  auto packet = this->shanConn->recvPacket();
  switch (packet.command) {
    case AUTH_SUCCESSFUL_COMMAND: {
      APWelcome welcome;
      CSPOT_LOG(debug, "Authorization successful");
      pbDecode(welcome, APWelcome_fields, packet.data);
      return std::vector<uint8_t>(welcome.reusable_auth_credentials.bytes,
                                  welcome.reusable_auth_credentials.bytes +
                                      welcome.reusable_auth_credentials.size);
      break;
    }
    case AUTH_DECLINED_COMMAND: {
      CSPOT_LOG(error, "Authorization declined");
      break;
    }
    default:
      CSPOT_LOG(error, "Unknown auth fail code %d", packet.command);
  }

  return std::vector<uint8_t>(0);
}

void Session::close() {
  this->conn->close();
}
