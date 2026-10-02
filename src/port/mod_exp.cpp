/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Montgomery modular exponentiation (CIOS), 32-bit limbs.
 *
 * Spotify's access point handshake uses the 768-bit "First Oakley Group",
 * which the PSA Crypto API cannot express (it only knows the RFC 7919 FFDH
 * groups), so the exponentiation is done here.
 */

#include "port/mod_exp.h"

#include <cstring>

namespace cspot::detail
{

namespace
{

constexpr int MAX_LIMBS = 32; /* 1024 bits */

void from_bytes(uint32_t *out, int limbs, const uint8_t *bytes, size_t len)
{
	memset(out, 0, limbs * sizeof(uint32_t));
	for (size_t i = 0; i < len; i++) {
		const int limb = i / 4;

		if (limb >= limbs) {
			break;
		}
		out[limb] |= static_cast<uint32_t>(bytes[len - 1 - i]) << (8 * (i % 4));
	}
}

void to_bytes(const uint32_t *in, int limbs, uint8_t *bytes, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		const int limb = i / 4;

		bytes[len - 1 - i] =
			limb < limbs ? static_cast<uint8_t>(in[limb] >> (8 * (i % 4))) : 0;
	}
}

int compare(const uint32_t *a, const uint32_t *b, int limbs)
{
	for (int i = limbs - 1; i >= 0; i--) {
		if (a[i] != b[i]) {
			return a[i] > b[i] ? 1 : -1;
		}
	}
	return 0;
}

void subtract(uint32_t *r, const uint32_t *a, const uint32_t *b, int limbs)
{
	uint64_t borrow = 0;

	for (int i = 0; i < limbs; i++) {
		const uint64_t d = static_cast<uint64_t>(a[i]) - b[i] - borrow;

		r[i] = static_cast<uint32_t>(d);
		borrow = (d >> 32) & 1;
	}
}

/* r = a * b * R^-1 mod n, with R = 2^(32 * limbs). Requires a, b < n. */
void mont_mul(uint32_t *r, const uint32_t *a, const uint32_t *b, const uint32_t *n,
	      uint32_t n0inv, int limbs)
{
	uint32_t t[MAX_LIMBS + 2];

	memset(t, 0, sizeof(t));

	for (int i = 0; i < limbs; i++) {
		uint64_t carry = 0;
		uint64_t s;

		for (int j = 0; j < limbs; j++) {
			s = static_cast<uint64_t>(a[j]) * b[i] + t[j] + carry;
			t[j] = static_cast<uint32_t>(s);
			carry = s >> 32;
		}
		s = static_cast<uint64_t>(t[limbs]) + carry;
		t[limbs] = static_cast<uint32_t>(s);
		t[limbs + 1] = static_cast<uint32_t>(s >> 32);

		const uint32_t m = t[0] * n0inv;

		carry = (static_cast<uint64_t>(m) * n[0] + t[0]) >> 32;
		for (int j = 1; j < limbs; j++) {
			s = static_cast<uint64_t>(m) * n[j] + t[j] + carry;
			t[j - 1] = static_cast<uint32_t>(s);
			carry = s >> 32;
		}
		s = static_cast<uint64_t>(t[limbs]) + carry;
		t[limbs - 1] = static_cast<uint32_t>(s);
		t[limbs] = t[limbs + 1] + static_cast<uint32_t>(s >> 32);
		t[limbs + 1] = 0;
	}

	if (t[limbs] != 0 || compare(t, n, limbs) >= 0) {
		subtract(t, t, n, limbs);
	}
	memcpy(r, t, limbs * sizeof(uint32_t));
}

/* -n^-1 mod 2^32 for odd n (Newton iteration). */
uint32_t neg_inverse32(uint32_t n)
{
	uint32_t x = n; /* correct to 3 bits because n^2 == 1 (mod 8) */

	for (int i = 0; i < 5; i++) {
		x *= 2 - n * x;
	}
	return 0U - x;
}

/* R^2 mod n by repeated doubling of 1. */
void compute_r2(uint32_t *r2, const uint32_t *n, int limbs)
{
	uint32_t r[MAX_LIMBS];

	memset(r, 0, sizeof(r));
	r[0] = 1;
	for (int i = 0; i < 64 * limbs; i++) {
		uint32_t carry = 0;

		for (int j = 0; j < limbs; j++) {
			const uint32_t next = r[j] >> 31;

			r[j] = (r[j] << 1) | carry;
			carry = next;
		}
		if (carry != 0 || compare(r, n, limbs) >= 0) {
			subtract(r, r, n, limbs);
		}
	}
	memcpy(r2, r, limbs * sizeof(uint32_t));
}

} /* namespace */

bool mod_exp(const std::vector<uint8_t> &base, const std::vector<uint8_t> &exp,
	     const uint8_t *modulus, size_t modulus_len, std::vector<uint8_t> &result)
{
	const int limbs = static_cast<int>((modulus_len + 3) / 4);

	if (limbs == 0 || limbs > MAX_LIMBS || (modulus[modulus_len - 1] & 1) == 0 ||
	    base.size() > modulus_len) {
		return false;
	}

	uint32_t n[MAX_LIMBS], b[MAX_LIMBS], r2[MAX_LIMBS], acc[MAX_LIMBS], bm[MAX_LIMBS],
		one[MAX_LIMBS];

	from_bytes(n, limbs, modulus, modulus_len);
	from_bytes(b, limbs, base.data(), base.size());
	while (compare(b, n, limbs) >= 0) {
		subtract(b, b, n, limbs);
	}

	const uint32_t n0inv = neg_inverse32(n[0]);

	compute_r2(r2, n, limbs);

	memset(one, 0, sizeof(one));
	one[0] = 1;

	mont_mul(bm, b, r2, n, n0inv, limbs);   /* base in Montgomery form */
	mont_mul(acc, one, r2, n, n0inv, limbs); /* 1 in Montgomery form */

	bool started = false;

	for (uint8_t byte : exp) {
		for (int bit = 7; bit >= 0; bit--) {
			if (started) {
				mont_mul(acc, acc, acc, n, n0inv, limbs);
			}
			if ((byte >> bit) & 1) {
				mont_mul(acc, acc, bm, n, n0inv, limbs);
				started = true;
			}
		}
	}

	mont_mul(acc, acc, one, n, n0inv, limbs); /* back from Montgomery form */
	result.resize(modulus_len);
	to_bytes(acc, limbs, result.data(), modulus_len);
	return true;
}

} /* namespace cspot::detail */
