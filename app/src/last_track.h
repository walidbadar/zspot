/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * The last song (CONFIG_ZSPOT_LAST_TRACK): what the Now Playing screen shows
 * after a restart, and what is needed to play it again on this device. Kept
 * in the settings.
 *
 * Storing writes to the flash, which stops the whole system for as long as
 * an erase takes: do that only while no audio goes out.
 */

#ifndef ZSPOT_SAMPLE_LAST_TRACK_H_
#define ZSPOT_SAMPLE_LAST_TRACK_H_

#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

/** "spotify:episode:" and the 22 digits of an id, with the terminator */
#define LAST_TRACK_URI_MAX     40
/** "spotify:playlist:" and an id; longer contexts are not kept */
#define LAST_TRACK_CONTEXT_MAX 64
/** As long as the cover loader takes them */
#define LAST_TRACK_IMAGE_MAX   128

/** Stored as it is: last_track_set() leaves no stray bytes behind the texts. */
struct last_track {
	/** Tells the layout; set by last_track_set() */
	uint32_t format;
	char title[UI_TEXT_MAX];
	char artist[UI_TEXT_MAX];
	char image_url[LAST_TRACK_IMAGE_MAX];
	/** "spotify:track:..." or "spotify:episode:..." */
	char uri[LAST_TRACK_URI_MAX];
	/** What it was played from, e.g. "spotify:playlist:..."; empty without */
	char context[LAST_TRACK_CONTEXT_MAX];
	uint32_t duration_ms;
	/** Where it was paused; 0 while it played */
	uint32_t position_ms;
};

/**
 * Fills in @p track, without a context and at position 0; text that is too
 * long is cut.
 *
 * @return false when @p uri is not the URI of a track or an episode; @p track
 *         is not usable then.
 */
bool last_track_set(struct last_track *track, const char *title, const char *artist,
		    const char *image_url, const char *uri, uint32_t duration_ms);

/**
 * Sets what the track is played from. NULL or a context that cannot be kept
 * leaves it without one, as the track can be played alone.
 */
void last_track_set_context(struct last_track *track, const char *context);

/**
 * Reads the stored track.
 *
 * @return 0 on success, -ENOENT when none is stored, or another negative
 *         errno. A stored track that cannot be used is removed.
 */
int last_track_load(struct last_track *track);

/**
 * Stores @p track, which last_track_set() or last_track_load() filled in.
 * @return 0 on success or a negative errno.
 */
int last_track_store(const struct last_track *track);

/** Removes the stored track. */
int last_track_forget(void);

#endif /* ZSPOT_SAMPLE_LAST_TRACK_H_ */
