/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/MercurySession.h"

#include <string.h>     // for memcpy
#include <memory>       // for shared_ptr
#include <mutex>        // for scoped_lock
#include <stdexcept>    // for runtime_error
#include <type_traits>  // for remove_extent_t, __underlying_type_impl<>:...
#include <utility>      // for pair
#include <zephyr/net/net_ip.h>
#include "port/thread.h"
#include "port/sync.h"
#include "port/log.h"
#include "port/protobuf.h"
#include "core/PlainConnection.h"    // for PlainConnection
#include "core/ShannonConnection.h"  // for ShannonConnection
#include "core/TimeProvider.h"       // for TimeProvider
#include "core/Utils.h"              // for extract, pack, hton64

ZSPOT_LOG_MODULE_DECLARE();

using namespace cspot;

MercurySession::MercurySession(std::shared_ptr<TimeProvider> timeProvider)
    : zspot::Task("zspot_mercury", CONFIG_ZSPOT_MERCURY_STACK_SIZE, 3) {
  this->timeProvider = timeProvider;
}

MercurySession::~MercurySession() {
  std::scoped_lock lock(this->isRunningMutex);
}

void MercurySession::runTask() {
  isRunning = true;
  std::scoped_lock lock(this->isRunningMutex);

  this->executeEstabilishedCallback = true;
  this->lastPingTimestamp = timeProvider->getSyncedTimestamp();
  while (isRunning) {
    cspot::Packet packet = {};
    try {
      packet = shanConn->recvPacket();
      CSPOT_LOG(info, "Received packet, command: %d", packet.command);

      if (static_cast<RequestType>(packet.command) == RequestType::PING) {
        timeProvider->syncWithPingPacket(packet.data);

        this->lastPingTimestamp = timeProvider->getSyncedTimestamp();
        this->shanConn->sendPacket(0x49, packet.data);
      } else {
        this->packetQueue.push(packet);
      }
    } catch (const std::runtime_error& e) {
      CSPOT_LOG(error, "Error while receiving packet: %s", e.what());

      if (!isRunning) {
        failAllPending();
        return;
      }

      reconnect();
      continue;
    }
  }
}

void MercurySession::reconnect() {
  {
    // Waits for a request that is being sent. From here on requests fail
    // without touching the connection
    std::scoped_lock lock(this->requestMutex);
    isReconnecting = true;
    reconnectRequested = false;
  }
  failAllPending();

  int delayMs = RECONNECT_MIN_DELAY_MS;

  while (isRunning) {
    try {
      // The handshake reads go through triggerTimeout() as well
      lastPingTimestamp = timeProvider->getSyncedTimestamp();

      this->shanConn = nullptr;
      this->conn = nullptr;

      this->connectWithRandomAp();
      if (this->authenticate(this->authBlob).empty()) {
        throw std::runtime_error("Authentication failed");
      }

      CSPOT_LOG(info, "Reconnection successful");

      k_msleep(100);

      lastPingTimestamp = timeProvider->getSyncedTimestamp();
      {
        std::scoped_lock lock(this->requestMutex);
        isReconnecting = false;
      }

      this->executeEstabilishedCallback = true;
      return;
    } catch (const std::exception& e) {
      CSPOT_LOG(error, "Cannot reconnect (%s), will retry in %d ms", e.what(),
                delayMs);
    }

    // Sleep in slices so that disconnect() does not wait for the full delay
    for (int slept = 0; slept < delayMs && isRunning; slept += 100) {
      k_msleep(100);
    }
    delayMs = delayMs * 2 < RECONNECT_MAX_DELAY_MS ? delayMs * 2
                                                   : RECONNECT_MAX_DELAY_MS;
  }
}

void MercurySession::setConnectedHandler(
    ConnectionEstabilishedCallback callback) {
  this->connectionReadyCallback = callback;
}

