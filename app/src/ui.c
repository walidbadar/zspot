/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * "Now Playing" screen for a 320x480 portrait display, plus a queue view that
 * lists the upcoming tracks and plays the one that is tapped.
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

#define MARGIN       24
#define CONTENT_W    (320 - 2 * MARGIN)
#define COVER_SIZE   256
#define COVER_Y      36
#define TITLE_Y      304
#define ARTIST_Y     332
#define PROGRESS_Y   366
#define TIME_Y       378
#define CONTROLS_Y   420 /* centre line of the transport buttons */
#define VOLUME_Y     466

#define BAR_H        48 /* top bar of the queue view */
#define ROW_H        56

#define TICK_MS          50
#define POSITION_TICKS   5 /* progress refresh: every 250 ms */

enum ui_msg_type {
	UI_MSG_MESSAGE,
	UI_MSG_TRACK,
	UI_MSG_PAUSED,
	UI_MSG_VOLUME,
	UI_MSG_QUEUE_RESET,
	UI_MSG_QUEUE_ITEM,
	UI_MSG_QUEUE_CHANGED,
};

struct ui_msg {
	enum ui_msg_type type;
	union {
		struct {
			char headline[64];
			char detail[96];
		} message;
		struct {
			char title[96];
			char artist[96];
			char image_url[128];
			uint32_t duration_ms;
		} track;
		struct {
			uint32_t generation;
			int first;
			int count;
		} queue_reset;
		struct {
			uint32_t generation;
			int index;
			char title[96];
			char artist[96];
			uint32_t duration_ms;
		} queue_item;
		bool paused;
		uint16_t volume;
	};
};

/* Room for a complete queue listing arriving at once */
#define UI_MSGQ_LEN (UI_QUEUE_MAX + 12)

/* Buffer allocated from the LVGL pool, like everything else of the UI */
static struct k_msgq ui_msgq;

static const struct ui_ops *ops;
static bool ready;

static lv_obj_t *main_screen;
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

static lv_obj_t *queue_screen;
static lv_obj_t *queue_list;
static lv_obj_t *queue_empty_label;
static uint32_t queue_generation;
static int queue_first;
static int queue_count;

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
	memcpy(dst, src, len);
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

static void apply_paused(void)
{
	lv_label_set_text(play_icon, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
}

static lv_obj_t *create_box(lv_obj_t *parent, int32_t w, int32_t h);
static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
			      const char *text);

static bool queue_visible(void)
{
	return lv_screen_active() == queue_screen;
}

static void on_queue_row_clicked(lv_event_t *e)
{
	ops->queue_play((int)(intptr_t)lv_event_get_user_data(e));
	lv_screen_load(main_screen);
}

