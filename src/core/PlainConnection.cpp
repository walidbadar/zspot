/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/PlainConnection.h"

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "core/Packet.h"
#include "core/Utils.h"
#include "port/log.h"

CSPOT_LOG_MODULE_DECLARE();

using namespace cspot;

PlainConnection::PlainConnection() {
  this->apSock = -1;
}

PlainConnection::~PlainConnection() {
  this->close();
}

void PlainConnection::connect(const std::string& apAddress) {
  std::string hostname = apAddress.substr(0, apAddress.find(":"));
  std::string portStr = apAddress.substr(apAddress.find(":") + 1);

  struct zsock_addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct zsock_addrinfo* results = nullptr;
  if (zsock_getaddrinfo(hostname.c_str(), portStr.c_str(), &hints, &results) !=
          0 ||
      results == nullptr) {
    CSPOT_LOG(error, "getaddrinfo failed for %s", hostname.c_str());
    throw std::runtime_error("Can't resolve spotify access point");
  }

  for (struct zsock_addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
    if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
      continue;
    }

    this->apSock = zsock_socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (this->apSock < 0) {
      continue;
    }

    if (zsock_connect(this->apSock, ai->ai_addr, ai->ai_addrlen) == 0) {
      struct timeval tv;
      tv.tv_sec = 3;
      tv.tv_usec = 0;
      zsock_setsockopt(this->apSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
      zsock_setsockopt(this->apSock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

      int flag = 1;
      zsock_setsockopt(this->apSock, IPPROTO_TCP, TCP_NODELAY, &flag,
                       sizeof(flag));
      break;
    }

    zsock_close(this->apSock);
    this->apSock = -1;
  }
  zsock_freeaddrinfo(results);

  if (this->apSock < 0) {
    throw std::runtime_error("Can't connect to spotify servers");
  }

  CSPOT_LOG(debug, "Connected to spotify server");
}

std::vector<uint8_t> PlainConnection::recvPacket() {
  // Read packet size
  std::vector<uint8_t> packetBuffer(4);
  readBlock(packetBuffer.data(), 4);
  uint32_t packetSize = ntohl(extract<uint32_t>(packetBuffer, 0));

  packetBuffer.resize(packetSize, 0);

  // Read actual data
  readBlock(packetBuffer.data() + 4, packetSize - 4);

  return packetBuffer;
}

std::vector<uint8_t> PlainConnection::sendPrefixPacket(
    const std::vector<uint8_t>& prefix, const std::vector<uint8_t>& data) {
  // Calculate full packet length
  uint32_t actualSize = prefix.size() + data.size() + sizeof(uint32_t);

  // Packet structure [PREFIX] + [SIZE] +  [DATA]
  auto sizeRaw = pack<uint32_t>(htonl(actualSize));
  sizeRaw.insert(sizeRaw.begin(), prefix.begin(), prefix.end());
  sizeRaw.insert(sizeRaw.end(), data.begin(), data.end());

  // Actually write it to the server
  writeBlock(sizeRaw);

  return sizeRaw;
}

void PlainConnection::readBlock(const uint8_t* dst, size_t size) {
  size_t idx = 0;
  int retries = 0;

  while (idx < size) {
    ssize_t n = zsock_recv(this->apSock, const_cast<uint8_t*>(dst) + idx,
                           size - idx, 0);
    if (n > 0) {
      idx += n;
      retries = 0;
      continue;
    }

    if (n == 0) {
      throw std::runtime_error("Connection closed by peer");
    }

    switch (errno) {
      case EAGAIN:
      case ETIMEDOUT:
        if (timeoutHandler()) {
          CSPOT_LOG(error, "Connection lost, will need to reconnect...");
          throw std::runtime_error("Reconnection required");
        }
        break;
      case EINTR:
        break;
      default:
        if (retries++ > 4) {
          throw std::runtime_error("Error in read");
        }
        break;
    }
  }
}

size_t PlainConnection::writeBlock(const std::vector<uint8_t>& data) {
  size_t idx = 0;
  int retries = 0;

  while (idx < data.size()) {
    size_t chunk = data.size() - idx < 64 ? data.size() - idx : 64;
    ssize_t n = zsock_send(this->apSock, data.data() + idx, chunk, 0);
    if (n > 0) {
      idx += n;
      retries = 0;
      continue;
    }

    switch (errno) {
      case EAGAIN:
      case ETIMEDOUT:
        if (timeoutHandler()) {
          throw std::runtime_error("Reconnection required");
        }
        break;
      case EINTR:
        break;
      default:
        if (retries++ > 4) {
          throw std::runtime_error("Error in write");
        }
        break;
    }
  }

  return data.size();
}

void PlainConnection::close() {
  if (this->apSock < 0) {
    return;
  }

  CSPOT_LOG(info, "Closing socket...");
  zsock_shutdown(this->apSock, ZSOCK_SHUT_RDWR);
  zsock_close(this->apSock);
  this->apSock = -1;
}