bool MercurySession::triggerTimeout() {
  if (!isRunning)
    return true;

  if (reconnectRequested) {
    CSPOT_LOG(debug, "Reconnection required, a send failed");
    return true;
  }

  auto currentTimestamp = timeProvider->getSyncedTimestamp();

  if (currentTimestamp - this->lastPingTimestamp > static_cast<unsigned long long>(PING_TIMEOUT_MS)) {
    CSPOT_LOG(debug, "Reconnection required, no ping received");
    return true;
  }

  return false;
}

void MercurySession::unregister(uint64_t sequenceId) {
  std::scoped_lock lock(this->requestMutex);
  auto callback = this->callbacks.find(sequenceId);

  if (callback != this->callbacks.end()) {
    this->callbacks.erase(callback);
  }
}

void MercurySession::unregisterAudioKey(uint32_t sequenceId) {
  std::scoped_lock lock(this->requestMutex);
  auto callback = this->audioKeyCallbacks.find(sequenceId);

  if (callback != this->audioKeyCallbacks.end()) {
    this->audioKeyCallbacks.erase(callback);
  }
}

void MercurySession::disconnect() {
  CSPOT_LOG(info, "Disconnecting mercury session");
  this->isRunning = false;
  {
    // While reconnecting the receive thread owns the connection, it stops
    // on its own
    std::scoped_lock lock(this->requestMutex);
    if (!isReconnecting && conn != nullptr) {
      conn->close();
    }
  }
  std::scoped_lock lock(this->isRunningMutex);
}

std::string MercurySession::getCountryCode() {
  return this->countryCode;
}

void MercurySession::handlePacket() {
  Packet packet = {};

  this->packetQueue.wtpop(packet, 200);

  failUnsent();

  if (executeEstabilishedCallback && this->connectionReadyCallback != nullptr) {
    executeEstabilishedCallback = false;
    this->connectionReadyCallback();
  }

  switch (static_cast<RequestType>(packet.command)) {
    case RequestType::COUNTRY_CODE_RESPONSE: {
      this->countryCode = std::string();
      this->countryCode.resize(2);
      memcpy(this->countryCode.data(), packet.data.data(), 2);
      CSPOT_LOG(debug, "Received country code %s", this->countryCode.c_str());
      break;
    }
    case RequestType::AUDIO_KEY_FAILURE_RESPONSE:
    case RequestType::AUDIO_KEY_SUCCESS_RESPONSE: {
      // this->lastRequestTimestamp = -1;

      // First four bytes mark the sequence id
      auto seqId = ntohl(extract<uint32_t>(packet.data, 0));

      AudioKeyCallback callback = nullptr;
      {
        std::scoped_lock lock(this->requestMutex);
        auto it = this->audioKeyCallbacks.find(seqId);

        if (it != this->audioKeyCallbacks.end()) {
          callback = it->second;
        }
      }
      if (callback != nullptr) {
        auto success = static_cast<RequestType>(packet.command) ==
                       RequestType::AUDIO_KEY_SUCCESS_RESPONSE;
        callback(success, packet.data);
      }

      break;
    }
    case RequestType::SEND:
    case RequestType::SUB:
    case RequestType::UNSUB: {
      CSPOT_LOG(debug, "Received mercury packet");

      auto response = this->decodeResponse(packet.data);
      ResponseCallback callback = nullptr;
      {
        std::scoped_lock lock(this->requestMutex);
        auto it = this->callbacks.find(response.sequenceId);

        if (it != this->callbacks.end()) {
          callback = it->second;
          this->callbacks.erase(it);
        }
      }
      // Called unlocked: handlers issue requests of their own
      if (callback != nullptr) {
        callback(response);
      }
      break;
    }
    case RequestType::SUBRES: {
      auto response = decodeResponse(packet.data);

      auto uri = std::string(response.mercuryHeader.uri);
      ResponseCallback subscription = nullptr;
      {
        std::scoped_lock lock(this->requestMutex);
        auto it = this->subscriptions.find(uri);

        if (it != this->subscriptions.end()) {
          subscription = it->second;
        }
      }
      if (subscription != nullptr) {
        subscription(response);
      }
      break;
    }
    default:
      break;
  }
}

