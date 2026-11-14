/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/AuthChallenges.h"

#include <algorithm>  // for copy
#include <stdexcept>  // for runtime_error
#include <climits>    // for CHAR_BIT

#include "port/protobuf.h"
#include "pb.h"            // for pb_byte_t
#include "pb_decode.h"     // for pb_release

using namespace cspot;

#ifdef CONFIG_ZSPOT_AP_VERIFY
// Modulus of the RSA key that Spotify's access points sign their half of the
// key exchange with, as other clients carry it; the exponent is 65537
static const std::vector<uint8_t> serverKey = {
    0xac, 0xe0, 0x46, 0x0b, 0xff, 0xc2, 0x30, 0xaf, 0xf4, 0x6b, 0xfe, 0xc3,
    0xbf, 0xbf, 0x86, 0x3d, 0xa1, 0x91, 0xc6, 0xcc, 0x33, 0x6c, 0x93, 0xa1,
    0x4f, 0xb3, 0xb0, 0x16, 0x12, 0xac, 0xac, 0x6a, 0xf1, 0x80, 0xe7, 0xf6,
    0x14, 0xd9, 0x42, 0x9d, 0xbe, 0x2e, 0x34, 0x66, 0x43, 0xe3, 0x62, 0xd2,
    0x32, 0x7a, 0x1a, 0x0d, 0x92, 0x3b, 0xae, 0xdd, 0x14, 0x02, 0xb1, 0x81,
    0x55, 0x05, 0x61, 0x04, 0xd5, 0x2c, 0x96, 0xa4, 0x4c, 0x1e, 0xcc, 0x02,
    0x4a, 0xd4, 0xb2, 0x0c, 0x00, 0x1f, 0x17, 0xed, 0xc2, 0x2f, 0xc4, 0x35,
    0x21, 0xc8, 0xf0, 0xcb, 0xae, 0xd2, 0xad, 0xd7, 0x2b, 0x0f, 0x9d, 0xb3,
    0xc5, 0x32, 0x1a, 0x2a, 0xfe, 0x59, 0xf3, 0x5a, 0x0d, 0xac, 0x68, 0xf1,
    0xfa, 0x62, 0x1e, 0xfb, 0x2c, 0x8d, 0x0c, 0xb7, 0x39, 0x2d, 0x92, 0x47,
    0xe3, 0xd7, 0x35, 0x1a, 0x6d, 0xbd, 0x24, 0xc2, 0xae, 0x25, 0x5b, 0x88,
    0xff, 0xab, 0x73, 0x29, 0x8a, 0x0b, 0xcc, 0xcd, 0x0c, 0x58, 0x67, 0x31,
    0x89, 0xe8, 0xbd, 0x34, 0x80, 0x78, 0x4a, 0x5f, 0xc9, 0x6b, 0x89, 0x9d,
    0x95, 0x6b, 0xfc, 0x86, 0xd7, 0x4f, 0x33, 0xa6, 0x78, 0x17, 0x96, 0xc9,
    0xc3, 0x2d, 0x0d, 0x32, 0xa5, 0xab, 0xcd, 0x05, 0x27, 0xe2, 0xf7, 0x10,
    0xa3, 0x96, 0x13, 0xc4, 0x2f, 0x99, 0xc0, 0x27, 0xbf, 0xed, 0x04, 0x9c,
    0x3c, 0x27, 0x58, 0x04, 0xb6, 0xb2, 0x19, 0xf9, 0xc1, 0x2f, 0x02, 0xe9,
    0x48, 0x63, 0xec, 0xa1, 0xb6, 0x42, 0xa0, 0x9d, 0x48, 0x25, 0xf8, 0xb3,
    0x9d, 0xd0, 0xe8, 0x6a, 0xf9, 0x48, 0x4d, 0xa1, 0xc2, 0xba, 0x86, 0x30,
    0x42, 0xea, 0x9d, 0xb3, 0x08, 0x6c, 0x19, 0x0e, 0x48, 0xb3, 0x9d, 0x66,
    0xeb, 0x00, 0x06, 0xa2, 0x5a, 0xee, 0xa1, 0x1b, 0x13, 0x87, 0x3c, 0xd7,
    0x19, 0xe6, 0x55, 0xbd,
};
#endif

AuthChallenges::AuthChallenges() {
  this->crypto = std::make_unique<zspot::Crypto>();
  this->clientHello = {};
  this->apResponse = {};
  this->authRequest = {};
  this->clientResPlaintext = {};
}

AuthChallenges::~AuthChallenges() {
  // Destruct the protobufs
  pb_release(ClientHello_fields, &clientHello);
  pb_release(APResponseMessage_fields, &apResponse);
  pb_release(ClientResponsePlaintext_fields, &clientResPlaintext);
  pb_release(ClientResponseEncrypted_fields, &authRequest);
}

