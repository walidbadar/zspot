/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * "Now Playing" screen, laid out for the resolution of the display, plus a list view for
 * browsing the user's library, search results and Wi-Fi networks, and a text
 * entry screen with a keyboard for the search and the Wi-Fi password.
 *
 * LVGL runs on its own workqueue (CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE). Updates
 * from other threads go through a message queue that an LVGL timer drains:
 * the zspot callbacks run with protocol locks held, and the touch handlers
 * call back into zspot from the LVGL thread, so taking the LVGL lock in the
 * callbacks could deadlock.
 */

#include "ui.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "cover.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define COLOR_BG      lv_color_hex(0x121212)
#define COLOR_SURFACE lv_color_hex(0x282828)
#define COLOR_TEXT    lv_color_hex(0xFFFFFF)
#define COLOR_SUBTLE  lv_color_hex(0xB3B3B3)
#define COLOR_TRACK   lv_color_hex(0x4D4D4D)
#define COLOR_ACCENT  lv_color_hex(0x1DB954)
#define COLOR_ERROR   lv_color_hex(0xE22134)

/*
 * The sizes below are those of the 320x480 design (480x320 in landscape).
 * sc() scales them up for a larger display, together with the fonts; on a
 * smaller one they stay and the cover shrinks. See compute_layout().
 */
#define TOP_H        sc(36) /* search bar and corner buttons */
#define SEARCH_BAR_H sc(26) /* ends 5 px above the content below */
#define GAP          sc(12) /* between the cover and the text below or beside it */
/* Title, artist, progress, times, transport buttons and the volume row */
#define INFO_H       sc(176)
/* Offsets of those within the block */
#define TITLE_DY     0
#define ARTIST_DY    sc(28)
#define PROGRESS_DY  sc(62)
#define TIME_DY      sc(74)
#define CONTROLS_DY  sc(116) /* centre line of the transport buttons */
#define VOLUME_DY    sc(162)

#define LYRICS_PAD   sc(16) /* around the lines of the lyrics panel */

#define BAR_H        sc(48) /* top bar of the library view */
#define ROW_H        sc(56)
/* Entry screen: hint, text field and button below the top bar */
#define ENTRY_FORM_H sc(150)
#define KEYBOARD_H_MIN sc(120)
#define KEYBOARD_H_MAX sc(240)

/* From these scales on larger fonts are used, when they are built in */
#define MEDIUM_FONT_SCALE 135 /* one and a half times the size */
#define LARGE_FONT_SCALE  175 /* twice the size */

/* Placement for the resolution of the display */
static struct {
	int32_t w;
	int32_t h;
	/* Size of the display relative to the design, in percent; at least 100 */
	int32_t scale;
	int32_t margin;
	/* Cover (and lyrics panel): a square */
	int32_t cover_x;
	int32_t cover_y;
	int32_t cover_size;
	/* Column with the text and the controls: below the cover, or beside it */
	int32_t info_x;
	int32_t info_y;
	int32_t info_w;
	/* Entry screen */
	int32_t keyboard_h;
	bool entry_button;
} lay;

/* Fonts for the scale: hints and times, text, title, large symbols */
static const lv_font_t *font_small = &lv_font_montserrat_12;
static const lv_font_t *font_text = &lv_font_montserrat_14;
static const lv_font_t *font_title = &lv_font_montserrat_20;
static const lv_font_t *font_icon = &lv_font_montserrat_28;

/* A size of the design in pixels of this display */
static int32_t sc(int32_t px)
{
	return px * lay.scale / 100;
}

static void compute_layout(void)
{
	bool portrait;

	lay.w = lv_display_get_horizontal_resolution(NULL);
	lay.h = lv_display_get_vertical_resolution(NULL);
	portrait = lay.w <= lay.h;

	lay.scale = portrait ? MIN(lay.w * 100 / 320, lay.h * 100 / 480)
			     : MIN(lay.w * 100 / 480, lay.h * 100 / 320);
	lay.scale = MAX(lay.scale, 100);
#if defined(CONFIG_LV_FONT_MONTSERRAT_18) && defined(CONFIG_LV_FONT_MONTSERRAT_30) &&             \
	defined(CONFIG_LV_FONT_MONTSERRAT_40)
	if (lay.scale >= MEDIUM_FONT_SCALE) {
		font_small = &lv_font_montserrat_18;
		font_text = &lv_font_montserrat_20;
		font_title = &lv_font_montserrat_30;
		font_icon = &lv_font_montserrat_40;
	}
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_24) && defined(CONFIG_LV_FONT_MONTSERRAT_40) &&             \
	defined(CONFIG_LV_FONT_MONTSERRAT_48)
	if (lay.scale >= LARGE_FONT_SCALE) {
		font_small = &lv_font_montserrat_24;
		font_text = &lv_font_montserrat_28;
		font_title = &lv_font_montserrat_40;
		font_icon = &lv_font_montserrat_48;
	}
#endif
	lay.margin = sc(lay.w >= 300 ? 24 : 12);

	if (portrait) {
		/* Portrait: the cover fills what the text block leaves above it. */
		int32_t space = lay.h - TOP_H - GAP - INFO_H;

		lay.info_x = lay.margin;
		lay.info_w = lay.w - 2 * lay.margin;
		lay.info_y = lay.h - INFO_H;
		lay.cover_size = MAX(MIN(lay.w - 2 * (lay.margin + sc(8)), space), 0);
		lay.cover_x = (lay.w - lay.cover_size) / 2;
		lay.cover_y = TOP_H + (space - lay.cover_size) / 2;
	} else {
		/* Landscape: the cover on the left, the text block beside it. */
		int32_t space = lay.h - TOP_H - GAP;

		lay.cover_size = MAX(MIN(space, lay.w / 2 - lay.margin - GAP), 0);
		lay.cover_x = lay.margin;
		lay.cover_y = TOP_H + (space - lay.cover_size) / 2;
		lay.info_x = lay.cover_x + lay.cover_size + lay.margin;
		lay.info_w = lay.w - lay.info_x - lay.margin;
		lay.info_y = TOP_H + MAX((lay.h - TOP_H - INFO_H) / 2, 0);
	}

	/* The keyboard takes what the entry form leaves; on a low display the
	 * button goes, the keyboard's confirm key does the same.
	 */
	lay.entry_button = lay.h >= BAR_H + ENTRY_FORM_H + KEYBOARD_H_MIN;
	lay.keyboard_h = CLAMP(lay.h - BAR_H - (lay.entry_button ? ENTRY_FORM_H : sc(96)),
			       KEYBOARD_H_MIN, KEYBOARD_H_MAX);
}

#define TICK_MS          50
#define POSITION_TICKS   5 /* progress refresh: every 250 ms */

/* How long the network indicator has to be held to open the Wi-Fi settings */
#define WIFI_HOLD_MS     1000
#define WIFI_FORGET_HOLD_MS 1000
#define WIFI_SSID_MAX    32
/* The battery indicator turns red at this charge level */
#define BATTERY_LOW_PERCENT 15
/* Longest Wi-Fi password, and search text */
#define ENTRY_TEXT_MAX   64