void MercurySession::failAllPending() {
  Response response = {};
  response.fail = true;

  // Take the tables over, the handlers run unlocked
  std::unordered_map<uint64_t, ResponseCallback> pendingCallbacks;
  std::unordered_map<std::string, ResponseCallback> pendingSubscriptions;
  std::unordered_map<uint32_t, AudioKeyCallback> pendingAudioKeys;
  {
    std::scoped_lock lock(this->requestMutex);
    pendingCallbacks.swap(this->callbacks);
    pendingSubscriptions.swap(this->subscriptions);
    pendingAudioKeys.swap(this->audioKeyCallbacks);
  }

  // Fail all callbacks
  for (auto& it : pendingCallbacks) {
    it.second(response);
  }

  // Fail all subscriptions
  for (auto& it : pendingSubscriptions) {
    it.second(response);
  }

  // Fail all audio key requests
  for (auto& it : pendingAudioKeys) {
    it.second(false, {});
  }
}

void MercurySession::failUnsent() {
  std::vector<ResponseCallback> failedRequests;
  std::vector<AudioKeyCallback> failedAudioKeys;
  {
    std::scoped_lock lock(this->requestMutex);

    for (auto id : this->unsentRequests) {
      auto it = this->callbacks.find(id);

      if (it != this->callbacks.end()) {
        failedRequests.push_back(it->second);
        this->callbacks.erase(it);
      }
    }
    this->unsentRequests.clear();

    for (auto id : this->unsentAudioKeys) {
      auto it = this->audioKeyCallbacks.find(id);

      if (it != this->audioKeyCallbacks.end()) {
        failedAudioKeys.push_back(it->second);
        this->audioKeyCallbacks.erase(it);
      }
    }
    this->unsentAudioKeys.clear();
  }

  Response response = {};
  response.fail = true;

  for (auto& callback : failedRequests) {
    callback(response);
  }

  for (auto& callback : failedAudioKeys) {
    callback(false, {});
  }
}

MercurySession::Response MercurySession::decodeResponse(
    const std::vector<uint8_t>& data) {
  Response response = {};
  response.parts = {};

  [[maybe_unused]] auto sequenceLength = ntohs(extract<uint16_t>(data, 0));
  response.sequenceId = hton64(extract<uint64_t>(data, 2));

  [[maybe_unused]] auto partsNumber = ntohs(extract<uint16_t>(data, 11));

  auto headerSize = ntohs(extract<uint16_t>(data, 13));
  auto headerBytes =
      std::vector<uint8_t>(data.begin() + 15, data.begin() + 15 + headerSize);

  auto pos = 15 + headerSize;
  while (static_cast<size_t>(pos) < data.size()) {
    auto partSize = ntohs(extract<uint16_t>(data, pos));

    response.parts.push_back(std::vector<uint8_t>(
        data.begin() + pos + 2, data.begin() + pos + 2 + partSize));
    pos += 2 + partSize;
  }

  pbDecode(response.mercuryHeader, Header_fields, headerBytes);
  response.fail = false;

  return response;
}

