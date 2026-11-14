/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <string>  // for string
#include <vector>  // for vector

namespace cspot {
class ApResolve {
 public:
  ApResolve(std::string apOverride);

  /**
   * @brief Asks spotify's servers for access points
   * @returns the addresses in the form of url:port, in the order to try them
   */
  std::vector<std::string> fetchApAddresses();

 private:
  std::string apOverride;
};
}  // namespace cspot