std::vector<uint8_t> AuthChallenges::prepareAuthPacket(
    std::vector<uint8_t>& authData, int authType, const std::string& deviceId,
    const std::string& username) {
  // prepare authentication request proto
  pbPutString(username, authRequest.login_credentials.username);

  std::copy(authData.begin(), authData.end(),
            authRequest.login_credentials.auth_data.bytes);
  authRequest.login_credentials.auth_data.size = authData.size();

  authRequest.login_credentials.typ = (AuthenticationType)authType;
  authRequest.system_info.cpu_family = CpuFamily_CPU_UNKNOWN;
  authRequest.system_info.os = Os_OS_UNKNOWN;

  auto infoStr = std::string("cspot-player");
  pbPutString(infoStr, authRequest.system_info.system_information_string);
  pbPutString(deviceId, authRequest.system_info.device_id);

  auto versionStr = std::string("cspot-1.1");
  pbPutString(versionStr, authRequest.version_string);
  authRequest.has_version_string = true;

  return pbEncode(ClientResponseEncrypted_fields, &authRequest);
}

std::vector<uint8_t> AuthChallenges::solveApHello(
    std::vector<uint8_t>& helloPacket, std::vector<uint8_t>& data) {
  // Decode the response
  auto skipSize = std::vector<uint8_t>(data.begin() + 4, data.end());

  pb_release(APResponseMessage_fields, &apResponse);
  pbDecode(apResponse, APResponseMessage_fields, skipSize);

  auto diffieKey = std::vector<uint8_t>(
      apResponse.challenge.login_crypto_challenge.diffie_hellman.gs,
      apResponse.challenge.login_crypto_challenge.diffie_hellman.gs + 96);

#ifdef CONFIG_ZSPOT_AP_VERIFY
  // Whoever is in the path can do the key exchange as well. Only Spotify can
  // sign its half of it, which the credentials are then sent under.
  const auto& dh = apResponse.challenge.login_crypto_challenge.diffie_hellman;

  if (!dh.has_gs_signature ||
      !this->crypto->rsaSha1Verify(
          serverKey, diffieKey,
          std::vector<uint8_t>(dh.gs_signature,
                               dh.gs_signature + sizeof(dh.gs_signature)))) {
    throw std::runtime_error(
        "The access point did not prove that it is Spotify's");
  }
#endif

  // Compute the diffie hellman shared key based on the response
  auto sharedKey = this->crypto->dhCalculateShared(diffieKey);

  // Init client packet + Init server packets are required for the hmac challenge
  data.insert(data.begin(), helloPacket.begin(), helloPacket.end());

  // Solve the hmac challenge
  auto resultData = std::vector<uint8_t>(0);
  for (int x = 1; x < 6; x++) {
    auto challengeVector = std::vector<uint8_t>(1);
    challengeVector[0] = x;

    challengeVector.insert(challengeVector.begin(), data.begin(), data.end());
    auto digest = crypto->sha1HMAC(sharedKey, challengeVector);
    resultData.insert(resultData.end(), digest.begin(), digest.end());
  }

  auto lastVec =
      std::vector<uint8_t>(resultData.begin(), resultData.begin() + 0x14);

  // Digest generated!
  auto digest = crypto->sha1HMAC(lastVec, data);
  clientResPlaintext.login_crypto_response.has_diffie_hellman = true;

  std::copy(digest.begin(), digest.end(),
            clientResPlaintext.login_crypto_response.diffie_hellman.hmac);

  // Get send and receive keys
  this->shanSendKey = std::vector<uint8_t>(resultData.begin() + 0x14,
                                           resultData.begin() + 0x34);
  this->shanRecvKey = std::vector<uint8_t>(resultData.begin() + 0x34,
                                           resultData.begin() + 0x54);

  return pbEncode(ClientResponsePlaintext_fields, &clientResPlaintext);
}

std::vector<uint8_t> AuthChallenges::prepareClientHello() {
  // Prepare protobuf message
  this->crypto->dhInit();

  // Copy the public key into diffiehellman hello packet
  std::copy(this->crypto->publicKey.begin(), this->crypto->publicKey.end(),
            clientHello.login_crypto_hello.diffie_hellman.gc);

  clientHello.login_crypto_hello.diffie_hellman.server_keys_known = 1;
  clientHello.build_info.product = Product_PRODUCT_CLIENT;
  clientHello.build_info.platform = Platform2_PLATFORM_LINUX_X86;
  clientHello.build_info.version = SPOTIFY_VERSION;
  clientHello.feature_set.autoupdate2 = true;
  clientHello.cryptosuites_supported[0] = Cryptosuite_CRYPTO_SUITE_SHANNON;
  clientHello.padding[0] = 0x1E;

  clientHello.has_feature_set = true;
  clientHello.login_crypto_hello.has_diffie_hellman = true;
  clientHello.has_padding = true;
  clientHello.has_feature_set = true;

  // Generate the random nonce
  auto nonce = crypto->generateVectorWithRandomData(16);
  std::copy(nonce.begin(), nonce.end(), clientHello.client_nonce);

  return pbEncode(ClientHello_fields, &clientHello);
}