enum ui_msg_type {
	UI_MSG_MESSAGE,
	UI_MSG_TRACK,
	UI_MSG_PAUSED,
	UI_MSG_VOLUME,
	UI_MSG_NETWORK,
	UI_MSG_BATTERY,
	UI_MSG_LYRICS,
	UI_MSG_LYRICS_STATUS,
	UI_MSG_LIST_RESET,
	UI_MSG_LIST_ROW,
};

struct ui_msg {
	enum ui_msg_type type;
	union {
		struct {
			char headline[64];
			char detail[96];
		} message;
		struct {
			char title[UI_TEXT_MAX];
			char artist[UI_TEXT_MAX];
			char image_url[128];
			uint32_t duration_ms;
		} track;
		struct {
			enum ui_list list;
			uint32_t generation;
			bool top_level;
			char heading[64];
			char status[64];
		} list_reset;
		struct {
			enum ui_list list;
			uint32_t generation;
			char title[96];
			char subtitle[96];
			uint32_t duration_ms;
		} list_row;
		struct ui_lyrics *lyrics;
		struct {
			char title[UI_TEXT_MAX];
			char status[64];
		} lyrics_status;
		bool paused;
		bool connected;
		int battery_percent;
		uint16_t volume;
	};
};

/* Room for a complete listing arriving at once */
#define UI_MSGQ_LEN (UI_LIST_MAX + 12)

/* Microphone, the lyrics button: LVGL's symbol fonts have none. 20x20, alpha only */
#define MIC_ICON_SIZE 20

static const uint8_t mic_icon_map[MIC_ICON_SIZE * MIC_ICON_SIZE] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x63,
	0x6e, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x27, 0xc5, 0xff,
	0xff, 0xe0, 0x4a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0xff, 0xff,
	0xff, 0xff, 0xbc, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xb4, 0xff, 0xff,
	0xff, 0xff, 0xdf, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xb7, 0xff, 0xff,
	0xff, 0xff, 0xe1, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xb6, 0xff, 0xff,
	0xff, 0xff, 0xe1, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xb6, 0xff, 0xff,
	0xff, 0xff, 0xe1, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x13, 0x0a, 0x05, 0xb6, 0xff, 0xff,
	0xff, 0xff, 0xe1, 0x1d, 0x06, 0x1e, 0x0f, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x33, 0xca, 0x6b, 0x00, 0x9a, 0xff, 0xff,
	0xff, 0xff, 0xcb, 0x0d, 0x41, 0xdf, 0x68, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x27, 0xec, 0xa9, 0x00, 0x3f, 0xe4, 0xff,
	0xff, 0xf7, 0x6b, 0x00, 0x71, 0xfe, 0x54, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x08, 0xb2, 0xec, 0x39, 0x00, 0x3f, 0x9a,
	0xa6, 0x59, 0x04, 0x17, 0xca, 0xde, 0x20, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x49, 0xf2, 0xc7, 0x2e, 0x00, 0x00,
	0x00, 0x00, 0x17, 0xa0, 0xfe, 0x79, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x03, 0x73, 0xf7, 0xdf, 0x84, 0x4d,
	0x49, 0x70, 0xca, 0xfe, 0x9f, 0x0f, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0xcb, 0xf9, 0xfb,
	0xfa, 0xfb, 0xde, 0x79, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x48, 0xd2,
	0xef, 0x61, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x1c, 0x1f, 0xbf,
	0xe8, 0x35, 0x1b, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0xe1, 0xe6, 0xfb,
	0xff, 0xe9, 0xe8, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0xb3, 0xb7, 0xb5,
	0xb5, 0xb7, 0xba, 0x5b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Magnifier of the search bar, 16x16, alpha only */
#define SEARCH_ICON_SIZE 16

static const uint8_t search_icon_map[SEARCH_ICON_SIZE * SEARCH_ICON_SIZE] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0x05,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x15, 0x67, 0xa9, 0xb7, 0xb1,
	0x7c, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x2b, 0xbc, 0xff, 0xfe, 0xee, 0xf9,
	0xff, 0xd8, 0x4b, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x15, 0xbc, 0xff, 0xbb, 0x53, 0x2d, 0x43,
	0x9d, 0xfb, 0xe0, 0x34, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x67, 0xff, 0xbb, 0x18, 0x00, 0x00, 0x00,
	0x06, 0x8e, 0xff, 0x9d, 0x03, 0x00, 0x00, 0x00,
	0x03, 0xa9, 0xfe, 0x53, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x27, 0xe5, 0xda, 0x18, 0x00, 0x00, 0x00,
	0x06, 0xb7, 0xee, 0x2d, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0d, 0xc7, 0xe5, 0x1e, 0x00, 0x00, 0x00,
	0x05, 0xb1, 0xf9, 0x43, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x1b, 0xdb, 0xe0, 0x1c, 0x00, 0x00, 0x00,
	0x00, 0x7c, 0xff, 0x9d, 0x06, 0x00, 0x00, 0x00,
	0x00, 0x6c, 0xfe, 0xb0, 0x08, 0x00, 0x00, 0x00,
	0x00, 0x26, 0xd8, 0xfb, 0x8e, 0x27, 0x0d, 0x1b,
	0x6c, 0xe8, 0xff, 0x6d, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x4b, 0xe0, 0xff, 0xe5, 0xc7, 0xdb,
	0xfe, 0xff, 0xff, 0xce, 0x34, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x34, 0x9d, 0xda, 0xe8, 0xe1,
	0xb0, 0x6d, 0xce, 0xff, 0xd0, 0x34, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x03, 0x18, 0x24, 0x1d,
	0x08, 0x00, 0x34, 0xd0, 0xff, 0xd0, 0x34, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x34, 0xd0, 0xff, 0xcf, 0x2f,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x34, 0xcf, 0xbf, 0x25,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x2f, 0x25, 0x00,
};

static const lv_image_dsc_t search_icon = {
	.header = {
		.magic = LV_IMAGE_HEADER_MAGIC,
		.cf = LV_COLOR_FORMAT_A8,
		.w = SEARCH_ICON_SIZE,
		.h = SEARCH_ICON_SIZE,
		.stride = SEARCH_ICON_SIZE,
	},
	.data_size = sizeof(search_icon_map),
	.data = search_icon_map,
};

static const lv_image_dsc_t mic_icon = {
	.header = {
		.magic = LV_IMAGE_HEADER_MAGIC,
		.cf = LV_COLOR_FORMAT_A8,
		.w = MIC_ICON_SIZE,
		.h = MIC_ICON_SIZE,
		.stride = MIC_ICON_SIZE,
	},
	.data_size = sizeof(mic_icon_map),
	.data = mic_icon_map,
};

/* Buffer allocated from the LVGL pool, like everything else of the UI */
static struct k_msgq ui_msgq;

static const struct ui_ops *ops;
static bool ready;

/* Screen timeout: covers the screen while it is off and takes the waking touch */
static lv_obj_t *wake_overlay;
static bool screen_off;

static lv_obj_t *main_screen;
static lv_obj_t *network_icon;
static lv_obj_t *battery_icon;
static lv_obj_t *cover_image;
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *progress_slider;
static lv_obj_t *elapsed_label;
static lv_obj_t *duration_label;
static lv_obj_t *prev_button;
static lv_obj_t *play_button;
static lv_obj_t *play_icon;
static lv_obj_t *next_button;
static lv_obj_t *volume_slider;
static lv_obj_t *volume_icon;
/* The volume before the speaker was tapped to mute, restored by the next tap */
static uint16_t volume_unmuted = UINT16_MAX;

