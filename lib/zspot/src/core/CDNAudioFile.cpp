/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/CDNAudioFile.h"

#include <cstring>    // for memcpy
#include <stdexcept>  // for runtime_error

#include "core/Utils.h"  // for bigNumAdd
#include "port/log.h"

ZSPOT_LOG_MODULE_DECLARE();

using namespace cspot;

CDNAudioFile::CDNAudioFile(const std::string& cdnUrl,
                           const std::vector<uint8_t>& audioKey)
    : cdnUrl(cdnUrl), audioKey(audioKey) {
  this->crypto = std::make_unique<zspot::Crypto>();
  this->httpConnection = std::make_unique<zspot::HttpConnection>();
}

size_t CDNAudioFile::getPosition() {
  return this->position;
}

void CDNAudioFile::seek(size_t newPos) {
  this->enableRequestMargin = true;
  this->position = newPos;
}

void CDNAudioFile::openStream() {
  CSPOT_LOG(info, "Opening HTTP stream to %s", this->cdnUrl.c_str());

  // Fetch the first bytes, learn the total size from Content-Range
  auto response = httpConnection->get(
      cdnUrl, {zspot::HttpConnection::rangeHeader(0, OPUS_HEADER_SIZE - 1)});

  if (response.body.size() < OPUS_HEADER_SIZE ||
      response.totalLength() <= SPOTIFY_OPUS_HEADER) {
    CSPOT_LOG(error, "CDN returned status %d with %u bytes", response.status,
              static_cast<unsigned>(response.body.size()));
    throw std::runtime_error("Cannot open CDN stream");
  }

  this->header = std::move(response.body);
  this->header.resize(OPUS_HEADER_SIZE);
  this->totalFileSize = response.totalLength() - SPOTIFY_OPUS_HEADER;
  CSPOT_LOG(debug, "CDN status %d, range %u-%u/%u, content length %u, file size %u",
            response.status, static_cast<unsigned>(response.range_start),
            static_cast<unsigned>(response.range_end),
            static_cast<unsigned>(response.range_total),
            static_cast<unsigned>(response.content_length),
            static_cast<unsigned>(this->totalFileSize));
  this->decrypt(header.data(), OPUS_HEADER_SIZE, 0);

  // Footer start must be divisible by 16 (AES block)
  size_t footerStartLocation =
      (this->totalFileSize - OPUS_FOOTER_PREFFERED + SPOTIFY_OPUS_HEADER) -
      (this->totalFileSize - OPUS_FOOTER_PREFFERED + SPOTIFY_OPUS_HEADER) % 16;
  size_t footerSize =
      this->totalFileSize - footerStartLocation + SPOTIFY_OPUS_HEADER;

  response =
      httpConnection->get(cdnUrl, {zspot::HttpConnection::lastBytesHeader(footerSize)});
  if (response.body.size() != footerSize) {
    CSPOT_LOG(error, "CDN footer request returned %u bytes, expected %u",
              static_cast<unsigned>(response.body.size()),
              static_cast<unsigned>(footerSize));
    throw std::runtime_error("Cannot open CDN stream");
  }
  this->footer = std::move(response.body);
  this->decrypt(footer.data(), footer.size(), footerStartLocation);

  CSPOT_LOG(info, "Header and footer bytes received");
  this->position = 0;
  this->lastRequestPosition = 0;
  this->lastRequestCapacity = 0;
}

size_t CDNAudioFile::readBytes(uint8_t* dst, size_t bytes) {
  size_t offsetPosition = position + SPOTIFY_OPUS_HEADER;
  size_t actualFileSize = this->totalFileSize + SPOTIFY_OPUS_HEADER;

  if (position + bytes >= this->totalFileSize) {
    CSPOT_LOG(debug, "CDN read at end: position %u + %u >= %u",
              static_cast<unsigned>(position), static_cast<unsigned>(bytes),
              static_cast<unsigned>(this->totalFileSize));
    return 0;
  }

  // Decoder reads the header: serve from the prefetched copy
  if (offsetPosition < OPUS_HEADER_SIZE &&
      bytes + offsetPosition <= OPUS_HEADER_SIZE) {
    memcpy(dst, this->header.data() + offsetPosition, bytes);
    position += bytes;
    return bytes;
  }

  // Decoder reads the footer: serve from the prefetched copy
  if (offsetPosition >= (actualFileSize - this->footer.size())) {
    size_t toReadBytes = bytes;

    if ((position + bytes) > this->totalFileSize) {
      toReadBytes = this->totalFileSize - position;
    }

    size_t footerOffset =
        offsetPosition - (actualFileSize - this->footer.size());
    memcpy(dst, this->footer.data() + footerOffset, toReadBytes);

    position += toReadBytes;
    return toReadBytes;
  }

  // Inside the most recently fetched range
  if (offsetPosition >= this->lastRequestPosition &&
      offsetPosition < this->lastRequestPosition + this->lastRequestCapacity) {
    size_t toRead = bytes;

    if ((toRead + offsetPosition) >
        this->lastRequestPosition + lastRequestCapacity) {
      toRead = this->lastRequestPosition + lastRequestCapacity - offsetPosition;
    }

    memcpy(dst, this->httpBuffer.data() + offsetPosition - lastRequestPosition,
           toRead);
    position += toRead;

    return toRead;
  }

  // Fetch a new range, aligned to the AES block size
  size_t requestPosition = offsetPosition - (offsetPosition % 16);
  if (this->enableRequestMargin && requestPosition > SEEK_MARGIN_SIZE) {
    requestPosition = (offsetPosition - SEEK_MARGIN_SIZE) -
                      ((offsetPosition - SEEK_MARGIN_SIZE) % 16);
    this->enableRequestMargin = false;
  }

  auto response = httpConnection->get(
      cdnUrl, {zspot::HttpConnection::rangeHeader(
                  requestPosition, requestPosition + HTTP_BUFFER_SIZE - 1)});
  if (response.body.empty()) {
    CSPOT_LOG(error, "CDN range request failed (status %d)", response.status);
    return 0;
  }

  this->httpBuffer = std::move(response.body);
  this->lastRequestPosition = requestPosition;
  this->lastRequestCapacity = this->httpBuffer.size();
  CSPOT_LOG(debug, "CDN range %u+%u -> status %d, %u bytes",
            static_cast<unsigned>(requestPosition),
            static_cast<unsigned>(HTTP_BUFFER_SIZE), response.status,
            static_cast<unsigned>(lastRequestCapacity));
  this->decrypt(this->httpBuffer.data(), lastRequestCapacity,
                this->lastRequestPosition);

  return readBytes(dst, bytes);
}

size_t CDNAudioFile::getSize() {
  return this->totalFileSize;
}

void CDNAudioFile::decrypt(uint8_t* dst, size_t nbytes, size_t pos) {
  auto calculatedIV = bigNumAdd(audioAESIV, pos / 16);

  this->crypto->aesCTRXcrypt(this->audioKey, calculatedIV, dst, nbytes);
}
