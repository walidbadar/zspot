/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/crypto.h"

#include <psa/crypto.h>
#include <zephyr/sys/base64.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "port/log.h"
#include "port/mod_exp.h"

ZSPOT_LOG_MODULE_DECLARE();

namespace zspot
{

namespace
{

constexpr size_t SHA1_SIZE = 20;
constexpr size_t AES_BLOCK = 16;

/* 768-bit "First Oakley Group" prime (RFC 2409, section 6.1). */
const unsigned char DH_PRIME[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc9, 0x0f, 0xda, 0xa2, 0x21, 0x68,
	0xc2, 0x34, 0xc4, 0xc6, 0x62, 0x8b, 0x80, 0xdc, 0x1c, 0xd1, 0x29, 0x02, 0x4e, 0x08,
	0x8a, 0x67, 0xcc, 0x74, 0x02, 0x0b, 0xbe, 0xa6, 0x3b, 0x13, 0x9b, 0x22, 0x51, 0x4a,
	0x08, 0x79, 0x8e, 0x34, 0x04, 0xdd, 0xef, 0x95, 0x19, 0xb3, 0xcd, 0x3a, 0x43, 0x1b,
	0x30, 0x2b, 0x0a, 0x6d, 0xf2, 0x5f, 0x14, 0x37, 0x4f, 0xe1, 0x35, 0x6d, 0x6d, 0x51,
	0xc2, 0x45, 0xe4, 0x85, 0xb5, 0x76, 0x62, 0x5e, 0x7e, 0xc6, 0xf4, 0x4c, 0x42, 0xe9,
	0xa6, 0x3a, 0x36, 0x20, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const std::vector<uint8_t> DH_GENERATOR = {2};

void ensure_psa()
{
	static bool initialised;

	if (!initialised) {
		const psa_status_t status = psa_crypto_init();

		if (status != PSA_SUCCESS) {
			LOG_ERR("psa_crypto_init failed: %d", static_cast<int>(status));
			throw std::runtime_error("PSA crypto init failed");
		}
		initialised = true;
	}
}

psa_key_id_t import_key(psa_key_type_t type, psa_algorithm_t alg, psa_key_usage_t usage,
			const uint8_t *key, size_t key_len)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t id = PSA_KEY_ID_NULL;

	psa_set_key_type(&attributes, type);
	psa_set_key_algorithm(&attributes, alg);
	psa_set_key_usage_flags(&attributes, usage);

	const psa_status_t status = psa_import_key(&attributes, key, key_len, &id);

	psa_reset_key_attributes(&attributes);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_import_key failed: %d", static_cast<int>(status));
		throw std::runtime_error("PSA key import failed");
	}
	return id;
}

class HmacSha1Key
{
public:
	HmacSha1Key(const uint8_t *key, size_t len)
		: id_(import_key(PSA_KEY_TYPE_HMAC, PSA_ALG_HMAC(PSA_ALG_SHA_1),
				 PSA_KEY_USAGE_SIGN_MESSAGE, key, len))
	{
	}
	~HmacSha1Key()
	{
		psa_destroy_key(id_);
	}

	void compute(const uint8_t *msg, size_t len, uint8_t out[SHA1_SIZE]) const
	{
		size_t out_len = 0;
		const psa_status_t status = psa_mac_compute(id_, PSA_ALG_HMAC(PSA_ALG_SHA_1),
							    msg, len, out, SHA1_SIZE, &out_len);

		if (status != PSA_SUCCESS || out_len != SHA1_SIZE) {
			LOG_ERR("psa_mac_compute failed: %d", static_cast<int>(status));
			throw std::runtime_error("HMAC failed");
		}
	}

private:
	psa_key_id_t id_;
};

} /* namespace */

struct Crypto::HashState {
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	bool active = false;
};

Crypto::Crypto() : sha1_(new HashState())
{
	ensure_psa();
}

Crypto::~Crypto()
{
	if (sha1_->active) {
		psa_hash_abort(&sha1_->op);
	}
	delete sha1_;
}

/* Base64 ------------------------------------------------------------------ */