uint64_t MercurySession::executeSubscription(RequestType method,
                                             const std::string& uri,
                                             ResponseCallback callback,
                                             ResponseCallback subscription,
                                             DataParts& payload) {
  std::scoped_lock lock(this->requestMutex);

  CSPOT_LOG(debug, "Executing Mercury Request, type %s",
            RequestTypeMap[method].c_str());

  // Encode header
  pbPutString(uri, tempMercuryHeader.uri);
  pbPutString(RequestTypeMap[method], tempMercuryHeader.method);

  tempMercuryHeader.has_method = true;
  tempMercuryHeader.has_uri = true;

  // GET and SEND are actually the same. Therefore the override
  // The difference between them is only in header's method
  if (method == RequestType::GET) {
    method = RequestType::SEND;
  }

  // Subscribed to again once the connection is back, see setConnectedHandler()
  if (method == RequestType::SUB && !isReconnecting) {
    this->subscriptions.insert({uri, subscription});
  }

  auto headerBytes = pbEncode(Header_fields, &tempMercuryHeader);

  this->callbacks.insert({sequenceId, callback});

  // Structure: [Sequence size] [SequenceId] [0x1] [Payloads number]
  // [Header size] [Header] [Payloads (size + data)]

  // Pack sequenceId
  auto sequenceIdBytes = pack<uint64_t>(hton64(this->sequenceId));
  auto sequenceSizeBytes = pack<uint16_t>(htons(sequenceIdBytes.size()));

  sequenceIdBytes.insert(sequenceIdBytes.begin(), sequenceSizeBytes.begin(),
                         sequenceSizeBytes.end());
  sequenceIdBytes.push_back(0x01);

  auto payloadNum = pack<uint16_t>(htons(payload.size() + 1));
  sequenceIdBytes.insert(sequenceIdBytes.end(), payloadNum.begin(),
                         payloadNum.end());

  auto headerSizePayload = pack<uint16_t>(htons(headerBytes.size()));
  sequenceIdBytes.insert(sequenceIdBytes.end(), headerSizePayload.begin(),
                         headerSizePayload.end());
  sequenceIdBytes.insert(sequenceIdBytes.end(), headerBytes.begin(),
                         headerBytes.end());

  // Encode all the payload parts
  for (size_t x = 0; x < payload.size(); x++) {
    headerSizePayload = pack<uint16_t>(htons(payload[x].size()));
    sequenceIdBytes.insert(sequenceIdBytes.end(), headerSizePayload.begin(),
                           headerSizePayload.end());
    sequenceIdBytes.insert(sequenceIdBytes.end(), payload[x].begin(),
                           payload[x].end());
  }

  // Bump sequence id
  this->sequenceId += 1;

  if (isReconnecting) {
    this->unsentRequests.push_back(this->sequenceId - 1);
    return this->sequenceId - 1;
  }

  try {
    this->shanConn->sendPacket(
        static_cast<std::underlying_type<RequestType>::type>(method),
        sequenceIdBytes);
  } catch (const std::exception& e) {
    // A partly written packet leaves the cipher out of step with the server,
    // the connection cannot be used any further
    CSPOT_LOG(error, "Cannot send mercury request: %s", e.what());
    this->unsentRequests.push_back(this->sequenceId - 1);
    reconnectRequested = true;
  }

  return this->sequenceId - 1;
}

uint32_t MercurySession::requestAudioKey(const std::vector<uint8_t>& trackId,
                                         const std::vector<uint8_t>& fileId,
                                         AudioKeyCallback audioCallback) {
  std::scoped_lock lock(this->requestMutex);
  auto buffer = fileId;

  // Store callback
  this->audioKeyCallbacks.insert({this->audioKeySequence, audioCallback});

  // Structure: [FILEID] [TRACKID] [4 BYTES SEQUENCE ID] [0x00, 0x00]
  buffer.insert(buffer.end(), trackId.begin(), trackId.end());
  auto audioKeySequenceBuffer = pack<uint32_t>(htonl(this->audioKeySequence));
  buffer.insert(buffer.end(), audioKeySequenceBuffer.begin(),
                audioKeySequenceBuffer.end());
  auto suffix = std::vector<uint8_t>({0x00, 0x00});
  buffer.insert(buffer.end(), suffix.begin(), suffix.end());

  // Bump audio key sequence
  this->audioKeySequence += 1;

  if (isReconnecting) {
    this->unsentAudioKeys.push_back(this->audioKeySequence - 1);
    return this->audioKeySequence - 1;
  }

  try {
    this->shanConn->sendPacket(
        static_cast<uint8_t>(RequestType::AUDIO_KEY_REQUEST_COMMAND), buffer);
  } catch (const std::exception& e) {
    CSPOT_LOG(error, "Cannot send audio key request: %s", e.what());
    this->unsentAudioKeys.push_back(this->audioKeySequence - 1);
    reconnectRequested = true;
  }
  return audioKeySequence - 1;
}