/* Rebuilds the listing with a placeholder row per entry. */
static void queue_reset(uint32_t generation, int first, int count)
{
	queue_generation = generation;
	queue_first = first;
	queue_count = CLAMP(count, 0, UI_QUEUE_MAX);

	lv_obj_clean(queue_list);
	lv_obj_scroll_to_y(queue_list, 0, LV_ANIM_OFF);
	lv_obj_set_flag(queue_empty_label, LV_OBJ_FLAG_HIDDEN, queue_count > 0);

	for (int i = 0; i < queue_count; i++) {
		lv_obj_t *row = create_box(queue_list, 320, ROW_H);
		lv_obj_t *title;
		lv_obj_t *artist;
		lv_obj_t *duration;

		lv_obj_set_pos(row, 0, i * ROW_H);
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_bg_color(row, COLOR_SURFACE, LV_STATE_PRESSED);
		lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
		lv_obj_add_event_cb(row, on_queue_row_clicked, LV_EVENT_CLICKED,
				    (void *)(intptr_t)(first + i));

		/* The first entry is the track that is playing. */
		title = create_label(row, &lv_font_montserrat_14,
				     i == 0 ? COLOR_ACCENT : COLOR_TEXT, "...");
		lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
		lv_obj_set_size(title, CONTENT_W - 44,
				lv_font_get_line_height(&lv_font_montserrat_14));
		lv_obj_set_pos(title, MARGIN, 10);

		artist = create_label(row, &lv_font_montserrat_12, COLOR_SUBTLE, "");
		lv_label_set_long_mode(artist, LV_LABEL_LONG_MODE_DOTS);
		lv_obj_set_size(artist, CONTENT_W - 44,
				lv_font_get_line_height(&lv_font_montserrat_12));
		lv_obj_set_pos(artist, MARGIN, 31);

		duration = create_label(row, &lv_font_montserrat_12, COLOR_SUBTLE, "");
		lv_obj_align(duration, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
	}
}

static void queue_fill(int index, const char *title, const char *artist, uint32_t duration_ms)
{
	lv_obj_t *row = lv_obj_get_child(queue_list, index - queue_first);

	if (row == NULL) {
		return;
	}
	lv_label_set_text(lv_obj_get_child(row, 0), title);
	lv_label_set_text(lv_obj_get_child(row, 1), artist);
	set_time(lv_obj_get_child(row, 2), duration_ms);
}

static void apply(const struct ui_msg *msg)
{
	switch (msg->type) {
	case UI_MSG_MESSAGE:
		track_shown = false;
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
		/* The listing starts at the current track. */
		if (queue_visible()) {
			ops->queue_refresh();
		}
		break;
	case UI_MSG_QUEUE_RESET:
		queue_reset(msg->queue_reset.generation, msg->queue_reset.first,
			    msg->queue_reset.count);
		break;
	case UI_MSG_QUEUE_ITEM:
		if (msg->queue_item.generation == queue_generation) {
			queue_fill(msg->queue_item.index, msg->queue_item.title,
				   msg->queue_item.artist, msg->queue_item.duration_ms);
		}
		break;
	case UI_MSG_QUEUE_CHANGED:
		if (queue_visible()) {
			ops->queue_refresh();
		}
		break;
	case UI_MSG_PAUSED:
		paused = msg->paused;
		apply_paused();
		break;
	case UI_MSG_VOLUME:
		if (!lv_slider_is_dragged(volume_slider)) {
			lv_slider_set_value(volume_slider, msg->volume, LV_ANIM_OFF);
		}
		break;
	}
}

static void on_tick(lv_timer_t *timer)
{
	static uint32_t ticks;
	struct ui_msg msg;

	ARG_UNUSED(timer);

	while (k_msgq_get(&ui_msgq, &msg, K_NO_WAIT) == 0) {
		apply(&msg);
	}

	if (++ticks % POSITION_TICKS == 0 && track_shown &&
	    !lv_slider_is_dragged(progress_slider)) {
		uint32_t position = MIN(ops->position_ms(),
					(uint32_t)lv_slider_get_max_value(progress_slider));

		lv_slider_set_value(progress_slider, position, LV_ANIM_OFF);
		set_time(elapsed_label, position);
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

static void on_queue_open_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	lv_screen_load(queue_screen);
	ops->queue_refresh();
}

static void on_queue_close_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	lv_screen_load(main_screen);
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
	lv_obj_set_size(slider, w, 4);
	lv_obj_set_ext_click_area(slider, 14);

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
	lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);

	lv_obj_set_style_opa(slider, LV_OPA_40, LV_STATE_DISABLED);

	lv_obj_add_event_cb(slider, event_cb, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_event_cb(slider, event_cb, LV_EVENT_RELEASED, NULL);
	return slider;
}

/* Transport button centred on (@p x, CONTROLS_Y); returns the button. */
static lv_obj_t *create_button(lv_obj_t *parent, int32_t x, int32_t size, const char *symbol,
			       lv_event_cb_t clicked_cb, lv_obj_t **icon)
{
	lv_obj_t *button = create_box(parent, size, size);
	lv_obj_t *label;

	lv_obj_set_pos(button, x - size / 2, CONTROLS_Y - size / 2);
	lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_text_color(button, COLOR_TEXT, 0);
	lv_obj_set_style_text_color(button, COLOR_SUBTLE, LV_STATE_PRESSED);
	lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
	lv_obj_add_event_cb(button, clicked_cb, LV_EVENT_CLICKED, NULL);

	label = lv_label_create(button);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
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
	lv_obj_t *button = create_box(parent, 44, 44);
	lv_obj_t *label;

	lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_text_color(button, COLOR_TEXT, 0);
	lv_obj_set_style_text_color(button, COLOR_SUBTLE, LV_STATE_PRESSED);
	lv_obj_add_event_cb(button, clicked_cb, LV_EVENT_CLICKED, NULL);

	label = lv_label_create(button);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
	lv_label_set_text(label, symbol);
	lv_obj_center(label);
	return button;
}

static void create_queue_screen(void)
{
	lv_obj_t *close_button;
	lv_obj_t *heading;

	queue_screen = lv_obj_create(NULL);
	lv_obj_remove_flag(queue_screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(queue_screen, COLOR_BG, 0);
	lv_obj_set_style_bg_opa(queue_screen, LV_OPA_COVER, 0);

	close_button = create_icon_button(queue_screen, LV_SYMBOL_LEFT, on_queue_close_clicked);
	lv_obj_set_pos(close_button, 8, 2);

	heading = create_label(queue_screen, &lv_font_montserrat_14, COLOR_TEXT, "Queue");
	lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, (BAR_H - 14) / 2);

	queue_list = create_box(queue_screen, 320, 480 - BAR_H);
	lv_obj_set_pos(queue_list, 0, BAR_H);
	lv_obj_add_flag(queue_list, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scroll_dir(queue_list, LV_DIR_VER);

	queue_empty_label = create_label(queue_screen, &lv_font_montserrat_14, COLOR_SUBTLE,
					 "Nothing is queued");
	lv_obj_center(queue_empty_label);
}

static void create_screen(const char *device_name)
{
	lv_obj_t *screen = lv_screen_active();
	lv_obj_t *queue_button;
	lv_obj_t *header;
	lv_obj_t *cover_box;
	lv_obj_t *placeholder;
	lv_obj_t *volume_icon;

	main_screen = screen;

	/* The top fades from the cover's colour into the background. */
	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
	lv_obj_set_style_bg_grad_color(screen, COLOR_BG, 0);
	lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_grad_stop(screen, 200, 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

	header = create_label(screen, &lv_font_montserrat_12, COLOR_SUBTLE, "");
	lv_label_set_text_fmt(header, "PLAYING ON %s", device_name);
	lv_obj_set_style_text_letter_space(header, 1, 0);
	lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 12);

	queue_button = create_icon_button(screen, LV_SYMBOL_LIST, on_queue_open_clicked);
	lv_obj_align(queue_button, LV_ALIGN_TOP_RIGHT, -4, -4);

	cover_box = create_box(screen, COVER_SIZE, COVER_SIZE);
	lv_obj_align(cover_box, LV_ALIGN_TOP_MID, 0, COVER_Y);
	lv_obj_set_style_radius(cover_box, 8, 0);
	lv_obj_set_style_clip_corner(cover_box, true, 0);
	lv_obj_set_style_bg_color(cover_box, COLOR_SURFACE, 0);
	lv_obj_set_style_bg_opa(cover_box, LV_OPA_COVER, 0);

	placeholder = create_label(cover_box, &lv_font_montserrat_28, COLOR_TRACK, LV_SYMBOL_AUDIO);
	lv_obj_center(placeholder);

	cover_image = lv_image_create(cover_box);
	lv_obj_set_size(cover_image, COVER_SIZE, COVER_SIZE);
	lv_image_set_inner_align(cover_image, LV_IMAGE_ALIGN_STRETCH);
	lv_obj_add_flag(cover_image, LV_OBJ_FLAG_HIDDEN);

	title_label = create_label(screen, &lv_font_montserrat_20, COLOR_TEXT, "");
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
	lv_obj_set_width(title_label, CONTENT_W);
	lv_obj_set_pos(title_label, MARGIN, TITLE_Y);

	artist_label = create_label(screen, &lv_font_montserrat_14, COLOR_SUBTLE, "");
	lv_label_set_long_mode(artist_label, LV_LABEL_LONG_MODE_DOTS);
	lv_obj_set_size(artist_label, CONTENT_W, lv_font_get_line_height(&lv_font_montserrat_14));
	lv_obj_set_pos(artist_label, MARGIN, ARTIST_Y);

	progress_slider = create_slider(screen, PROGRESS_Y, MARGIN, CONTENT_W, on_progress_event);

	elapsed_label = create_label(screen, &lv_font_montserrat_12, COLOR_SUBTLE, "");
	lv_obj_set_pos(elapsed_label, MARGIN, TIME_Y);
	duration_label = create_label(screen, &lv_font_montserrat_12, COLOR_SUBTLE, "");
	lv_obj_align(duration_label, LV_ALIGN_TOP_RIGHT, -MARGIN, TIME_Y);

	prev_button = create_button(screen, 80, 48, LV_SYMBOL_PREV, on_prev_clicked, NULL);
	play_button = create_button(screen, 160, 60, LV_SYMBOL_PLAY, on_play_clicked, &play_icon);
	next_button = create_button(screen, 240, 48, LV_SYMBOL_NEXT, on_next_clicked, NULL);
	lv_obj_set_style_bg_color(play_button, COLOR_TEXT, 0);
	lv_obj_set_style_bg_color(play_button, COLOR_SUBTLE, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(play_button, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(play_button, COLOR_BG, 0);
	lv_obj_set_style_text_color(play_button, COLOR_BG, LV_STATE_PRESSED);

	volume_icon = create_label(screen, &lv_font_montserrat_14, COLOR_SUBTLE,
				   LV_SYMBOL_VOLUME_MAX);
	lv_obj_set_pos(volume_icon, MARGIN, VOLUME_Y - 7);
	volume_slider = create_slider(screen, VOLUME_Y, MARGIN + 30, CONTENT_W - 30,
				      on_volume_event);
	lv_slider_set_range(volume_slider, 0, UINT16_MAX);
	lv_slider_set_value(volume_slider, UINT16_MAX, LV_ANIM_OFF);

	create_queue_screen();

	lv_timer_create(on_tick, TICK_MS, NULL);
}

int ui_init(const char *device_name, const struct ui_ops *ui_ops)
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
	create_screen(device_name);
	lvgl_unlock();

	display_blanking_off(display);
	ready = true;
	return 0;
}

static void post(const struct ui_msg *msg)
{
	if (ready && k_msgq_put(&ui_msgq, msg, K_NO_WAIT) != 0) {
		LOG_WRN("UI queue full, update dropped");
	}
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

void ui_queue_reset(uint32_t generation, int first, int count)
{
	const struct ui_msg msg = {
		.type = UI_MSG_QUEUE_RESET,
		.queue_reset = {.generation = generation, .first = first, .count = count},
	};

	post(&msg);
}

void ui_queue_add(uint32_t generation, int index, const char *title, const char *artist,
		  uint32_t duration_ms)
{
	struct ui_msg msg = {
		.type = UI_MSG_QUEUE_ITEM,
		.queue_item = {.generation = generation, .index = index,
			       .duration_ms = duration_ms},
	};

	copy_text(msg.queue_item.title, sizeof(msg.queue_item.title), title);
	copy_text(msg.queue_item.artist, sizeof(msg.queue_item.artist), artist);
	post(&msg);
}

void ui_queue_changed(void)
{
	const struct ui_msg msg = {.type = UI_MSG_QUEUE_CHANGED};

	post(&msg);
}
