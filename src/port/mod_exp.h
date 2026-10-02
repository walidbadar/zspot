/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef CSPOT_PORT_MOD_EXP_H_
#define CSPOT_PORT_MOD_EXP_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cspot::detail
{

/**
 * @brief Computes base^exp mod modulus for big-endian byte strings.
 *
 * Uses Montgomery multiplication. The modulus must be odd and at most 1024
 * bits; the base must not be longer than the modulus.
 *
 * @return false on invalid input, true on success (result sized like modulus).
 */
bool mod_exp(const std::vector<uint8_t> &base, const std::vector<uint8_t> &exp,
	     const uint8_t *modulus, size_t modulus_len, std::vector<uint8_t> &result);

} /* namespace cspot::detail */

#endif /* CSPOT_PORT_MOD_EXP_H_ */
