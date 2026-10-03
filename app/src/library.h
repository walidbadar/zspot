/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * "Your Library": lists the user's Liked Songs and playlists through the
 * Spotify Web API, lists the tracks of the one that is picked and starts
 * playback of a track on this device. The listings are shown with
 * ui_list_reset() / ui_list_add(); the functions below are the matching
 * struct ui_ops hooks and only queue work for the library thread.
 */

#ifndef ZSPOT_SAMPLE_LIBRARY_H_
#define ZSPOT_SAMPLE_LIBRARY_H_

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

#endif /* ZSPOT_SAMPLE_LIBRARY_H_ */
