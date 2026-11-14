/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/base62.h"

#include <string.h>

/* Longest number handled: ids are 16 bytes */
#define NUMBER_MAX 32

static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

void zspot_base62_encode(const uint8_t *data, size_t len, char *out, size_t digits)
{
	uint8_t number[NUMBER_MAX];

	if (len > sizeof(number)) {
		/* The upper bytes would not fit into the digits either */
		data += len - sizeof(number);
		len = sizeof(number);
	}
	memcpy(number, data, len);

	/* Long division by 62: the remainders are the digits, last one first */
	for (size_t digit = digits; digit-- > 0;) {
		unsigned int remainder = 0;

		for (size_t i = 0; i < len; i++) {
			unsigned int value = (remainder << 8) | number[i];

			number[i] = value / 62;
			remainder = value % 62;
		}
		out[digit] = alphabet[remainder];
	}
	out[digits] = '\0';
}
