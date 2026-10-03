/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef SHANNONCONNECTION_H
#define SHANNONCONNECTION_H

#include "port/sync.h"
#include <cstdint>  // for uint8_t, uint32_t
#include <memory>   // for shared_ptr, unique_ptr
#include <mutex>    // for mutex
#include <vector>   // for vector

#include "core/Packet.h"  // for Packet

class Shannon;
namespace cspot {

class PlainConnection;
}  // namespace cspot

#define MAC_SIZE 4
namespace cspot {
class ShannonConnection {
 private:
  std::unique_ptr<Shannon> sendCipher;
  std::unique_ptr<Shannon> recvCipher;
  uint32_t sendNonce = 0;
  uint32_t recvNonce = 0;
  std::vector<uint8_t> cipherPacket(uint8_t cmd, std::vector<uint8_t>& data);
  zspot::Mutex writeMutex;
  zspot::Mutex readMutex;

 public:
  ShannonConnection();
  ~ShannonConnection();
  void wrapConnection(std::shared_ptr<PlainConnection> conn,
                      std::vector<uint8_t>& sendKey,
                      std::vector<uint8_t>& recvKey);
  void sendPacket(uint8_t cmd, std::vector<uint8_t>& data);
  std::shared_ptr<PlainConnection> conn;
  Packet recvPacket();
};
}  // namespace cspot

#endif
