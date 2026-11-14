/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * The last song across restarts, wherever there are settings: keeps what plays
 * in the settings, shows it paused on the Now Playing screen after a restart
 * instead of a notice, and starts it on this device when Play is tapped. The
 * song is started through the Web API, as a restarted device has no queue to
 * play.
 *
 * Nothing here needs the screen: without one the ui_*() calls do nothing,
 * and track_pause() and track_seek() are for whatever stands in for its
 * buttons.
 *
 * The song is stored when no audio goes out (it is paused, it just started,
 * the queue ended, another device took over), because writing to the flash
 * stops the system for longer than the output can go on by itself. A song
 * that began without a break in the audio is stored at the next such moment.
 */

#ifndef ZSPOT_SAMPLE_TRACK_H_
#define ZSPOT_SAMPLE_TRACK_H_

#include <stdbool.h>
#include <stdint.h>

#include <zspot/zspot.h>

#if defined(CONFIG_SETTINGS)

/* --- What plays: from the zspot event callback ----------------------------- */

/** The queue changed (ZSPOT_EVENT_QUEUE_CHANGED): what it plays from. */
void track_set_context(const char *context_uri);

/**
 * A track began (ZSPOT_EVENT_TRACK_INFO).
 *
 * @param position_ms  where it starts
 * @param idle         no audio goes out at the moment
 */
void track_set(const struct zspot_track_info *track, uint32_t position_ms, bool idle);

/**
 * The output stopped at @p position_ms: the track was paused, moved while
 * paused, or went on to play on another device.
 */
void track_set_position(uint32_t position_ms);

/** Something was started: the song shown after the restart is not waiting any more. */
void track_playback_begun(void);

/* --- After a restart ---------------------------------------------------- */

/**
 * Shows the last song, paused, unless something plays already.
 *
 * @param set_position  takes the position the song was paused at, now and
 *                      whenever track_seek() changes it
 * @return false when there is no song to show, or no way to start it
 */
bool track_show(void (*set_position)(uint32_t position_ms));

/** Whether the song of track_show() still waits to be started. */
bool track_waiting(void);

/**
 * Play or pause was tapped. Starts the waiting song.
 *
 * @return false when no song waits: the tap is for the player
 */
bool track_pause(bool pause);

/**
 * The progress slider was moved: the waiting song starts from there.
 *
 * @return false when no song waits: the seek is for the player
 */
bool track_seek(uint32_t position_ms);

#else

static inline void track_set_context(const char *context_uri)
{
}

static inline void track_set(const struct zspot_track_info *track, uint32_t position_ms, bool idle)
{
}

static inline void track_set_position(uint32_t position_ms)
{
}

static inline void track_playback_begun(void)
{
}

static inline bool track_show(void (*set_position)(uint32_t position_ms))
{
	return false;
}

static inline bool track_waiting(void)
{
	return false;
}

static inline bool track_pause(bool pause)
{
	return false;
}

static inline bool track_seek(uint32_t position_ms)
{
	return false;
}

#endif /* CONFIG_SETTINGS */

#endif /* ZSPOT_SAMPLE_TRACK_H_ */
