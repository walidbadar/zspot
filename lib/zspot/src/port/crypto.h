/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Cryptographic primitives used by the Spotify protocol.
 *
 * Implemented with the PSA Crypto API shipped with Zephyr (SHA-1, HMAC-SHA1,
 * AES-CTR, AES-ECB, CSPRNG), Zephyr's base64 helpers and a small Montgomery
 * modular exponentiation for the 768-bit access point handshake. Method names
 * follow the upstream cspot crypto interface.
 */

#ifndef ZSPOT_PORT_CRYPTO_H_
#define ZSPOT_PORT_CRYPTO_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define DH_KEY_SIZE 96

namespace zspot
{

class Crypto
{
public:
	Crypto();
	~Crypto();

	Crypto(const Crypto &) = delete;
	Crypto &operator=(const Crypto &) = delete;

	/* Base64 */
	static std::vector<uint8_t> base64Decode(const std::string &data);
	static std::string base64Encode(const std::vector<uint8_t> &data);

	/* SHA-1 (streaming) */
	void sha1Init();
	void sha1Update(const std::string &s);
	void sha1Update(const std::vector<uint8_t> &vec);
	std::string sha1Final();
	std::vector<uint8_t> sha1FinalBytes();

	/* HMAC-SHA1 */
	std::vector<uint8_t> sha1HMAC(const std::vector<uint8_t> &key,
				      const std::vector<uint8_t> &message);

	/* AES-128-CTR, in place */
	void aesCTRXcrypt(const std::vector<uint8_t> &key, std::vector<uint8_t> &iv,
			  uint8_t *data, size_t nbytes);

	/* AES-ECB decrypt (AES-192 for the zeroconf login blob), in place */
	void aesECBdecrypt(const std::vector<uint8_t> &key, std::vector<uint8_t> &data);

	/* Diffie-Hellman (768-bit "First Oakley Group", generator 2) */
	std::vector<uint8_t> publicKey;
	std::vector<uint8_t> privateKey;
	void dhInit();
	std::vector<uint8_t> dhCalculateShared(const std::vector<uint8_t> &remote_key);

	/* PBKDF2-HMAC-SHA1 */
	std::vector<uint8_t> pbkdf2HmacSha1(const std::vector<uint8_t> &password,
					    const std::vector<uint8_t> &salt, int iterations,
					    int digest_size);

	/* Cryptographically secure random bytes */
	std::vector<uint8_t> generateVectorWithRandomData(size_t length);

private:
	struct HashState;
	HashState *sha1_;
};

} /* namespace zspot */

#endif /* ZSPOT_PORT_CRYPTO_H_ */