static lv_obj_t *list_screen;
static lv_obj_t *list_heading;
static lv_obj_t *list_rows;
static lv_obj_t *list_status;
static enum ui_list list_shown;
static uint32_t list_generation;
static bool list_top_level;

/* Text entry screen: the Wi-Fi password, or the search when entry_search */
static lv_obj_t *entry_screen;
static lv_obj_t *entry_heading;
static lv_obj_t *entry_hint;
static lv_obj_t *entry_text;
static lv_obj_t *entry_action;
static bool entry_search;
static lv_timer_t *wifi_hold_timer;
static char wifi_ssid[WIFI_SSID_MAX + 1];

/* Lyrics panel, shown over the cover while the lyrics button is on */
static lv_obj_t *lyrics_panel;
static lv_obj_t *lyrics_icon;
static struct ui_lyrics *lyrics;
static int lyrics_line;       /* highlighted line, -1 for none */
static bool lyrics_open;
static bool lyrics_requested; /* for the track on screen */

/* Track on screen */
static char track_title[UI_TEXT_MAX];
static char track_artist[UI_TEXT_MAX];
static uint32_t track_duration_ms;

static lv_draw_buf_t *cover_buf;
static char cover_url[sizeof(((struct ui_msg *)0)->track.image_url)];
static bool track_shown;
static bool paused = true;

/* Copies as much of @p src as fits without cutting a UTF-8 sequence in half. */
static void copy_text(char *dst, size_t size, const char *src)
{
	size_t len = src != NULL ? strlen(src) : 0;

	if (len >= size) {
		len = size - 1;
		while (len > 0 && ((uint8_t)src[len] & 0xC0) == 0x80) {
			len--;
		}
	}
	if (len > 0) {
		memcpy(dst, src, len);
	}
	dst[len] = '\0';
}

static void set_time(lv_obj_t *label, uint32_t ms)
{
	lv_label_set_text_fmt(label, "%u:%02u", ms / 60000, (ms / 1000) % 60);
}

static void set_backdrop(lv_color_t color)
{
	lv_obj_set_style_bg_color(main_screen, color, 0);
}

static void drop_cover(void)
{
	lv_obj_add_flag(cover_image, LV_OBJ_FLAG_HIDDEN);
	set_backdrop(COLOR_BG);
	cover_url[0] = '\0';
	cover_request(NULL);
}

/* Called by the cover loader with the LVGL lock held. */
static void on_cover_ready(lv_draw_buf_t *image, lv_color_t accent)
{
	lv_draw_buf_t *old = cover_buf;

	cover_buf = image;
	lv_image_set_src(cover_image, cover_buf);
	lv_obj_remove_flag(cover_image, LV_OBJ_FLAG_HIDDEN);
	set_backdrop(lv_color_mix(accent, COLOR_BG, LV_OPA_60));

	if (old != NULL) {
		lv_draw_buf_destroy(old);
	}
}

static void set_controls_enabled(bool enabled)
{
	lv_obj_t *const controls[] = {progress_slider, prev_button, play_button, next_button};

	ARRAY_FOR_EACH(controls, i) {
		lv_obj_set_state(controls[i], LV_STATE_DISABLED, !enabled);
	}
}

static void update_volume_icon(void)
{
	lv_label_set_text(volume_icon, lv_slider_get_value(volume_slider) == 0
					       ? LV_SYMBOL_MUTE
					       : LV_SYMBOL_VOLUME_MAX);
}

static void apply_paused(void)
{
	lv_label_set_text(play_icon, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

static lv_obj_t *create_box(lv_obj_t *parent, int32_t w, int32_t h);
static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
			      const char *text);

/* Shows the entry screen for a search, or for the password of wifi_ssid. */
static void open_entry(bool search)
{
	entry_search = search;
	lv_label_set_text(entry_heading, search ? "Search" : wifi_ssid);
	lv_label_set_text(entry_hint, search ? "Song or artist"
					     : "Password (leave empty for an open network)");
	lv_label_set_text(entry_action, search ? "Search" : "Connect");
	/* A password shows as bullets; the character typed last stays readable briefly. */
	lv_textarea_set_password_mode(entry_text, !search);
	lv_textarea_set_text(entry_text, "");
	lv_screen_load(entry_screen);
}

static void on_list_row_clicked(lv_event_t *e)
{
	int index = (int)(intptr_t)lv_event_get_user_data(e);

	switch (list_shown) {
	case UI_LIST_WIFI: {
		/* The row's title is the SSID: ask for the password next. */
		lv_obj_t *title = lv_obj_get_child(lv_event_get_current_target_obj(e), 0);

		copy_text(wifi_ssid, sizeof(wifi_ssid), lv_label_get_text(title));
		open_entry(false);
		break;
	}
	case UI_LIST_SEARCH:
		ops->search_select(index);
		lv_screen_load(main_screen);
		break;
	case UI_LIST_LIBRARY:
		ops->library_select(index);
		if (!list_top_level) {
			lv_screen_load(main_screen);
		}
		break;
	}
}

/* A row held for WIFI_FORGET_HOLD_MS without scrolling forgets its network. */
static void on_list_row_held(lv_event_t *e)
{
	static uint32_t pressed_at;
	char ssid[WIFI_SSID_MAX + 1];

	if (list_shown != UI_LIST_WIFI) {
		return;
	}
	if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
		pressed_at = lv_tick_get();
		return;
	}
	/* LVGL repeats the event while the row stays pressed */
	if (lv_tick_elaps(pressed_at) < WIFI_FORGET_HOLD_MS) {
		return;
	}

	/*
	 * The rest of this touch is ignored: it neither picks the network nor
	 * holds a row of the listing that follows
	 */
	lv_indev_wait_release(lv_indev_active());

	/* That listing deletes the row, hence the copy */
	copy_text(ssid, sizeof(ssid),
		  lv_label_get_text(lv_obj_get_child(lv_event_get_current_target_obj(e), 0)));
	ops->wifi_forget(ssid);
}

static void list_reset(uint32_t generation, const char *heading, bool top_level,
		       const char *status)
{
	list_generation = generation;
	list_top_level = top_level;

	lv_label_set_text(list_heading, heading);
	lv_label_set_text(list_status, status);
	lv_obj_clean(list_rows);
	lv_obj_scroll_to_y(list_rows, 0, LV_ANIM_OFF);
}

