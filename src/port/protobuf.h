/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Helpers around nanopb (provided by Zephyr's nanopb module).
 *
 * Function names follow the upstream cspot helpers they replace.
 */

#ifndef CSPOT_PORT_PROTOBUF_H_
#define CSPOT_PORT_PROTOBUF_H_

#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/sys/printk.h>

#include <cstdint>
#include <string>
#include <vector>

std::vector<uint8_t> pbEncode(const pb_msgdesc_t *fields, const void *src_struct);

pb_bytes_array_t *vectorToPbArray(const std::vector<uint8_t> &vector_to_pack);

void packString(char *&dst, std::string string_to_pack);

std::vector<uint8_t> pbArrayToVector(pb_bytes_array_t *pb_array);

template <typename T> T pbDecode(const pb_msgdesc_t *fields, std::vector<uint8_t> &data)
{
	T result = {};
	pb_istream_t stream = pb_istream_from_buffer(data.data(), data.size());

	if (!pb_decode(&stream, fields, &result)) {
		printk("cspot: protobuf decode failed: %s\n", PB_GET_ERROR(&stream));
	}
	return result;
}

template <typename T>
void pbDecode(T &result, const pb_msgdesc_t *fields, std::vector<uint8_t> &data)
{
	pb_istream_t stream = pb_istream_from_buffer(data.data(), data.size());

	if (!pb_decode(&stream, fields, &result)) {
		printk("cspot: protobuf decode failed: %s\n", PB_GET_ERROR(&stream));
	}
}

void pbPutString(const std::string &string_to_pack, char *dst);
void pbPutCharArray(const char *string_to_pack, char *dst);
void pbPutBytes(const std::vector<uint8_t> &data, pb_bytes_array_t &dst);

namespace cspot::nanopb
{

/* Encode callbacks for pb_callback_t fields backed by C++ containers. */
bool encodeString(pb_ostream_t *stream, const pb_field_t *field, void *const *arg);
bool encodeVector(pb_ostream_t *stream, const pb_field_t *field, void *const *arg);
bool encodeBoolean(pb_ostream_t *stream, const pb_field_t *field, void *const *arg);

} /* namespace cspot::nanopb */

#endif /* CSPOT_PORT_PROTOBUF_H_ */
