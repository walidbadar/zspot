/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * "Your Library" and the search: lists the user's Liked Songs and playlists
 * through the Spotify Web API, the tracks of the one that is picked, or the
 * songs that match a search, and starts playback of a track on this device. The listings are shown with
 * ui_list_reset() / ui_list_add(); the functions below are the matching
 * struct ui_ops hooks and only queue work for the library thread.
 */

#ifndef ZSPOT_SAMPLE_LIBRARY_H_
#define ZSPOT_SAMPLE_LIBRARY_H_

#include <stdbool.h>
#include <stdint.h>

/** Starts the library thread. Call after ui_init() succeeded. */
int library_init(void);

/** Loads and shows the top level: Liked Songs and the playlists. */
void library_open(void);

/** Returns from a track listing to the top level without reloading it. */
void library_back(void);

/**
 * Row @p index of the current listing was picked: opens that collection, or
 * plays that track of the open collection.
 */
void library_select(int index);

/** Searches for songs and lists the hits (UI_LIST_SEARCH). */
void library_search(const char *query);

/** Plays the hit @p index of the last search, followed by the hits after it. */
void library_search_select(int index);

/** Reports from the library thread whether Spotify accepted the request. */
typedef void (*library_play_cb_t)(bool accepted);

/**
 * Plays a track on this device, from @p position_ms on.
 *
 * @param track_uri    "spotify:track:..." or "spotify:episode:..."
 * @param context_uri  the playlist or album to go on with after the track,
 *                     or empty; without one, or when the track is no longer
 *                     part of it, the track is played alone
 * @return 0 when the request is on its way and @p done will be called;
 *         -EBUSY while an earlier one is, or another negative errno
 */
int library_play(const char *track_uri, const char *context_uri, uint32_t position_ms,
		 library_play_cb_t done);

#endif /* ZSPOT_SAMPLE_LIBRARY_H_ */