static void list_add(const char *title, const char *subtitle, uint32_t duration_ms)
{
	int index = lv_obj_get_child_count(list_rows);
	/* Leave room for the duration at the right edge */
	int32_t text_w = lay.w - 2 * lay.margin - (duration_ms > 0 ? sc(44) : 0);
	lv_obj_t *row;
	lv_obj_t *label;

	if (index >= UI_LIST_MAX) {
		return;
	}

	row = create_box(list_rows, lay.w, ROW_H);
	lv_obj_set_pos(row, 0, index * ROW_H);
	lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_color(row, COLOR_SURFACE, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
	lv_obj_add_event_cb(row, on_list_row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)index);
	lv_obj_add_event_cb(row, on_list_row_held, LV_EVENT_PRESSED, NULL);
	lv_obj_add_event_cb(row, on_list_row_held, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

	label = create_label(row, font_text, COLOR_TEXT, title);
	lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_size(label, text_w, lv_font_get_line_height(font_text));
	lv_obj_set_pos(label, lay.margin, sc(10));

	label = create_label(row, font_small, COLOR_SUBTLE, subtitle);
	lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_size(label, text_w, lv_font_get_line_height(font_small));
	lv_obj_set_pos(label, lay.margin, sc(31));

	if (duration_ms > 0) {
		label = create_label(row, font_small, COLOR_SUBTLE, "");
		set_time(label, duration_ms);
		lv_obj_align(label, LV_ALIGN_RIGHT_MID, -lay.margin, 0);
	}
}

/* Replaces the content of the lyrics panel with a notice. */
static void lyrics_set_status(const char *status)
{
	lv_obj_t *label;

	lv_obj_clean(lyrics_panel);
	if (lyrics != NULL) {
		lv_free(lyrics);
		lyrics = NULL;
	}

	label = create_label(lyrics_panel, font_text, COLOR_SUBTLE, status);
	lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_width(label, lv_pct(100));
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	/* Roughly in the middle of the panel */
	lv_obj_set_style_margin_top(label, MAX(lay.cover_size / 2 - sc(40), 0), 0);
}

/* Takes over @p new_lyrics and lists its lines; the labels point into it. */
static void lyrics_set(struct ui_lyrics *new_lyrics)
{
	lyrics_set_status("");
	lv_obj_clean(lyrics_panel);
	lv_obj_scroll_to_y(lyrics_panel, 0, LV_ANIM_OFF);

	lyrics = new_lyrics;
	lyrics_line = -1;
	for (int i = 0; i < lyrics->count; i++) {
		const char *text = lyrics->lines[i].text;
		/* Synced lines start dim and light up when they are sung. */
		lv_obj_t *label = create_label(lyrics_panel, font_title,
					       lyrics->synced ? COLOR_TRACK : COLOR_TEXT, "");

		lv_label_set_text_static(label, text[0] != '\0' ? text : LV_SYMBOL_AUDIO);
		lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
		lv_obj_set_width(label, lv_pct(100));
	}
	lv_obj_update_layout(lyrics_panel);
}

/* Highlights the line that is sung at @p position_ms and centres it. */
static void lyrics_follow(uint32_t position_ms)
{
	int line = -1;
	lv_obj_t *label;

	if (lyrics == NULL || !lyrics->synced) {
		return;
	}
	while (line + 1 < lyrics->count && lyrics->lines[line + 1].time_ms <= position_ms) {
		line++;
	}
	if (line == lyrics_line) {
		return;
	}

	if (lyrics_line >= 0) {
		lv_obj_set_style_text_color(lv_obj_get_child(lyrics_panel, lyrics_line),
					    COLOR_TRACK, 0);
	}
	lyrics_line = line;
	if (line < 0) {
		lv_obj_scroll_to_y(lyrics_panel, 0, LV_ANIM_ON);
		return;
	}

	label = lv_obj_get_child(lyrics_panel, line);
	lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
	/* lv_obj_get_y() is relative to the padded content, whatever the scroll. */
	lv_obj_scroll_to_y(lyrics_panel,
			   MAX(lv_obj_get_y(label) + LYRICS_PAD -
				       (lay.cover_size - lv_obj_get_height(label)) / 2,
			       0),
			   LV_ANIM_ON);
}

static void lyrics_request(void)
{
	lyrics_requested = true;
	lyrics_set_status("Loading lyrics...");
	ops->lyrics_request(track_title, track_artist, track_duration_ms);
}

/* The lyrics button switches between the cover and the lyrics. */
static void on_lyrics_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);

	lyrics_open = !lyrics_open;
	lv_obj_set_flag(lyrics_panel, LV_OBJ_FLAG_HIDDEN, !lyrics_open);
	lv_obj_set_style_image_recolor(lyrics_icon, lyrics_open ? COLOR_ACCENT : COLOR_SUBTLE, 0);
	if (!lyrics_open) {
		return;
	}

	if (!track_shown) {
		lyrics_set_status("Nothing is playing");
	} else if (!lyrics_requested) {
		lyrics_request();
	}
}

static const char *battery_symbol(int percent)
{
	if (percent > 87) {
		return LV_SYMBOL_BATTERY_FULL;
	} else if (percent > 62) {
		return LV_SYMBOL_BATTERY_3;
	} else if (percent > 37) {
		return LV_SYMBOL_BATTERY_2;
	} else if (percent > 12) {
		return LV_SYMBOL_BATTERY_1;
	}
	return LV_SYMBOL_BATTERY_EMPTY;
}

static void apply(const struct ui_msg *msg)
{
	switch (msg->type) {
	case UI_MSG_MESSAGE:
		track_shown = false;
		lyrics_requested = false;
		lyrics_set_status("Nothing is playing");
		lv_label_set_text(title_label, msg->message.headline);
		lv_label_set_text(artist_label, msg->message.detail);
		lv_slider_set_value(progress_slider, 0, LV_ANIM_OFF);
		lv_label_set_text(elapsed_label, "-:--");
		lv_label_set_text(duration_label, "-:--");
		set_controls_enabled(false);
		drop_cover();
		break;
	case UI_MSG_TRACK:
		track_shown = true;
		strcpy(track_title, msg->track.title);
		strcpy(track_artist, msg->track.artist);
		track_duration_ms = msg->track.duration_ms;
		/* The lyrics are only fetched when they are looked at. */
		lyrics_requested = false;
		if (lyrics_open) {
			lyrics_request();
		} else {
			lyrics_set_status("");
		}
		lv_label_set_text(title_label, msg->track.title);
		lv_label_set_text(artist_label, msg->track.artist);
		lv_slider_set_range(progress_slider, 0, MAX(msg->track.duration_ms, 1));
		lv_slider_set_value(progress_slider, 0, LV_ANIM_OFF);
		set_time(elapsed_label, 0);
		set_time(duration_label, msg->track.duration_ms);
		set_controls_enabled(true);
		/* Tracks of one album share the cover: keep it. */
		if (strcmp(cover_url, msg->track.image_url) != 0) {
			drop_cover();
			strcpy(cover_url, msg->track.image_url);
			cover_request(cover_url);
		}
		break;
	case UI_MSG_NETWORK:
		lv_obj_set_style_text_color(network_icon,
					    msg->connected ? COLOR_TEXT : COLOR_ERROR, 0);
		lv_obj_set_style_text_color(network_icon,
					    msg->connected ? COLOR_SUBTLE : COLOR_ERROR,
					    LV_STATE_PRESSED);
		break;
	case UI_MSG_BATTERY:
		if (msg->battery_percent < 0) {
			lv_obj_add_flag(battery_icon, LV_OBJ_FLAG_HIDDEN);
			break;
		}
		lv_label_set_text(battery_icon, battery_symbol(msg->battery_percent));
		lv_obj_set_style_text_color(battery_icon,
					    msg->battery_percent <= BATTERY_LOW_PERCENT
						    ? COLOR_ERROR : COLOR_TEXT, 0);
		lv_obj_remove_flag(battery_icon, LV_OBJ_FLAG_HIDDEN);
		break;
	case UI_MSG_LYRICS:
		/* Lyrics of a track that is no longer on screen are stale. */
		if (track_shown && strcmp(msg->lyrics->title, track_title) == 0) {
			lyrics_set(msg->lyrics);
		} else {
			lv_free(msg->lyrics);
		}
		break;
	case UI_MSG_LYRICS_STATUS:
		if (track_shown && strcmp(msg->lyrics_status.title, track_title) == 0) {
			lyrics_set_status(msg->lyrics_status.status);
		}
		break;
	case UI_MSG_LIST_RESET:
		if (msg->list_reset.list == list_shown) {
			list_reset(msg->list_reset.generation, msg->list_reset.heading,
				   msg->list_reset.top_level, msg->list_reset.status);
		}
		break;
	case UI_MSG_LIST_ROW:
		if (msg->list_row.list == list_shown &&
		    msg->list_row.generation == list_generation) {
			list_add(msg->list_row.title, msg->list_row.subtitle,
				 msg->list_row.duration_ms);
		}
		break;
	case UI_MSG_PAUSED:
		paused = msg->paused;
		apply_paused();
		break;
	case UI_MSG_VOLUME:
		if (!lv_slider_is_dragged(volume_slider)) {
			lv_slider_set_value(volume_slider, msg->volume, LV_ANIM_OFF);
			update_volume_icon();
		}
		break;
	}
}

