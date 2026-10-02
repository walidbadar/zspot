/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <string>  // for string

namespace cspot {
class ApResolve {
 public:
  ApResolve(std::string apOverride);

  /**
   * @brief Connects to spotify's servers and returns first valid ap address
   * @returns std::string Address in form of url:port
   */
  std::string fetchFirstApAddress();

 private:
  std::string apOverride;
};
}  // namespace cspot
