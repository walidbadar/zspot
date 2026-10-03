/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stddef.h>  // for size_t
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

#include "port/thread.h"
#include "core/PlaybackState.h"
#include "core/TrackReference.h"

#include "protobuf/metadata.pb.h"  // for Track, _Track, AudioFile, Episode

#include "port/sync.h"
namespace cspot {
struct Context;
class AccessKeyFetcher;
class CDNAudioFile;

// Used in got track info event
struct TrackInfo {
  std::string name, album, artist, imageUrl, trackId;
  uint32_t duration, number, discNumber;

  void loadPbTrack(Track* pbTrack, const std::vector<uint8_t>& gid);
  void loadPbEpisode(Episode* pbEpisode, const std::vector<uint8_t>& gid);
};

class QueuedTrack {
 public:
  QueuedTrack(TrackReference& ref, std::shared_ptr<cspot::Context> ctx,
              uint32_t requestedPosition = 0);
  ~QueuedTrack();

  enum class State {
    QUEUED,
    PENDING_META,
    KEY_REQUIRED,
    PENDING_KEY,
    CDN_REQUIRED,
    READY,
    FAILED
  };

  std::shared_ptr<zspot::Semaphore> loadedSemaphore;

  State state = State::QUEUED;  // Current state of the track
  TrackReference ref;           // Holds GID, URI and Context
  TrackInfo trackInfo;  // Full track information fetched from spotify, name etc

  uint32_t requestedPosition;
  std::string identifier;
  bool loading = false;

  // Will return nullptr if the track is not ready
  std::shared_ptr<cspot::CDNAudioFile> getAudioFile();

  // --- Steps ---
  void stepLoadMetadata(
      Track* pbTrack, Episode* pbEpisode, zspot::Mutex& trackListMutex,
      std::shared_ptr<zspot::Semaphore> updateSemaphore);

  void stepParseMetadata(Track* pbTrack, Episode* pbEpisode);

  void stepLoadAudioFile(
      zspot::Mutex& trackListMutex,
      std::shared_ptr<zspot::Semaphore> updateSemaphore);

  void stepLoadCDNUrl(const std::string& accessKey);

  void expire();

 private:
  std::shared_ptr<cspot::Context> ctx;

  uint64_t pendingMercuryRequest = 0;
  uint32_t pendingAudioKeyRequest = 0;

  std::vector<uint8_t> trackId, fileId, audioKey;
  std::string cdnUrl;
};

class TrackQueue : public zspot::Task {
 public:
  TrackQueue(std::shared_ptr<cspot::Context> ctx,
             std::shared_ptr<cspot::PlaybackState> playbackState);
  ~TrackQueue();

  enum class SkipDirection { NEXT, PREV };

  std::shared_ptr<zspot::Semaphore> playableSemaphore;
  std::atomic<bool> notifyPending = false;

  void runTask() override;
  void stopTask();

  bool hasTracks();
  bool isFinished();
  bool skipTrack(SkipDirection dir, bool expectNotify = true);
  bool updateTracks(uint32_t requestedPosition = 0, bool initial = false);
  TrackInfo getTrackInfo(std::string_view identifier);
  std::shared_ptr<QueuedTrack> consumeTrack(
      std::shared_ptr<QueuedTrack> prevSong, int& offset);

 private:
  static const int MAX_TRACKS_PRELOAD = 3;

  std::shared_ptr<cspot::AccessKeyFetcher> accessKeyFetcher;
  std::shared_ptr<PlaybackState> playbackState;
  std::shared_ptr<cspot::Context> ctx;
  std::shared_ptr<zspot::Semaphore> processSemaphore;

  std::deque<std::shared_ptr<QueuedTrack>> preloadedTracks;
  std::vector<TrackReference> currentTracks;
  zspot::Mutex tracksMutex, runningMutex;

  // PB data
  Track pbTrack;
  Episode pbEpisode;

  std::string accessKey;

  int16_t currentTracksIndex = -1;

  bool isRunning = false;

  void processTrack(std::shared_ptr<QueuedTrack> track);
  bool queueNextTrack(int offset = 0, uint32_t positionMs = 0);
};
}  // namespace cspot
