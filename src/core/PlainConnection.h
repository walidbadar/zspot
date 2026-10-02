/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef PLAINCONNECTION_H
#define PLAINCONNECTION_H

#include <cstddef>     // for size_t
#include <cstdint>     // for uint8_t
#include <functional>  // for function
#include <string>      // for string
#include <vector>      // for vector

typedef std::function<bool()> timeoutCallback;

namespace cspot {
class PlainConnection {
 public:
  PlainConnection();
  ~PlainConnection();

  /**
   * @brief Connect to the given AP address
   *
   * @param apAddress The AP url to connect to
   */
  void connect(const std::string& apAddress);
  void close();

  timeoutCallback timeoutHandler;
  std::vector<uint8_t> sendPrefixPacket(const std::vector<uint8_t>& prefix,
                                        const std::vector<uint8_t>& data);
  std::vector<uint8_t> recvPacket();

  void readBlock(const uint8_t* dst, size_t size);
  size_t writeBlock(const std::vector<uint8_t>& data);

 private:
  int apSock;
};
}  // namespace cspot

#endif
