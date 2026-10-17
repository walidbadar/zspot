/*
 * Copyright (c) 2020-2024 Filip Krzywda (feelfreelinux) and cspot contributors
 * Copyright (c) 2026 Muhammad Waleed Badar (Zephyr port)
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "core/TrackQueue.h"
#include <pb_decode.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <functional>
#include <memory>
#include <mutex>

#include "core/AccessKeyFetcher.h"
#include "port/thread.h"
#include "core/CDNAudioFile.h"
#include "core/Context.h"
#include "port/http.h"
#include "port/log.h"
#include "core/Utils.h"
#include "port/sync.h"
#include "port/json.h"
#include "protobuf/metadata.pb.h"

ZSPOT_LOG_MODULE_DECLARE();

using namespace cspot;
namespace TrackDataUtils {
bool countryListContains(char* countryList, const char* country) {
  uint16_t countryList_length = strlen(countryList);
  for (int x = 0; x < countryList_length; x += 2) {
    if (countryList[x] == country[0] && countryList[x + 1] == country[1]) {
      return true;
    }
  }
  return false;
}

bool doRestrictionsApply(Restriction* restrictions, int count,
                         const char* country) {
  for (int x = 0; x < count; x++) {
    if (restrictions[x].countries_allowed != nullptr) {
      return !countryListContains(restrictions[x].countries_allowed, country);
    }

    if (restrictions[x].countries_forbidden != nullptr) {
      return countryListContains(restrictions[x].countries_forbidden, country);
    }
  }

  return false;
}

bool canPlayTrack(Track& trackInfo, int altIndex, const char* country) {
  if (altIndex < 0) {

  } else {
    const int restrictionCount =
        static_cast<int>(trackInfo.alternative[altIndex].restriction_count);
    for (int x = 0; x < restrictionCount; x++) {
      if (trackInfo.alternative[altIndex].restriction[x].countries_allowed !=
          nullptr) {
        return countryListContains(
            trackInfo.alternative[altIndex].restriction[x].countries_allowed,
            country);
      }

      if (trackInfo.alternative[altIndex].restriction[x].countries_forbidden !=
          nullptr) {
        return !countryListContains(
            trackInfo.alternative[altIndex].restriction[x].countries_forbidden,
            country);
      }
    }
  }
  return true;
}
}  // namespace TrackDataUtils

void TrackInfo::loadPbTrack(Track* pbTrack, const std::vector<uint8_t>& gid) {
  // Generate ID based on GID
  trackId = bytesToHexString(gid);

  name = std::string(pbTrack->name);

  if (pbTrack->artist_count > 0) {
    // Handle artist data
    artist = std::string(pbTrack->artist[0].name);
  }

  if (pbTrack->has_album) {
    // Handle album data
    album = std::string(pbTrack->album.name);

    if (pbTrack->album.has_cover_group &&
        pbTrack->album.cover_group.image_count > 0) {
      auto imageId =
          pbArrayToVector(pbTrack->album.cover_group.image[0].file_id);
      imageUrl = "https://i.scdn.co/image/" + bytesToHexString(imageId);
    }
  }

  number = pbTrack->has_number ? pbTrack->number : 0;
  discNumber = pbTrack->has_disc_number ? pbTrack->disc_number : 0;
  duration = pbTrack->duration;
}

void TrackInfo::loadPbEpisode(Episode* pbEpisode,
                              const std::vector<uint8_t>& gid) {
  // Generate ID based on GID
  trackId = bytesToHexString(gid);

  name = std::string(pbEpisode->name);

  if (pbEpisode->covers != nullptr && pbEpisode->covers->image_count > 0) {
    // Handle episode info
    auto imageId = pbArrayToVector(pbEpisode->covers->image[0].file_id);
    imageUrl = "https://i.scdn.co/image/" + bytesToHexString(imageId);
  }

  number = pbEpisode->has_number ? pbEpisode->number : 0;
  discNumber = 0;
  duration = pbEpisode->duration;
}

static std::atomic<uint32_t> playCounter{0};

QueuedTrack::QueuedTrack(TrackReference& ref,
                         std::shared_ptr<cspot::Context> ctx,
                         uint32_t requestedPosition)
    : requestedPosition(requestedPosition),
      playNumber(++playCounter),
      ctx(ctx) {
  this->ref = ref;

  loadedSemaphore = std::make_shared<zspot::Semaphore>();
  state = State::QUEUED;
}

QueuedTrack::~QueuedTrack() {
  state = State::FAILED;
  loadedSemaphore->give();

  if (pendingMercuryRequest != 0) {
    ctx->session->unregister(pendingMercuryRequest);
  }

  if (pendingAudioKeyRequest != 0) {
    ctx->session->unregisterAudioKey(pendingAudioKeyRequest);
  }
}

std::shared_ptr<cspot::CDNAudioFile> QueuedTrack::getAudioFile() {
  if (state != State::READY) {
    return nullptr;
  }

  return std::make_shared<cspot::CDNAudioFile>(cdnUrl, audioKey);
}

void QueuedTrack::stepParseMetadata(Track* pbTrack, Episode* pbEpisode) {
  int filesCount = 0;
  AudioFile* selectedFiles = nullptr;

  const char* countryCode = ctx->config.countryCode.c_str();

  if (ref.type == TrackReference::Type::TRACK) {
    CSPOT_LOG(info, "Track name: %s", pbTrack->name);
    CSPOT_LOG(info, "Track duration: %d", pbTrack->duration);

    CSPOT_LOG(debug, "trackInfo.restriction.size() = %d",
              pbTrack->restriction_count);

    // Check if we can play the track, if not, try alternatives
    if (TrackDataUtils::doRestrictionsApply(
            pbTrack->restriction, pbTrack->restriction_count, countryCode)) {
      // Go through alternatives
      for (int x = 0; x < static_cast<int>(pbTrack->alternative_count); x++) {
        if (!TrackDataUtils::doRestrictionsApply(
                pbTrack->alternative[x].restriction,
                pbTrack->alternative[x].restriction_count, countryCode)) {
          selectedFiles = pbTrack->alternative[x].file;
          filesCount = pbTrack->alternative[x].file_count;
          trackId = pbArrayToVector(pbTrack->alternative[x].gid);
          break;
        }
      }
    } else {
      // We can play the track
      selectedFiles = pbTrack->file;
      filesCount = pbTrack->file_count;
      trackId = pbArrayToVector(pbTrack->gid);
    }

    if (trackId.size() > 0) {
      // Load track information
      trackInfo.loadPbTrack(pbTrack, trackId);
    }
  } else {
    // Handle episodes
    CSPOT_LOG(info, "Episode name: %s", pbEpisode->name);
    CSPOT_LOG(info, "Episode duration: %d", pbEpisode->duration);

    CSPOT_LOG(debug, "episodeInfo.restriction.size() = %d",
              pbEpisode->restriction_count);

    // Check if we can play the episode
    if (!TrackDataUtils::doRestrictionsApply(pbEpisode->restriction,
                                             pbEpisode->restriction_count,
                                             countryCode)) {
      selectedFiles = pbEpisode->file;
      filesCount = pbEpisode->file_count;
      trackId = pbArrayToVector(pbEpisode->gid);

      // Load track information
      trackInfo.loadPbEpisode(pbEpisode, trackId);
    }
  }

  // Find playable file
  for (int x = 0; x < filesCount; x++) {
    CSPOT_LOG(debug, "File format: %d", selectedFiles[x].format);
    if (selectedFiles[x].format == ctx->config.audioFormat) {
      fileId = pbArrayToVector(selectedFiles[x].file_id);
      break;  // If file found stop searching
    }

    // Fallback to OGG Vorbis 96kbps
    if (fileId.size() == 0 &&
        selectedFiles[x].format == AudioFormat_OGG_VORBIS_96) {
      fileId = pbArrayToVector(selectedFiles[x].file_id);
    }
  }

  // No viable files found for playback
  if (fileId.size() == 0) {
    CSPOT_LOG(info, "File not available for playback");

    // no alternatives for song
    state = State::FAILED;
    loadedSemaphore->give();
    return;
  }

  // Assign track identifier
  identifier = bytesToHexString(fileId);

  state = State::KEY_REQUIRED;
}

void QueuedTrack::stepLoadAudioFile(
    zspot::Mutex& trackListMutex,
    std::shared_ptr<zspot::Semaphore> updateSemaphore) {
  // Request audio key
  this->pendingAudioKeyRequest = ctx->session->requestAudioKey(
      trackId, fileId,
      [this, &trackListMutex, updateSemaphore](
          bool success, const std::vector<uint8_t>& audioKey) {
        std::scoped_lock lock(trackListMutex);

        if (success) {
          CSPOT_LOG(info, "Got audio key");
          this->audioKey =
              std::vector<uint8_t>(audioKey.begin() + 4, audioKey.end());

          state = State::CDN_REQUIRED;
        } else if (audioKey.empty()) {
          // No answer: the connection was lost, asked for again once it is
          // back
          state = State::KEY_REQUIRED;
        } else {
          CSPOT_LOG(error, "Failed to get audio key");
          state = State::FAILED;
          loadedSemaphore->give();
        }
        updateSemaphore->give();
      });

  state = State::PENDING_KEY;
}

void QueuedTrack::stepLoadCDNUrl(const std::string& accessKey) {
  if (accessKey.size() == 0) {
    // Wait for access key
    return;
  }

  // Request CDN URL
  CSPOT_LOG(info, "Received access key, fetching CDN URL...");

  try {

    std::string requestUrl = string_format(
        "https://api.spotify.com/v1/storage-resolve/files/audio/interactive/"
        "%s?alt=json&product=9",
        bytesToHexString(fileId).c_str());

    auto response = zspot::HttpConnection::fetch(
        "GET", requestUrl, {"Authorization: Bearer " + accessKey});

    std::string_view result(
        reinterpret_cast<const char*>(response.body.data()),
        response.body.size());

    if (!zspot::json::firstOfStringArray(result, "cdnurl", cdnUrl)) {
      throw std::runtime_error("No CDN url in storage-resolve response");
    }

    CSPOT_LOG(info, "Received CDN URL, %s", cdnUrl.c_str());
    state = State::READY;
    loadedSemaphore->give();
  } catch (...) {
    CSPOT_LOG(error, "Cannot fetch CDN URL");
    state = State::FAILED;
    loadedSemaphore->give();
  }
}

void QueuedTrack::expire() {
  if (state != State::QUEUED) {
    state = State::FAILED;
    loadedSemaphore->give();
  }
}

void QueuedTrack::stepLoadMetadata(
    Track* pbTrack, Episode* pbEpisode, zspot::Mutex& trackListMutex,
    std::shared_ptr<zspot::Semaphore> updateSemaphore) {

  // Prepare request ID
  std::string requestUrl = string_format(
      "hm://metadata/3/%s/%s",
      ref.type == TrackReference::Type::TRACK ? "track" : "episode",
      bytesToHexString(ref.gid).c_str());

  auto responseHandler = [this, pbTrack, pbEpisode, &trackListMutex,
                          updateSemaphore](MercurySession::Response& res) {
    std::scoped_lock lock(trackListMutex);

    if (res.fail) {
      // The connection was lost, asked for again once it is back
      pendingMercuryRequest = 0;
      state = State::QUEUED;
      updateSemaphore->give();
      return;
    }

    if (res.parts.size() == 0) {
      // Invalid metadata, cannot proceed
      state = State::FAILED;
      updateSemaphore->give();
      loadedSemaphore->give();
      return;
    }

    // Parse the metadata
    if (ref.type == TrackReference::Type::TRACK) {
      pb_release(Track_fields, pbTrack);
      pbDecode(*pbTrack, Track_fields, res.parts[0]);
    } else {
      pb_release(Episode_fields, pbEpisode);
      pbDecode(*pbEpisode, Episode_fields, res.parts[0]);
    }

    // Parse received metadata
    stepParseMetadata(pbTrack, pbEpisode);

    updateSemaphore->give();
  };
  // Execute the request
  pendingMercuryRequest = ctx->session->execute(
      MercurySession::RequestType::GET, requestUrl, responseHandler);

  // Set the state to pending
  state = State::PENDING_META;
}

TrackQueue::TrackQueue(std::shared_ptr<cspot::Context> ctx,
                       std::shared_ptr<cspot::PlaybackState> state)
    : zspot::Task("zspot_queue", CONFIG_ZSPOT_TRACKQUEUE_STACK_SIZE, 2),
      playbackState(state),
      ctx(ctx) {
  accessKeyFetcher = std::make_shared<cspot::AccessKeyFetcher>(ctx);
  processSemaphore = std::make_shared<zspot::Semaphore>();
  playableSemaphore = std::make_shared<zspot::Semaphore>();

  // Assign encode callback to track list
  playbackState->innerFrame.state.track.funcs.encode =
      &TrackReference::pbEncodeTrackList;
  playbackState->innerFrame.state.track.arg = &currentTracks;
  pbTrack = Track_init_zero;
  pbEpisode = Episode_init_zero;

  // Start the task
  startTask();
};

TrackQueue::~TrackQueue() {
  stopTask();

  std::scoped_lock lock(tracksMutex);

  pb_release(Track_fields, &pbTrack);
  pb_release(Episode_fields, &pbEpisode);
}

TrackInfo TrackQueue::getTrackInfo(std::string_view identifier) {
  for (auto& track : preloadedTracks) {
    if (track->identifier == identifier)
      return track->trackInfo;
  }
  return TrackInfo{};
}

void TrackQueue::runTask() {
  isRunning = true;

  std::scoped_lock lock(runningMutex);

  std::deque<std::shared_ptr<QueuedTrack>> trackQueue;

  while (isRunning) {
    processSemaphore->twait(100);

    // Make sure we have the newest access key
    accessKey = accessKeyFetcher->getAccessKey();

    int loadedIndex = currentTracksIndex;

    // No tracks loaded yet
    if (loadedIndex < 0) {
      continue;
    } else {
      std::scoped_lock lock(tracksMutex);

      trackQueue = preloadedTracks;
    }

    for (auto& track : trackQueue) {
      if (track) {
        this->processTrack(track);
      }
    }
  }
}

void TrackQueue::stopTask() {
  if (isRunning) {
    isRunning = false;
    processSemaphore->give();
    std::scoped_lock lock(runningMutex);
  }
}

std::shared_ptr<QueuedTrack> TrackQueue::consumeTrack(
    std::shared_ptr<QueuedTrack> prevTrack, int& offset) {
  std::scoped_lock lock(tracksMutex);

  if (currentTracksIndex == -1 || static_cast<size_t>(currentTracksIndex) >= currentTracks.size()) {
    return nullptr;
  }

  // No previous track, return head
  if (prevTrack == nullptr) {
    offset = 0;

    return preloadedTracks[0];
  }

  // if (currentTracksIndex + preloadedTracks.size() >= currentTracks.size()) {
  //   offset = -1;

  //   // Last track in queue
  //   return nullptr;
  // }

  auto prevTrackIter =
      std::find(preloadedTracks.begin(), preloadedTracks.end(), prevTrack);

  if (prevTrackIter != preloadedTracks.end()) {
    // Get offset of next track
    offset = prevTrackIter - preloadedTracks.begin() + 1;
  } else {
    offset = 0;
  }

  if (static_cast<size_t>(offset) >= preloadedTracks.size()) {
    // Last track in preloaded queue
    return nullptr;
  }

  // Return the current track
  return preloadedTracks[offset];
}

void TrackQueue::processTrack(std::shared_ptr<QueuedTrack> track) {
  // The tracks keep their state and load on once the connection is back
  if (!ctx->session->isConnected()) {
    return;
  }

  switch (track->state) {
    case QueuedTrack::State::QUEUED:
      track->stepLoadMetadata(&pbTrack, &pbEpisode, tracksMutex,
                              processSemaphore);
      break;
    case QueuedTrack::State::KEY_REQUIRED:
      track->stepLoadAudioFile(tracksMutex, processSemaphore);
      break;
    case QueuedTrack::State::CDN_REQUIRED:
      track->stepLoadCDNUrl(accessKey);

      if (track->state == QueuedTrack::State::READY) {
        std::scoped_lock lock(tracksMutex);

        if (preloadedTracks.size() < MAX_TRACKS_PRELOAD) {
          // Queue a new track to preload
          queueFollowing();
        }
      }
      break;
    default:
      // Do not perform any action
      break;
  }
}

int TrackQueue::followingIndex(int index) {
  const int count = static_cast<int>(currentTracks.size());

  if (index < 0 || index >= count) {
    return -1;
  }

  switch (repeatMode) {
    case RepeatMode::TRACK:
      return index;
    case RepeatMode::CONTEXT:
      return (index + 1) % count;
    default:
      return index + 1 < count ? index + 1 : -1;
  }
}

int TrackQueue::precedingIndex(int index) {
  const int count = static_cast<int>(currentTracks.size());

  if (index < 0 || index >= count) {
    return -1;
  }

  // Back from the first track only wraps around while the queue repeats
  if (index == 0) {
    return repeatMode == RepeatMode::CONTEXT ? count - 1 : -1;
  }
  return index - 1;
}

bool TrackQueue::queueTrackAt(int index, bool front, uint32_t positionMs) {
  if (index < 0 || static_cast<size_t>(index) >= currentTracks.size()) {
    return false;
  }

  auto track =
      std::make_shared<QueuedTrack>(currentTracks[index], ctx, positionMs);
  track->index = index;

  if (front) {
    preloadedTracks.push_front(track);
  } else {
    preloadedTracks.push_back(track);
  }
  return true;
}

bool TrackQueue::requeueCurrent(uint32_t positionMs) {
  // in case we re-queue current track, make sure position is updated (0)
  if (preloadedTracks.size() && currentTracksIndex >= 0 &&
      static_cast<size_t>(currentTracksIndex) < currentTracks.size() &&
      preloadedTracks[0]->ref == currentTracks[currentTracksIndex]) {
    preloadedTracks.pop_front();
  }

  return queueTrackAt(currentTracksIndex, true, positionMs);
}

bool TrackQueue::queueFollowing() {
  if (preloadedTracks.empty()) {
    return queueTrackAt(currentTracksIndex, false);
  }

  return queueTrackAt(followingIndex(preloadedTracks.back()->index), false);
}

void TrackQueue::refreshPreloaded(bool keepPlaying) {
  size_t keep = 1;

  while (keep < preloadedTracks.size() && keep < MAX_TRACKS_PRELOAD &&
         ((keepPlaying && preloadedTracks[keep]->loading) ||
          preloadedTracks[keep]->index ==
              followingIndex(preloadedTracks[keep - 1]->index))) {
    keep++;
  }

  if (keep < preloadedTracks.size()) {
    preloadedTracks.erase(preloadedTracks.begin() + keep,
                          preloadedTracks.end());
  }

  if (!preloadedTracks.empty() &&
      preloadedTracks.size() < MAX_TRACKS_PRELOAD) {
    queueFollowing();
  }
}

void TrackQueue::setRepeat(RepeatMode mode) {
  {
    std::scoped_lock lock(tracksMutex);

    if (mode == repeatMode) {
      return;
    }
    repeatMode = mode;
    refreshPreloaded(true);
  }

  // The player may wait at the end of the queue, which now goes on
  playableSemaphore->give();
  processSemaphore->give();
}

TrackQueue::RepeatMode TrackQueue::getRepeat() {
  std::scoped_lock lock(tracksMutex);

  return repeatMode;
}

bool TrackQueue::skipTrack(SkipDirection dir, bool expectNotify) {
  bool skipped = true;
  std::scoped_lock lock(tracksMutex);

  if (dir == SkipDirection::PREV) {
    uint64_t position =
        !playbackState->innerFrame.state.has_position_ms
            ? 0
            : playbackState->innerFrame.state.position_ms +
                  ctx->timeProvider->getSyncedTimestamp() -
                  playbackState->innerFrame.state.position_measured_at;
    int previous = precedingIndex(currentTracksIndex);

    if (previous >= 0 && position < 3000) {
      queueTrackAt(previous, true);
      currentTracksIndex = previous;
    } else {
      requeueCurrent();
    }

    // What was preloaded followed the track before; the player restarts
    refreshPreloaded(false);
  } else if (expectNotify) {
    // Skipped by the user: the player restarts from the head of the queue
    int next = followingIndex(currentTracksIndex);

    if (next < 0) {
      skipped = false;
    } else {
      if (preloadedTracks.size() > 1 && preloadedTracks[1]->index == next) {
        preloadedTracks.pop_front();
      } else {
        preloadedTracks.clear();
        queueTrackAt(next, false);
      }
      currentTracksIndex = next;
      refreshPreloaded(false);
    }
  } else {
    // The player went on to the next preloaded track
    int next = preloadedTracks.size() > 1
                   ? preloadedTracks[1]->index
                   : followingIndex(currentTracksIndex);

    if (next < 0) {
      skipped = false;
    } else {
      if (!preloadedTracks.empty()) {
        preloadedTracks.pop_front();
      }
      if (preloadedTracks.empty() && !queueTrackAt(next, false)) {
        CSPOT_LOG(info, "Failed to queue next track");
      }
      currentTracksIndex = next;

      if (preloadedTracks.size() < MAX_TRACKS_PRELOAD) {
        queueFollowing();
      }
    }
  }

  if (skipped) {
    // Update frame data
    playbackState->innerFrame.state.playing_track_index = currentTracksIndex;

    if (expectNotify) {
      // Reset position to zero
      notifyPending = true;
    }
  }

  return skipped;
}

size_t TrackQueue::getTrackCount() {
  std::scoped_lock lock(tracksMutex);

  return currentTracks.size();
}

int TrackQueue::getCurrentIndex() {
  std::scoped_lock lock(tracksMutex);

  return currentTracksIndex;
}

bool TrackQueue::requestTrackInfo(int index, TrackInfoCallback callback) {
  TrackReference ref;
  {
    std::scoped_lock lock(tracksMutex);

    if (index < 0 || static_cast<size_t>(index) >= currentTracks.size()) {
      return false;
    }
    ref = currentTracks[index];
  }

  bool isTrack = ref.type == TrackReference::Type::TRACK;
  std::string requestUrl =
      string_format("hm://metadata/3/%s/%s", isTrack ? "track" : "episode",
                    bytesToHexString(ref.gid).c_str());

  auto responseHandler = [gid = ref.gid, isTrack,
                          callback](MercurySession::Response& res) {
    TrackInfo info = {};
    bool valid = false;

    if (!res.fail && res.parts.size() > 0) {
      if (isTrack) {
        Track pbTrack = Track_init_zero;

        pbDecode(pbTrack, Track_fields, res.parts[0]);
        valid = pbTrack.name != nullptr;
        if (valid) {
          info.loadPbTrack(&pbTrack, gid);
        }
        pb_release(Track_fields, &pbTrack);
      } else {
        Episode pbEpisode = Episode_init_zero;

        pbDecode(pbEpisode, Episode_fields, res.parts[0]);
        valid = pbEpisode.name != nullptr;
        if (valid) {
          info.loadPbEpisode(&pbEpisode, gid);
        }
        pb_release(Episode_fields, &pbEpisode);
      }
    }
    callback(valid ? &info : nullptr);
  };

  ctx->session->execute(MercurySession::RequestType::GET, requestUrl,
                        responseHandler);
  return true;
}

bool TrackQueue::jumpTo(int index) {
  std::scoped_lock lock(tracksMutex);

  if (index < 0 || static_cast<size_t>(index) >= currentTracks.size()) {
    return false;
  }

  currentTracksIndex = index;
  preloadedTracks.clear();
  queueTrackAt(index, false);

  // Update frame data
  playbackState->innerFrame.state.playing_track_index = currentTracksIndex;
  notifyPending = true;
  return true;
}

bool TrackQueue::hasTracks() {
  std::scoped_lock lock(tracksMutex);

  return currentTracks.size() > 0;
}

bool TrackQueue::isFinished() {
  std::scoped_lock lock(tracksMutex);

  // A repeating queue goes on after the last track
  if (repeatMode != RepeatMode::OFF && !currentTracks.empty()) {
    return false;
  }
  return static_cast<size_t>(currentTracksIndex) >= currentTracks.size() - 1;
}

bool TrackQueue::updateTracks(uint32_t requestedPosition, bool initial) {
  std::scoped_lock lock(tracksMutex);
  bool cleared = true;

  // Copy requested track list
  currentTracks = playbackState->remoteTracks;
  currentTracksIndex = playbackState->innerFrame.state.playing_track_index;

  if (initial) {
    // Clear preloaded tracks
    preloadedTracks.clear();

    if (static_cast<size_t>(currentTracksIndex) < currentTracks.size()) {
      // Push a song on the preloaded queue
      queueTrackAt(currentTracksIndex, true, requestedPosition);
    }

    // We already updated track meta, mark it
    notifyPending = true;

    playableSemaphore->give();
  } else if (!preloadedTracks.empty() && preloadedTracks[0]->loading) {
    // try to not re-load track if we are still loading it

    // remove everything except first track
    preloadedTracks.erase(preloadedTracks.begin() + 1, preloadedTracks.end());
    preloadedTracks[0]->index = currentTracksIndex;

    // Push a song on the preloaded queue
    CSPOT_LOG(info, "Keeping current track %d", currentTracksIndex);
    queueFollowing();

    cleared = false;
  } else {
    // Clear preloaded tracks
    preloadedTracks.clear();

    // Push a song on the preloaded queue
    CSPOT_LOG(info, "Re-loading current track");
    queueTrackAt(currentTracksIndex, true, requestedPosition);
  }

  return cleared;
}
