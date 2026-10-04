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

/** Longest title or artist kept by the UI, including the terminator */
#define UI_TEXT_MAX 96

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
	/**
	 * The Wi-Fi settings were opened: answer with ui_list_reset() and one
	 * ui_list_add() per network found, for UI_LIST_WIFI.
	 */
	void (*wifi_scan)(void);
	/** A network was picked and its password entered (empty for an open one). */
	void (*wifi_connect)(const char *ssid, const char *password);
	/** A network of the Wi-Fi settings was held: forget it when it is a stored one. */
	void (*wifi_forget)(const char *ssid);
	/**
	 * The lyrics of the current track are wanted (the lyrics button is on):
	 * answer with ui_show_lyrics() or ui_set_lyrics_status().
	 */
	void (*lyrics_request)(const char *title, const char *artist, uint32_t duration_ms);
	/**
	 * A search was entered: answer with ui_list_reset() and one
	 * ui_list_add() per hit, for UI_LIST_SEARCH.
	 */
	void (*search)(const char *query);
	/** The hit @p index of the search results was tapped. */
	void (*search_select)(int index);
};

struct ui_lyrics_line {
	/** Start of the line within the track; 0 when the lyrics are not synced */
	uint32_t time_ms;
	const char *text;
};

/** Lyrics of one track, allocated from the LVGL pool in a single block. */
struct ui_lyrics {
	/** Title of the track they belong to: lyrics of another one are dropped */
	char title[UI_TEXT_MAX];
	/** The lines carry time stamps and are highlighted as the track plays */
	bool synced;
	uint16_t count;
	struct ui_lyrics_line lines[];
};

#if defined(CONFIG_ZSPOT_UI)

/**
 * Builds the screen and turns the display on.
 * @return 0 on success, -ENODEV when there is no usable display, -ENOMEM when
 *         the LVGL memory pool is too small.
 */
int ui_init(const struct ui_ops *ops);

/** Shows a status message instead of a track, e.g. while nothing is playing. */
void ui_show_message(const char *headline, const char *detail);

/** Shows a track; the cover behind @p image_url (may be NULL) loads in the background. */
void ui_show_track(const char *title, const char *artist, const char *image_url,
		   uint32_t duration_ms);

void ui_set_paused(bool paused);

/** @param volume 0..65535 */
void ui_set_volume(uint16_t volume);

/**
 * Updates the network indicator in the top right corner. Holding it for one
 * second opens the Wi-Fi settings.
 */
void ui_set_network(bool connected);

/**
 * Shows the battery charge next to the network indicator.
 *
 * @param percent 0..100, or negative to hide the indicator
 */
void ui_set_battery(int percent);

/**
 * Shows lyrics in place of the cover while the lyrics button is on. On success the UI owns @p lyrics and
 * releases it with lv_free(); on failure (false) it stays with the caller.
 */
bool ui_show_lyrics(struct ui_lyrics *lyrics);

/** Shows a notice instead of the lyrics of the track @p title, e.g. that there are none. */
void ui_set_lyrics_status(const char *title, const char *status);

/** The listings of the list view; only the one that is open accepts content. */
enum ui_list {
	UI_LIST_LIBRARY, /**< "Your Library", opened with the list button */
	UI_LIST_WIFI,    /**< Networks found, opened from the network indicator */
	UI_LIST_SEARCH,  /**< Hits of a search entered in the search bar */
};

/** Most rows the library view shows */
#define UI_LIST_MAX 30

/**
 * Starts a new listing in the list view.
 *
 * @param generation  tags the listing; rows added with another value are
 *                    ignored, so a superseded listing cannot leak into it
 * @param heading     shown in the top bar
 * @param top_level   true for the list of collections: back then closes the
 *                    view, and tapping a row keeps it open. On other
 *                    listings a tap returns to the Now Playing screen.
 * @param status      shown instead of rows, e.g. while loading; NULL for none
 */
void ui_list_reset(enum ui_list list, uint32_t generation, const char *heading, bool top_level,
		   const char *status);

/**
 * Appends a row to the listing @p generation; rows are added in order.
 * @param duration_ms  shown at the right edge, 0 for none
 */
void ui_list_add(enum ui_list list, uint32_t generation, const char *title, const char *subtitle,
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

static inline void ui_set_network(bool connected)
{
}

static inline void ui_set_battery(int percent)
{
}

#endif /* CONFIG_ZSPOT_UI */

#endif /* ZSPOT_SAMPLE_UI_H_ */
