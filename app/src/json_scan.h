/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Minimal read-only JSON walker for picking a few fields out of large Web API
 * responses without building a tree. The document must be NUL terminated. A
 * value is referred to by a pointer to its first character; NULL means
 * "absent" and is accepted by every function, so lookups can be chained.
 */

#ifndef ZSPOT_SAMPLE_JSON_SCAN_H_
#define ZSPOT_SAMPLE_JSON_SCAN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Value of @p key in the object @p object, or NULL. */
const char *json_member(const char *object, const char *key);

/** First element of the array @p array, or NULL when it is empty. */
const char *json_first(const char *array);

/** Element following @p element in its array, or NULL after the last one. */
const char *json_next(const char *element);

/**
 * Decodes the string @p value into @p out, truncating at a character
 * boundary. Returns false (and an empty @p out) when it is not a string.
 */
bool json_string(const char *value, char *out, size_t size);

/** Non-negative integer @p value, or @p fallback when it is not a number. */
uint32_t json_uint(const char *value, uint32_t fallback);

#endif /* ZSPOT_SAMPLE_JSON_SCAN_H_ */
