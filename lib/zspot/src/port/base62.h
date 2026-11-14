/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Base62 as Spotify writes its ids in URIs: digits, then lower-case, then
 * upper-case letters.
 */

#ifndef ZSPOT_PORT_BASE62_H_
#define ZSPOT_PORT_BASE62_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Digits of the id of a track, an episode or a playlist (16 bytes) */
#define ZSPOT_BASE62_ID_DIGITS 22

/**
 * Writes the big-endian number @p data as @p digits base62 digits, padded
 * with leading zeros, followed by a NUL. A number that needs more digits
 * loses its upper ones.
 *
 * @param out  @p digits + 1 bytes
 */
void zspot_base62_encode(const uint8_t *data, size_t len, char *out, size_t digits);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_PORT_BASE62_H_ */
