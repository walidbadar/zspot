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
