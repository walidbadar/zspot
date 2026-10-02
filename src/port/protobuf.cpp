/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/protobuf.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>

static bool vector_write(pb_ostream_t *stream, const pb_byte_t *buf, size_t count)
{
	auto *dest = reinterpret_cast<std::vector<uint8_t> *>(stream->state);

	dest->insert(dest->end(), buf, buf + count);
	return true;
}

static pb_ostream_t ostream_from_vector(std::vector<uint8_t> &vec)
{
	pb_ostream_t stream;

	stream.callback = &vector_write;
	stream.state = &vec;
	stream.max_size = SIZE_MAX;
	stream.bytes_written = 0;
#ifndef PB_NO_ERRMSG
	stream.errmsg = nullptr;
#endif
	return stream;
}

std::vector<uint8_t> pbEncode(const pb_msgdesc_t *fields, const void *src_struct)
{
	std::vector<uint8_t> data;
	pb_ostream_t stream = ostream_from_vector(data);

	pb_encode(&stream, fields, src_struct);
	return data;
}

void packString(char *&dst, std::string string_to_pack)
{
	dst = static_cast<char *>(malloc(string_to_pack.size() + 1));
	memcpy(dst, string_to_pack.c_str(), string_to_pack.size() + 1);
}

pb_bytes_array_t *vectorToPbArray(const std::vector<uint8_t> &vector_to_pack)
{
	auto size = static_cast<pb_size_t>(vector_to_pack.size());
	auto *result = static_cast<pb_bytes_array_t *>(malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(size)));

	result->size = size;
	memcpy(result->bytes, vector_to_pack.data(), size);
	return result;
}

void pbPutString(const std::string &string_to_pack, char *dst)
{
	string_to_pack.copy(dst, string_to_pack.size());
	dst[string_to_pack.size()] = '\0';
}

void pbPutCharArray(const char *string_to_pack, char *dst)
{
	strcpy(dst, string_to_pack);
}

void pbPutBytes(const std::vector<uint8_t> &data, pb_bytes_array_t &dst)
{
	dst.size = data.size();
	std::copy(data.begin(), data.end(), dst.bytes);
}

std::vector<uint8_t> pbArrayToVector(pb_bytes_array_t *pb_array)
{
	if (pb_array == nullptr) {
		return {};
	}
	return std::vector<uint8_t>(pb_array->bytes, pb_array->bytes + pb_array->size);
}

bool cspot::nanopb::encodeString(pb_ostream_t *stream, const pb_field_t *field,
				 void *const *arg)
{
	auto &str = *static_cast<std::string *>(*arg);

	if (str.empty()) {
		return true;
	}
	return pb_encode_tag_for_field(stream, field) &&
	       pb_encode_string(stream, reinterpret_cast<const pb_byte_t *>(str.data()),
				str.size());
}

bool cspot::nanopb::encodeBoolean(pb_ostream_t *stream, const pb_field_t *field,
				  void *const *arg)
{
	auto &value = *static_cast<std::optional<bool> *>(*arg);

	if (!value.has_value()) {
		return true;
	}
	return pb_encode_tag_for_field(stream, field) && pb_encode_varint(stream, value.value());
}

bool cspot::nanopb::encodeVector(pb_ostream_t *stream, const pb_field_t *field,
				 void *const *arg)
{
	auto &vector = *static_cast<std::vector<uint8_t> *>(*arg);

	if (vector.empty()) {
		return true;
	}
	return pb_encode_tag_for_field(stream, field) &&
	       pb_encode_string(stream, vector.data(), vector.size());
}
