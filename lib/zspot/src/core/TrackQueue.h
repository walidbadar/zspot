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

  // Position in the play queue this entry plays
  int index = -1;
  // Tells two plays of the same track apart, e.g. with repeat one
  const uint32_t playNumber;

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
  enum class RepeatMode { OFF, CONTEXT, TRACK };

  std::shared_ptr<zspot::Semaphore> playableSemaphore;
  std::atomic<bool> notifyPending = false;

  void runTask() override;
  void stopTask();

  bool hasTracks();
  bool isFinished();
  bool skipTrack(SkipDirection dir, bool expectNotify = true);
  bool updateTracks(uint32_t requestedPosition = 0, bool initial = false);
  TrackInfo getTrackInfo(std::string_view identifier);

  // Play queue as handed over by the controlling app
  typedef std::function<void(const TrackInfo*)> TrackInfoCallback;

  size_t getTrackCount();
  int getCurrentIndex();
  // Fetches the metadata of a queue entry; the callback receives nullptr when
  // that failed. Returns false for an index outside of the queue.
  bool requestTrackInfo(int index, TrackInfoCallback callback);
  // Makes a queue entry the current track
  bool jumpTo(int index);

  // Changes what follows the current track; the tracks the player already
  // decodes stay as they are
  void setRepeat(RepeatMode mode);
  RepeatMode getRepeat();
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
  RepeatMode repeatMode = RepeatMode::OFF;

  bool isRunning = false;

  void processTrack(std::shared_ptr<QueuedTrack> track);

  // Queue positions before and after @p index in the repeat mode, -1 at the
  // ends of the queue. Called with tracksMutex held, as the ones below.
  int followingIndex(int index);
  int precedingIndex(int index);
  bool queueTrackAt(int index, bool front, uint32_t positionMs = 0);
  // Puts the current track at the head again, from @p positionMs
  bool requeueCurrent(uint32_t positionMs = 0);
  // Preloads the track that follows the last preloaded one
  bool queueFollowing();
  // Drops preloaded tracks that no longer follow in the repeat mode and
  // preloads what follows instead. @p keepPlaying keeps the ones the player
  // decodes, for when it is not about to restart.
  void refreshPreloaded(bool keepPlaying);
};
}  // namespace cspot