static void set_backlight(bool on)
{
#if DT_HAS_ALIAS(zspot_backlight)
	const struct device *backlight = DEVICE_DT_GET(DT_ALIAS(zspot_backlight));

	if (!device_is_ready(backlight)) {
		return;
	}
	if (on) {
		regulator_enable(backlight);
	} else {
		regulator_disable(backlight);
	}
#else
	ARG_UNUSED(on);
#endif
}

static void screen_sleep(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	screen_off = true;
	lv_obj_remove_flag(wake_overlay, LV_OBJ_FLAG_HIDDEN);
	/* Nothing is drawn or sent to the panel while it is off */
	lv_display_enable_invalidation(NULL, false);
	set_backlight(false);
	display_blanking_on(display);
}

static void screen_wake(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	screen_off = false;
	lv_display_enable_invalidation(NULL, true);
	lv_obj_invalidate(lv_screen_active());
	/* The panel kept the picture it had when it was turned off */
	lv_refr_now(NULL);
	display_blanking_off(display);
	set_backlight(true);
}

static void on_wake_event(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
		if (screen_off) {
			screen_wake();
		}
	} else {
		/* The touch that woke the screen ends here, below nothing saw it */
		lv_obj_add_flag(wake_overlay, LV_OBJ_FLAG_HIDDEN);
	}
}

static void create_wake_overlay(void)
{
	wake_overlay = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(wake_overlay);
	lv_obj_set_size(wake_overlay, LV_PCT(100), LV_PCT(100));
	lv_obj_add_flag(wake_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_event_cb(wake_overlay, on_wake_event, LV_EVENT_PRESSED, NULL);
	lv_obj_add_event_cb(wake_overlay, on_wake_event, LV_EVENT_RELEASED, NULL);
	lv_obj_add_event_cb(wake_overlay, on_wake_event, LV_EVENT_PRESS_LOST, NULL);
}

static void on_tick(lv_timer_t *timer)
{
	static uint32_t ticks;
	struct ui_msg msg;

	ARG_UNUSED(timer);

	while (k_msgq_get(&ui_msgq, &msg, K_NO_WAIT) == 0) {
		apply(&msg);
	}

	if (CONFIG_ZSPOT_SCREEN_TIMEOUT_SECONDS > 0 && !screen_off &&
	    lv_display_get_inactive_time(NULL) >=
		    CONFIG_ZSPOT_SCREEN_TIMEOUT_SECONDS * MSEC_PER_SEC) {
		screen_sleep();
	}

	if (++ticks % POSITION_TICKS == 0 && track_shown &&
	    !lv_slider_is_dragged(progress_slider)) {
		uint32_t position = MIN(ops->position_ms(),
					(uint32_t)lv_slider_get_max_value(progress_slider));

		lv_slider_set_value(progress_slider, position, LV_ANIM_OFF);
		set_time(elapsed_label, position);
		if (lyrics_open) {
			lyrics_follow(position);
		}
	}
}

static void on_progress_event(lv_event_t *e)
{
	uint32_t position = lv_slider_get_value(progress_slider);

	if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
		ops->seek(position);
	}
	/* The elapsed time follows the knob while it is dragged. */
	set_time(elapsed_label, position);
}

static void on_volume_event(lv_event_t *e)
{
	ops->set_volume(lv_slider_get_value(volume_slider),
			lv_event_get_code(e) == LV_EVENT_RELEASED);
	update_volume_icon();
}

/* Mutes by setting the volume to zero, so that the Spotify app shows it too. */
static void on_volume_icon_clicked(lv_event_t *e)
{
	uint16_t volume = lv_slider_get_value(volume_slider);

	ARG_UNUSED(e);

	if (volume > 0) {
		volume_unmuted = volume;
		volume = 0;
	} else {
		volume = volume_unmuted;
	}
	lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);
	ops->set_volume(volume, true);
	update_volume_icon();
}

static void on_play_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ops->set_paused(!paused);
}

static void on_prev_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ops->previous();
}

static void on_next_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ops->next();
}

/* Shows the list view empty; its content arrives through ui_list_reset(). */
static void open_list(enum ui_list list, const char *heading)
{
	list_shown = list;
	/* No listing of the new kind is current yet: 0 matches none. */
	list_reset(0, heading, true, "");
	lv_screen_load(list_screen);
}

static void on_library_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	open_list(UI_LIST_LIBRARY, "Your Library");
	ops->library_open();
}

static void on_wifi_hold_elapsed(lv_timer_t *timer)
{
	ARG_UNUSED(timer);
	wifi_hold_timer = NULL; /* one-shot: LVGL deletes it */
	open_list(UI_LIST_WIFI, "Wi-Fi");
	ops->wifi_scan();
}

/* The Wi-Fi settings open once the network indicator was held long enough. */
static void on_network_event(lv_event_t *e)
{
	if (wifi_hold_timer != NULL) {
		lv_timer_delete(wifi_hold_timer);
		wifi_hold_timer = NULL;
	}
	if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
		wifi_hold_timer = lv_timer_create(on_wifi_hold_elapsed, WIFI_HOLD_MS, NULL);
		lv_timer_set_repeat_count(wifi_hold_timer, 1);
	}
}

static void on_search_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	open_entry(true);
}

/* The button of the entry screen or the confirm key of its keyboard */
static void on_entry_confirm(lv_event_t *e)
{
	const char *text = lv_textarea_get_text(entry_text);

	ARG_UNUSED(e);

	if (!entry_search) {
		ops->wifi_connect(wifi_ssid, text);
		lv_screen_load(main_screen);
	} else if (text[0] != '\0') {
		/* The hits arrive through ui_list_reset(). */
		open_list(UI_LIST_SEARCH, text);
		ops->search(text);
	}
}

static void on_entry_back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	lv_screen_load(entry_search ? main_screen : list_screen);
}

