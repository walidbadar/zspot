/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * "Now Playing" screen (LVGL). The ui_show_*() and ui_set_*() functions only
 * queue an update, so they may be called from any thread, including the zspot
 * callbacks. They do nothing until ui_init() succeeded.
 */

#ifndef ZSPOT_SAMPLE_UI_H_
#define ZSPOT_SAMPLE_UI_H_

#include <stdbool.h>
#include <stdint.h>

/** Player hooks, invoked on the LVGL thread in response to touch input. */
struct ui_ops {
	void (*set_paused)(bool paused);
	void (*next)(void);
	void (*previous)(void);
	void (*seek)(uint32_t position_ms);
	/** @p commit is false while the slider is still being dragged. */
	void (*set_volume)(uint16_t volume, bool commit);
	/** Current playback position, polled while a track is shown. */
	uint32_t (*position_ms)(void);
	/**
	 * The library view was opened: answer with ui_list_reset() and one
	 * ui_list_add() per row.
	 */
	void (*library_open)(void);
	/** Back was tapped on a listing below the top level. */
	void (*library_back)(void);
	/** The row @p index of the current listing was tapped. */
	void (*library_select)(int index);
};

#if defined(CONFIG_ZSPOT_SAMPLE_UI)

/**
 * Builds the screen and turns the display on.
 * @return 0 on success, -ENODEV when there is no usable display, -ENOMEM when
 *         the LVGL memory pool is too small.
 */
int ui_init(const char *device_name, const struct ui_ops *ops);

/** Shows a status message instead of a track, e.g. while nothing is playing. */
void ui_show_message(const char *headline, const char *detail);

/** Shows a track; the cover behind @p image_url (may be NULL) loads in the background. */
void ui_show_track(const char *title, const char *artist, const char *image_url,
		   uint32_t duration_ms);

void ui_set_paused(bool paused);

/** @param volume 0..65535 */
void ui_set_volume(uint16_t volume);

/** Most rows the library view shows */
#define UI_LIST_MAX 30

/**
 * Starts a new listing in the library view.
 *
 * @param generation  tags the listing; rows added with another value are
 *                    ignored, so a superseded listing cannot leak into it
 * @param heading     shown in the top bar
 * @param top_level   true for the list of collections: back then closes the
 *                    view, and tapping a row keeps it open. On other
 *                    listings a tap returns to the Now Playing screen.
 * @param status      shown instead of rows, e.g. while loading; NULL for none
 */
void ui_list_reset(uint32_t generation, const char *heading, bool top_level, const char *status);

/**
 * Appends a row to the listing @p generation; rows are added in order.
 * @param duration_ms  shown at the right edge, 0 for none
 */
void ui_list_add(uint32_t generation, const char *title, const char *subtitle,
		 uint32_t duration_ms);

#else

static inline void ui_show_message(const char *headline, const char *detail)
{
}

static inline void ui_show_track(const char *title, const char *artist, const char *image_url,
				 uint32_t duration_ms)
{
}

static inline void ui_set_paused(bool paused)
{
}

static inline void ui_set_volume(uint16_t volume)
{
}

#endif /* CONFIG_ZSPOT_SAMPLE_UI */

#endif /* ZSPOT_SAMPLE_UI_H_ */