std::vector<uint8_t> Crypto::base64Decode(const std::string &data)
{
	size_t needed = 0;
	size_t written = 0;

	base64_decode(nullptr, 0, &needed, reinterpret_cast<const uint8_t *>(data.data()),
		      data.size());

	std::vector<uint8_t> output(needed);

	if (base64_decode(output.data(), output.size(), &written,
			  reinterpret_cast<const uint8_t *>(data.data()), data.size()) != 0) {
		return {};
	}
	output.resize(written);
	return output;
}

std::string Crypto::base64Encode(const std::vector<uint8_t> &data)
{
	size_t needed = 0;
	size_t written = 0;

	base64_encode(nullptr, 0, &needed, data.data(), data.size());

	std::string output(needed, '\0');

	if (base64_encode(reinterpret_cast<uint8_t *>(output.data()), output.size(), &written,
			  data.data(), data.size()) != 0) {
		return {};
	}
	output.resize(written); /* drop the terminating NUL counted by Zephyr */
	return output;
}

/* SHA-1 ------------------------------------------------------------------- */

void Crypto::sha1Init()
{
	if (sha1_->active) {
		psa_hash_abort(&sha1_->op);
	}
	sha1_->op = PSA_HASH_OPERATION_INIT;
	if (psa_hash_setup(&sha1_->op, PSA_ALG_SHA_1) != PSA_SUCCESS) {
		throw std::runtime_error("SHA-1 setup failed");
	}
	sha1_->active = true;
}

