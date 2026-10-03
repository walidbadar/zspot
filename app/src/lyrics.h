/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Lyrics from LRCLIB (https://lrclib.net), a free service that needs no
 * account. They are looked up by title, artist and duration on a thread of
 * their own and handed to the UI with ui_show_lyrics() or, when there are
 * none, ui_set_lyrics_status().
 */

#ifndef ZSPOT_SAMPLE_LYRICS_H_
#define ZSPOT_SAMPLE_LYRICS_H_

#include <stdint.h>

/** Starts the lyrics thread. Call after ui_init() succeeded. */
int lyrics_init(void);

/**
 * Looks the lyrics of a track up; matches struct ui_ops.lyrics_request. A
 * lookup that is still queued or running is superseded.
 */
void lyrics_request(const char *title, const char *artist, uint32_t duration_ms);

#endif /* ZSPOT_SAMPLE_LYRICS_H_ */