static void on_list_back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (list_shown == UI_LIST_SEARCH) {
		/* Back to the search with its text, to refine it */
		lv_screen_load(entry_screen);
	} else if (list_top_level) {
		lv_screen_load(main_screen);
	} else {
		ops->library_back();
	}
}

static lv_obj_t *create_box(lv_obj_t *parent, int32_t w, int32_t h)
{
	lv_obj_t *box = lv_obj_create(parent);

	lv_obj_remove_style_all(box);
	lv_obj_set_size(box, w, h);
	lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
	return box;
}

static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
			      const char *text)
{
	lv_obj_t *label = lv_label_create(parent);

	lv_obj_set_style_text_font(label, font, 0);
	lv_obj_set_style_text_color(label, color, 0);
	lv_label_set_text(label, text);
	return label;
}

static lv_obj_t *create_slider(lv_obj_t *parent, int32_t y, int32_t x, int32_t w,
			       lv_event_cb_t event_cb)
{
	lv_obj_t *slider = lv_slider_create(parent);

	lv_obj_remove_style_all(slider);
	lv_obj_set_pos(slider, x, y);
	lv_obj_set_size(slider, w, sc(4));
	lv_obj_set_ext_click_area(slider, sc(14));

	lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(slider, COLOR_TRACK, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);

	lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(slider, COLOR_TEXT, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(slider, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);

	lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
	lv_obj_set_style_bg_color(slider, COLOR_TEXT, LV_PART_KNOB);
	lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_pad_all(slider, sc(4), LV_PART_KNOB);

	lv_obj_set_style_opa(slider, LV_OPA_40, LV_STATE_DISABLED);

	lv_obj_add_event_cb(slider, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_event_cb(slider, event_cb, LV_EVENT_RELEASED, NULL);
	return slider;
}

/* Transport button centred on @p x of the controls line; returns the button. */
static lv_obj_t *create_button(lv_obj_t *parent, int32_t x, int32_t size, const char *symbol,
			       lv_event_cb_t clicked_cb, lv_obj_t **icon)
{
	lv_obj_t *button = create_box(parent, size, size);
	lv_obj_t *label;

	lv_obj_set_pos(button, x - size / 2, lay.info_y + CONTROLS_DY - size / 2);
	lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_text_color(button, COLOR_TEXT, 0);
	lv_obj_set_style_text_color(button, COLOR_SUBTLE, LV_STATE_PRESSED);
	lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
	lv_obj_add_event_cb(button, clicked_cb, LV_EVENT_CLICKED, NULL);

	label = lv_label_create(button);
	lv_obj_set_style_text_font(label, font_icon, 0);
	lv_label_set_text(label, symbol);
	lv_obj_center(label);
	if (icon != NULL) {
		*icon = label;
	}
	return button;
}

/* Borderless icon with a comfortable touch area, e.g. in a top bar. */
static lv_obj_t *create_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t clicked_cb)
{
	lv_obj_t *button = create_box(parent, sc(44), sc(44));
	lv_obj_t *label;

	lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_text_color(button, COLOR_TEXT, 0);
	lv_obj_set_style_text_color(button, COLOR_SUBTLE, LV_STATE_PRESSED);
	if (clicked_cb != NULL) {
		lv_obj_add_event_cb(button, clicked_cb, LV_EVENT_CLICKED, NULL);
	}

	label = lv_label_create(button);
	lv_obj_set_style_text_font(label, font_text, 0);
	lv_label_set_text(label, symbol);
	lv_obj_center(label);
	return button;
}

static void create_list_screen(void)
{
	lv_obj_t *back_button;

	list_screen = lv_obj_create(NULL);
	lv_obj_remove_flag(list_screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(list_screen, COLOR_BG, 0);
	lv_obj_set_style_bg_opa(list_screen, LV_OPA_COVER, 0);

	back_button = create_icon_button(list_screen, LV_SYMBOL_LEFT, on_list_back_clicked);
	lv_obj_set_pos(back_button, sc(8), sc(2));

	list_heading = create_label(list_screen, font_text, COLOR_TEXT, "");
	lv_label_set_long_mode(list_heading, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_style_text_align(list_heading, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_size(list_heading, lay.w - 2 * sc(56),
			lv_font_get_line_height(font_text));
	lv_obj_align(list_heading, LV_ALIGN_TOP_MID, 0,
		     (BAR_H - lv_font_get_line_height(font_text)) / 2);

	list_rows = create_box(list_screen, lay.w, lay.h - BAR_H);
	lv_obj_set_pos(list_rows, 0, BAR_H);
	lv_obj_add_flag(list_rows, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scroll_dir(list_rows, LV_DIR_VER);

	list_status = create_label(list_screen, font_text, COLOR_SUBTLE, "");
	lv_label_set_long_mode(list_status, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_style_text_align(list_status, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(list_status, lay.w - 2 * lay.margin);
	lv_obj_center(list_status);
}

/* Text entry with a keyboard, set up for its use by open_entry() */
static void create_entry_screen(void)
{
	lv_obj_t *button;
	lv_obj_t *keyboard;

	entry_screen = lv_obj_create(NULL);
	lv_obj_remove_flag(entry_screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(entry_screen, COLOR_BG, 0);
	lv_obj_set_style_bg_opa(entry_screen, LV_OPA_COVER, 0);

	button = create_icon_button(entry_screen, LV_SYMBOL_LEFT, on_entry_back_clicked);
	lv_obj_set_pos(button, sc(8), sc(2));

	entry_heading = create_label(entry_screen, font_text, COLOR_TEXT, "");
	lv_label_set_long_mode(entry_heading, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_style_text_align(entry_heading, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_size(entry_heading, lay.w - 2 * sc(56),
			lv_font_get_line_height(font_text));
	lv_obj_align(entry_heading, LV_ALIGN_TOP_MID, 0,
		     (BAR_H - lv_font_get_line_height(font_text)) / 2);

	entry_hint = create_label(entry_screen, font_small, COLOR_SUBTLE, "");
	lv_obj_set_pos(entry_hint, lay.margin, BAR_H + sc(16));

	entry_text = lv_textarea_create(entry_screen);
	lv_textarea_set_one_line(entry_text, true);
	lv_textarea_set_max_length(entry_text, ENTRY_TEXT_MAX);
	lv_obj_set_style_text_font(entry_text, font_text, 0);
	lv_obj_set_style_pad_ver(entry_text, sc(11), 0);
	lv_obj_set_width(entry_text, lay.w - 2 * lay.margin);
	lv_obj_set_pos(entry_text, lay.margin, BAR_H + sc(40));
	lv_obj_add_state(entry_text, LV_STATE_FOCUSED); /* shows the cursor */

	button = create_box(entry_screen, sc(120), sc(40));
	lv_obj_align(button, LV_ALIGN_TOP_MID, 0, BAR_H + sc(104));
	lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(button, COLOR_ACCENT, 0);
	lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
	lv_obj_add_event_cb(button, on_entry_confirm, LV_EVENT_CLICKED, NULL);
	entry_action = create_label(button, font_text, COLOR_BG, "");
	lv_obj_center(entry_action);
	lv_obj_set_flag(button, LV_OBJ_FLAG_HIDDEN, !lay.entry_button);

	/* The keyboard's confirm key acts like the button, its close key goes back. */
	keyboard = lv_keyboard_create(entry_screen);
	lv_obj_set_size(keyboard, lay.w, lay.keyboard_h);
	lv_obj_set_style_text_font(keyboard, font_text, LV_PART_ITEMS);
	lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_keyboard_set_textarea(keyboard, entry_text);
	lv_obj_add_event_cb(keyboard, on_entry_confirm, LV_EVENT_READY, NULL);
	lv_obj_add_event_cb(keyboard, on_entry_back_clicked, LV_EVENT_CANCEL, NULL);
}

static void create_screen(void)
{
	lv_obj_t *screen = lv_screen_active();
	lv_obj_t *library_button;
	int32_t controls_x;
	int32_t controls_step;
	const int32_t battery_w = IS_ENABLED(CONFIG_ZSPOT_BATTERY) ? sc(28) : 0;
	const int32_t search_w = lay.w - 2 * sc(52) - battery_w;
	lv_obj_t *search_bar;
	lv_obj_t *search_image;
	lv_obj_t *search_hint;
	lv_obj_t *cover_box;
	lv_obj_t *placeholder;
	lv_obj_t *lyrics_button;

	main_screen = screen;

	/* The top fades from the cover's colour into the background. */
	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
	lv_obj_set_style_bg_grad_color(screen, COLOR_BG, 0);
	lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_grad_stop(screen, 200, 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

	/* Search bar between the two corner buttons; tapping it asks for the text */
	/* Centred on the icons of the corner buttons, clear of the cover below */
	/* and of the battery indicator, when there is one */
	search_bar = create_box(screen, search_w, SEARCH_BAR_H);
	lv_obj_align(search_bar, LV_ALIGN_TOP_MID, -battery_w / 2, sc(18) - SEARCH_BAR_H / 2);
	lv_obj_add_flag(search_bar, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_radius(search_bar, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(search_bar, COLOR_SURFACE, 0);
	lv_obj_set_style_bg_color(search_bar, COLOR_TRACK, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(search_bar, LV_OPA_COVER, 0);
	lv_obj_add_event_cb(search_bar, on_search_clicked, LV_EVENT_CLICKED, NULL);

	search_image = lv_image_create(search_bar);
	lv_image_set_src(search_image, &search_icon);
	lv_obj_set_style_image_recolor(search_image, COLOR_SUBTLE, 0);
	lv_obj_set_style_image_recolor_opa(search_image, LV_OPA_COVER, 0);
	lv_image_set_scale(search_image, LV_SCALE_NONE * lay.scale / 100);
	lv_obj_align(search_image, LV_ALIGN_LEFT_MID, sc(10), 0);

	/* A shorter hint when the bar has no room for the question */
	search_hint = create_label(search_bar, font_small, COLOR_SUBTLE,
				   "What do you want to play?");
	lv_obj_update_layout(search_hint);
	if (lv_obj_get_width(search_hint) > search_w - sc(32) - sc(14)) {
		lv_label_set_text(search_hint, "Search");
	}
	lv_obj_align(search_hint, LV_ALIGN_LEFT_MID, sc(32), 0);

	/* Red until the application reports the network as connected */
	network_icon = create_icon_button(screen, LV_SYMBOL_WIFI, NULL);
	lv_obj_align(network_icon, LV_ALIGN_TOP_RIGHT, -sc(4) - battery_w, -sc(4));
	lv_obj_set_style_text_color(network_icon, COLOR_ERROR, 0);
	lv_obj_set_style_text_color(network_icon, COLOR_ERROR, LV_STATE_PRESSED);
	lv_obj_add_event_cb(network_icon, on_network_event, LV_EVENT_PRESSED, NULL);
	lv_obj_add_event_cb(network_icon, on_network_event, LV_EVENT_RELEASED, NULL);
	lv_obj_add_event_cb(network_icon, on_network_event, LV_EVENT_PRESS_LOST, NULL);

	/* Right of the network indicator; hidden until a charge level is reported */
	battery_icon = create_label(screen, font_text, COLOR_TEXT, LV_SYMBOL_BATTERY_FULL);
	lv_obj_align(battery_icon, LV_ALIGN_TOP_RIGHT, -sc(14),
		     sc(18) - lv_font_get_line_height(font_text) / 2);
	lv_obj_add_flag(battery_icon, LV_OBJ_FLAG_HIDDEN);

	library_button = create_icon_button(screen, LV_SYMBOL_LIST, on_library_clicked);
	lv_obj_set_pos(library_button, sc(4), -sc(4));

	cover_box = create_box(screen, lay.cover_size, lay.cover_size);
	lv_obj_set_pos(cover_box, lay.cover_x, lay.cover_y);
	lv_obj_set_style_radius(cover_box, sc(8), 0);
	lv_obj_set_style_clip_corner(cover_box, true, 0);
	lv_obj_set_style_bg_color(cover_box, COLOR_SURFACE, 0);
	lv_obj_set_style_bg_opa(cover_box, LV_OPA_COVER, 0);

	placeholder = create_label(cover_box, font_icon, COLOR_TRACK, LV_SYMBOL_AUDIO);
	lv_obj_center(placeholder);

	cover_image = lv_image_create(cover_box);
	lv_obj_set_size(cover_image, lay.cover_size, lay.cover_size);
	lv_image_set_inner_align(cover_image, LV_IMAGE_ALIGN_STRETCH);
	lv_obj_add_flag(cover_image, LV_OBJ_FLAG_HIDDEN);

	/* One wrapped label per line, stacked; scrolled as the track plays */
	lyrics_panel = create_box(cover_box, lay.cover_size, lay.cover_size);
	lv_obj_set_style_bg_color(lyrics_panel, COLOR_SURFACE, 0);
	lv_obj_set_style_bg_opa(lyrics_panel, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(lyrics_panel, LYRICS_PAD, 0);
	lv_obj_set_style_pad_row(lyrics_panel, sc(12), 0);
	lv_obj_set_flex_flow(lyrics_panel, LV_FLEX_FLOW_COLUMN);
	lv_obj_add_flag(lyrics_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_scroll_dir(lyrics_panel, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(lyrics_panel, LV_SCROLLBAR_MODE_OFF);

	title_label = create_label(screen, font_title, COLOR_TEXT, "");
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
	lv_obj_set_width(title_label, lay.info_w);
	lv_obj_set_pos(title_label, lay.info_x, lay.info_y + TITLE_DY);

	artist_label = create_label(screen, font_text, COLOR_SUBTLE, "");
	lv_label_set_long_mode(artist_label, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_size(artist_label, lay.info_w, lv_font_get_line_height(font_text));
	lv_obj_set_pos(artist_label, lay.info_x, lay.info_y + ARTIST_DY);

	progress_slider = create_slider(screen, lay.info_y + PROGRESS_DY, lay.info_x, lay.info_w,
					on_progress_event);

	elapsed_label = create_label(screen, font_small, COLOR_SUBTLE, "");
	lv_obj_set_pos(elapsed_label, lay.info_x, lay.info_y + TIME_DY);
	duration_label = create_label(screen, font_small, COLOR_SUBTLE, "");
	lv_obj_align(duration_label, LV_ALIGN_TOP_RIGHT, -(lay.w - lay.info_x - lay.info_w),
		     lay.info_y + TIME_DY);

	/* Transport buttons around the middle of the column, closer on a narrow one */
	controls_x = lay.info_x + lay.info_w / 2;
	controls_step = MIN(sc(80), lay.info_w / 2 - sc(24));
	prev_button = create_button(screen, controls_x - controls_step, sc(48), LV_SYMBOL_PREV,
				    on_prev_clicked, NULL);
	play_button = create_button(screen, controls_x, sc(60), LV_SYMBOL_PLAY, on_play_clicked,
				    &play_icon);
	next_button = create_button(screen, controls_x + controls_step, sc(48), LV_SYMBOL_NEXT,
				    on_next_clicked, NULL);
	lv_obj_set_style_bg_color(play_button, COLOR_TEXT, 0);
	lv_obj_set_style_bg_color(play_button, COLOR_SUBTLE, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(play_button, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(play_button, COLOR_BG, 0);
	lv_obj_set_style_text_color(play_button, COLOR_BG, LV_STATE_PRESSED);

	/* Bottom row: lyrics button, speaker symbol, volume slider */
	lyrics_button = create_box(screen, sc(36), sc(36));
	lv_obj_set_pos(lyrics_button, lay.info_x - sc(8), lay.info_y + VOLUME_DY + sc(2) - sc(18));
	lv_obj_add_flag(lyrics_button, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(lyrics_button, on_lyrics_clicked, LV_EVENT_CLICKED, NULL);

	/* An alpha-only image takes its colour from the recolour style. */
	lyrics_icon = lv_image_create(lyrics_button);
	lv_image_set_src(lyrics_icon, &mic_icon);
	lv_image_set_scale(lyrics_icon, LV_SCALE_NONE * lay.scale / 100);
	lv_obj_set_style_image_recolor(lyrics_icon, COLOR_SUBTLE, 0);
	lv_obj_set_style_image_recolor_opa(lyrics_icon, LV_OPA_COVER, 0);
	lv_obj_center(lyrics_icon);

	volume_icon = create_label(screen, font_text, COLOR_SUBTLE,
				   LV_SYMBOL_VOLUME_MAX);
	lv_obj_set_pos(volume_icon, lay.info_x + sc(32), lay.info_y + VOLUME_DY - sc(7));
	lv_obj_add_flag(volume_icon, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_ext_click_area(volume_icon, sc(12));
	lv_obj_set_style_text_color(volume_icon, COLOR_TEXT, LV_STATE_PRESSED);
	lv_obj_add_event_cb(volume_icon, on_volume_icon_clicked, LV_EVENT_CLICKED, NULL);
	volume_slider = create_slider(screen, lay.info_y + VOLUME_DY, lay.info_x + sc(62),
				      lay.info_w - sc(62),
				      on_volume_event);
	lv_slider_set_range(volume_slider, 0, UINT16_MAX);
	lv_slider_set_value(volume_slider, UINT16_MAX, LV_ANIM_OFF);

	create_list_screen();
	create_entry_screen();
	create_wake_overlay();

	lv_timer_create(on_tick, TICK_MS, NULL);
}

int ui_init(const struct ui_ops *ui_ops)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	char *msgq_buf;

	if (!device_is_ready(display) || lv_display_get_default() == NULL) {
		LOG_WRN("No display, running without the UI");
		return -ENODEV;
	}

	ops = ui_ops;

	lvgl_lock();
	msgq_buf = lv_malloc(UI_MSGQ_LEN * sizeof(struct ui_msg));
	if (msgq_buf == NULL || cover_init(on_cover_ready) != 0) {
		lvgl_unlock();
		LOG_ERR("LVGL memory pool too small for the UI");
		return -ENOMEM;
	}
	k_msgq_init(&ui_msgq, msgq_buf, sizeof(struct ui_msg), UI_MSGQ_LEN);
	compute_layout();
	create_screen();
	lvgl_unlock();

	display_blanking_off(display);
	ready = true;
	return 0;
}

static bool post(const struct ui_msg *msg)
{
	if (!ready) {
		return false;
	}
	if (k_msgq_put(&ui_msgq, msg, K_NO_WAIT) != 0) {
		LOG_WRN("UI queue full, update dropped");
		return false;
	}
	return true;
}

void ui_show_message(const char *headline, const char *detail)
{
	struct ui_msg msg = {.type = UI_MSG_MESSAGE};

	copy_text(msg.message.headline, sizeof(msg.message.headline), headline);
	copy_text(msg.message.detail, sizeof(msg.message.detail), detail);
	post(&msg);
}

void ui_show_track(const char *title, const char *artist, const char *image_url,
		   uint32_t duration_ms)
{
	struct ui_msg msg = {.type = UI_MSG_TRACK};

	copy_text(msg.track.title, sizeof(msg.track.title), title);
	copy_text(msg.track.artist, sizeof(msg.track.artist), artist);
	copy_text(msg.track.image_url, sizeof(msg.track.image_url), image_url);
	msg.track.duration_ms = duration_ms;
	post(&msg);
}

void ui_set_paused(bool is_paused)
{
	const struct ui_msg msg = {.type = UI_MSG_PAUSED, .paused = is_paused};

	post(&msg);
}

void ui_set_volume(uint16_t volume)
{
	const struct ui_msg msg = {.type = UI_MSG_VOLUME, .volume = volume};

	post(&msg);
}

void ui_set_network(bool connected)
{
	const struct ui_msg msg = {.type = UI_MSG_NETWORK, .connected = connected};

	post(&msg);
}

void ui_set_battery(int percent)
{
	const struct ui_msg msg = {.type = UI_MSG_BATTERY, .battery_percent = percent};

	post(&msg);
}

bool ui_show_lyrics(struct ui_lyrics *new_lyrics)
{
	const struct ui_msg msg = {.type = UI_MSG_LYRICS, .lyrics = new_lyrics};

	return post(&msg);
}

void ui_set_lyrics_status(const char *title, const char *status)
{
	struct ui_msg msg = {.type = UI_MSG_LYRICS_STATUS};

	copy_text(msg.lyrics_status.title, sizeof(msg.lyrics_status.title), title);
	copy_text(msg.lyrics_status.status, sizeof(msg.lyrics_status.status), status);
	post(&msg);
}

void ui_list_reset(enum ui_list list, uint32_t generation, const char *heading, bool top_level,
		   const char *status)
{
	struct ui_msg msg = {
		.type = UI_MSG_LIST_RESET,
		.list_reset = {.list = list, .generation = generation, .top_level = top_level},
	};

	copy_text(msg.list_reset.heading, sizeof(msg.list_reset.heading), heading);
	copy_text(msg.list_reset.status, sizeof(msg.list_reset.status), status);
	post(&msg);
}

void ui_list_add(enum ui_list list, uint32_t generation, const char *title, const char *subtitle,
		 uint32_t duration_ms)
{
	struct ui_msg msg = {
		.type = UI_MSG_LIST_ROW,
		.list_row = {.list = list, .generation = generation, .duration_ms = duration_ms},
	};

	copy_text(msg.list_row.title, sizeof(msg.list_row.title), title);
	copy_text(msg.list_row.subtitle, sizeof(msg.list_row.subtitle), subtitle);
	post(&msg);
}