void Crypto::sha1Update(const std::string &s)
{
	psa_hash_update(&sha1_->op, reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

void Crypto::sha1Update(const std::vector<uint8_t> &vec)
{
	psa_hash_update(&sha1_->op, vec.data(), vec.size());
}

std::vector<uint8_t> Crypto::sha1FinalBytes()
{
	std::vector<uint8_t> digest(SHA1_SIZE);
	size_t len = 0;

	psa_hash_finish(&sha1_->op, digest.data(), digest.size(), &len);
	sha1_->active = false;
	return digest;
}

std::string Crypto::sha1Final()
{
	auto digest = sha1FinalBytes();

	return std::string(digest.begin(), digest.end());
}

/* HMAC-SHA1 --------------------------------------------------------------- */

std::vector<uint8_t> Crypto::sha1HMAC(const std::vector<uint8_t> &key,
				      const std::vector<uint8_t> &message)
{
	std::vector<uint8_t> digest(SHA1_SIZE);
	HmacSha1Key hmac(key.data(), key.size());

	hmac.compute(message.data(), message.size(), digest.data());
	return digest;
}

/* AES --------------------------------------------------------------------- */

void Crypto::aesCTRXcrypt(const std::vector<uint8_t> &key, std::vector<uint8_t> &iv,
			  uint8_t *data, size_t nbytes)
{
	if (nbytes == 0) {
		return;
	}

	const psa_key_id_t id =
		import_key(PSA_KEY_TYPE_AES, PSA_ALG_CTR,
			   PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, key.data(), key.size());
	psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
	/* PSA forbids overlapping input and output buffers: work on a copy. */
	std::vector<uint8_t> out(PSA_CIPHER_UPDATE_OUTPUT_SIZE(PSA_KEY_TYPE_AES, PSA_ALG_CTR, nbytes) +
				 PSA_CIPHER_FINISH_OUTPUT_SIZE(PSA_KEY_TYPE_AES, PSA_ALG_CTR));
	size_t produced = 0;
	size_t len = 0;
	const char *step = "setup";
	psa_status_t status = psa_cipher_encrypt_setup(&op, id, PSA_ALG_CTR);

	if (status == PSA_SUCCESS) {
		step = "set_iv";
		status = psa_cipher_set_iv(&op, iv.data(), iv.size());
	}
	if (status == PSA_SUCCESS) {
		step = "update";
		status = psa_cipher_update(&op, data, nbytes, out.data(), out.size(), &len);
		produced += len;
	}
	if (status == PSA_SUCCESS) {
		step = "finish";
		status = psa_cipher_finish(&op, out.data() + produced, out.size() - produced, &len);
		produced += len;
	}

	psa_cipher_abort(&op);
	psa_destroy_key(id);

	if (status != PSA_SUCCESS || produced != nbytes) {
		LOG_ERR("AES-CTR %s failed: %d (%u of %u bytes)", step, static_cast<int>(status),
			static_cast<unsigned int>(produced), static_cast<unsigned int>(nbytes));
		throw std::runtime_error("AES-CTR failed");
	}
	memcpy(data, out.data(), nbytes);
}

void Crypto::aesECBdecrypt(const std::vector<uint8_t> &key, std::vector<uint8_t> &data)
{
	const size_t len = data.size() - (data.size() % AES_BLOCK);

	if (len == 0) {
		return;
	}

	const psa_key_id_t id = import_key(PSA_KEY_TYPE_AES, PSA_ALG_ECB_NO_PADDING,
					   PSA_KEY_USAGE_DECRYPT, key.data(), key.size());
	std::vector<uint8_t> output(len);
	size_t out_len = 0;
	const psa_status_t status = psa_cipher_decrypt(id, PSA_ALG_ECB_NO_PADDING, data.data(),
						       len, output.data(), output.size(),
						       &out_len);

	psa_destroy_key(id);

	if (status != PSA_SUCCESS || out_len != len) {
		LOG_ERR("AES-ECB failed: %d", static_cast<int>(status));
		throw std::runtime_error("AES-ECB failed");
	}
	memcpy(data.data(), output.data(), len);
}

/* PBKDF2 ------------------------------------------------------------------ */

std::vector<uint8_t> Crypto::pbkdf2HmacSha1(const std::vector<uint8_t> &password,
					    const std::vector<uint8_t> &salt, int iterations,
					    int digest_size)
{
	std::vector<uint8_t> output;
	std::vector<uint8_t> block;
	HmacSha1Key prf(password.data(), password.size());
	uint8_t u[SHA1_SIZE];
	uint8_t t[SHA1_SIZE];

	output.reserve(digest_size);

	for (uint32_t index = 1; output.size() < static_cast<size_t>(digest_size); index++) {
		block = salt;
		block.push_back(static_cast<uint8_t>(index >> 24));
		block.push_back(static_cast<uint8_t>(index >> 16));
		block.push_back(static_cast<uint8_t>(index >> 8));
		block.push_back(static_cast<uint8_t>(index));

		prf.compute(block.data(), block.size(), u);
		memcpy(t, u, SHA1_SIZE);

		for (int i = 1; i < iterations; i++) {
			prf.compute(u, SHA1_SIZE, u);
			for (size_t j = 0; j < SHA1_SIZE; j++) {
				t[j] ^= u[j];
			}
		}

		const size_t take =
			std::min(SHA1_SIZE, static_cast<size_t>(digest_size) - output.size());

		output.insert(output.end(), t, t + take);
	}

	return output;
}

/* Diffie-Hellman ---------------------------------------------------------- */

void Crypto::dhInit()
{
	privateKey = generateVectorWithRandomData(DH_KEY_SIZE);
	if (!detail::mod_exp(DH_GENERATOR, privateKey, DH_PRIME, sizeof(DH_PRIME), publicKey)) {
		throw std::runtime_error("DH key generation failed");
	}
}

std::vector<uint8_t> Crypto::dhCalculateShared(const std::vector<uint8_t> &remote_key)
{
	std::vector<uint8_t> shared;

	if (!detail::mod_exp(remote_key, privateKey, DH_PRIME, sizeof(DH_PRIME), shared)) {
		throw std::runtime_error("DH shared secret failed");
	}
	return shared;
}

/* Random ------------------------------------------------------------------ */

std::vector<uint8_t> Crypto::generateVectorWithRandomData(size_t length)
{
	std::vector<uint8_t> random(length);

	if (psa_generate_random(random.data(), random.size()) != PSA_SUCCESS) {
		throw std::runtime_error("psa_generate_random failed");
	}
	return random;
}

} /* namespace zspot */
