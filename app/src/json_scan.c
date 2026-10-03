/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "json_scan.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_space(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
		p++;
	}
	return p;
}

/* Returns the character after the closing quote, or NULL. */
static const char *skip_string(const char *p)
{
	for (p++; *p != '\0'; p++) {
		if (*p == '\\' && p[1] != '\0') {
			p++;
		} else if (*p == '"') {
			return p + 1;
		}
	}
	return NULL;
}

/* Returns the character after the value starting at @p p, or NULL. */
static const char *skip_value(const char *p)
{
	int depth = 0;

	do {
		p = skip_space(p);
		switch (*p) {
		case '\0':
			return NULL;
		case '"':
			p = skip_string(p);
			if (p == NULL) {
				return NULL;
			}
			break;
		case '{':
		case '[':
			depth++;
			p++;
			break;
		case '}':
		case ']':
			depth--;
			p++;
			break;
		case ',':
		case ':':
			if (depth == 0) {
				return p; /* empty value: malformed, stop here */
			}
			p++;
			break;
		default: /* number, true, false, null */
			while (*p != '\0' && strchr(",:]} \t\r\n", *p) == NULL) {
				p++;
			}
			break;
		}
	} while (depth > 0);

	return p;
}

const char *json_member(const char *object, const char *key)
{
	size_t key_len = strlen(key);
	const char *p;

	if (object == NULL || *object != '{') {
		return NULL;
	}

	p = skip_space(object + 1);
	while (*p == '"') {
		const char *name = p + 1;
		const char *value;

		p = skip_string(p);
		if (p == NULL) {
			return NULL;
		}
		value = skip_space(p);
		if (*value != ':') {
			return NULL;
		}
		value = skip_space(value + 1);

		/* Keys of the Web API contain no escapes, so compare them raw. */
		if ((size_t)(p - 1 - name) == key_len && memcmp(name, key, key_len) == 0) {
			return value;
		}

		p = skip_value(value);
		if (p == NULL) {
			return NULL;
		}
		p = skip_space(p);
		if (*p != ',') {
			return NULL;
		}
		p = skip_space(p + 1);
	}
	return NULL;
}

const char *json_first(const char *array)
{
	const char *p;

	if (array == NULL || *array != '[') {
		return NULL;
	}
	p = skip_space(array + 1);
	return *p == ']' || *p == '\0' ? NULL : p;
}

const char *json_next(const char *element)
{
	const char *p;

	if (element == NULL) {
		return NULL;
	}
	p = skip_value(element);
	if (p == NULL) {
		return NULL;
	}
	p = skip_space(p);
	return *p == ',' ? skip_space(p + 1) : NULL;
}

static size_t put_utf8(uint32_t code, char *out)
{
	if (code < 0x80) {
		out[0] = code;
		return 1;
	}
	if (code < 0x800) {
		out[0] = 0xC0 | (code >> 6);
		out[1] = 0x80 | (code & 0x3F);
		return 2;
	}
	if (code < 0x10000) {
		out[0] = 0xE0 | (code >> 12);
		out[1] = 0x80 | ((code >> 6) & 0x3F);
		out[2] = 0x80 | (code & 0x3F);
		return 3;
	}
	out[0] = 0xF0 | (code >> 18);
	out[1] = 0x80 | ((code >> 12) & 0x3F);
	out[2] = 0x80 | ((code >> 6) & 0x3F);
	out[3] = 0x80 | (code & 0x3F);
	return 4;
}

/* Reads the four hex digits of a \u escape; false when they are not there. */
static bool read_hex4(const char *p, uint32_t *code)
{
	*code = 0;
	for (int i = 0; i < 4; i++) {
		if (!isxdigit((unsigned char)p[i])) {
			return false;
		}
		*code = *code * 16 + (isdigit((unsigned char)p[i])
					      ? p[i] - '0'
					      : tolower((unsigned char)p[i]) - 'a' + 10);
	}
	return true;
}

bool json_string(const char *value, char *out, size_t size)
{
	size_t len = 0;
	const char *p;

	out[0] = '\0';
	if (value == NULL || *value != '"') {
		return false;
	}

	for (p = value + 1; *p != '\0' && *p != '"';) {
		char utf8[4];
		size_t n = 1;

		if (*p != '\\') {
			/* Copy a whole UTF-8 sequence so truncation cannot split it. */
			utf8[0] = *p++;
			while (((uint8_t)*p & 0xC0) == 0x80 && n < sizeof(utf8)) {
				utf8[n++] = *p++;
			}
		} else {
			uint32_t code;
			uint32_t low;

			p++;
			switch (*p) {
			case 'n':
				utf8[0] = '\n';
				break;
			case 't':
				utf8[0] = '\t';
				break;
			case 'r':
				utf8[0] = '\r';
				break;
			case 'b':
			case 'f':
				utf8[0] = ' ';
				break;
			case 'u':
				if (!read_hex4(p + 1, &code)) {
					return len > 0;
				}
				p += 4;
				/* Surrogate pair for characters beyond the BMP */
				if (code >= 0xD800 && code < 0xDC00 && p[1] == '\\' &&
				    p[2] == 'u' && read_hex4(p + 3, &low) && low >= 0xDC00 &&
				    low < 0xE000) {
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
					p += 6;
				}
				n = put_utf8(code, utf8);
				break;
			case '\0':
				return len > 0;
			default: /* quote, backslash, slash */
				utf8[0] = *p;
				break;
			}
			p++;
		}

		if (len + n >= size) {
			break;
		}
		memcpy(&out[len], utf8, n);
		len += n;
		out[len] = '\0';
	}
	return true;
}

uint32_t json_uint(const char *value, uint32_t fallback)
{
	if (value == NULL || !isdigit((unsigned char)*value)) {
		return fallback;
	}
	return strtoul(value, NULL, 10);
}
