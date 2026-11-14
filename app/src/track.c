/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "track.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "ui.h"
#include "webapi.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define TRACK_SETTING "zspot/last"
/* Layout of struct track; a stored track of another one is not read */
#define TRACK_FORMAT  1

/* "spotify:episode:" and the 22 digits of an id, with the terminator */
#define TRACK_URI_MAX     40
/* "spotify:playlist:" and an id; longer contexts are not kept */
#define TRACK_CONTEXT_MAX 64
/* As long as the cover loader takes them */
#define TRACK_IMAGE_MAX   128

/* Without playback this long after Spotify accepted the request, Play is offered again */
#define TRACK_START_TIMEOUT K_SECONDS(10)

/*
 * What the Now Playing screen shows after a restart, and what is needed to
 * play it again on this device. Stored as it is: track_fill() leaves no
 * stray bytes behind the texts.
 */
struct track {
	/* Tells the layout; set by track_fill() */
	uint32_t format;
	char title[UI_TEXT_MAX];
	char artist[UI_TEXT_MAX];
	char image_url[TRACK_IMAGE_MAX];
	/* "spotify:track:..." or "spotify:episode:..." */
	char uri[TRACK_URI_MAX];
	/* What it was played from, e.g. "spotify:playlist:..."; empty without */
	char context[TRACK_CONTEXT_MAX];
	uint32_t duration_ms;
	/* Where it was paused; 0 while it played */
	uint32_t position_ms;
};

static void save_handler(struct k_work *work);
static void start_timeout_handler(struct k_work *work);

static K_MUTEX_DEFINE(lock);

/* The song that plays, or the one that waits to be started after the restart */
static struct track current;
static bool current_valid;
/* current differs from what is stored */
static bool dirty;
/* What the queue plays from; goes with the tracks that begin from now on */
static char queue_context[TRACK_CONTEXT_MAX];

/* The song of track_show() has not been started yet */
static atomic_t waiting;
/* Something was started since the restart */
static bool begun;
static void (*show_position)(uint32_t position_ms);

static K_WORK_DEFINE(save_work, save_handler);
static K_WORK_DELAYABLE_DEFINE(start_timeout_work, start_timeout_handler);

/* --- The stored song ------------------------------------------------------- */

/*
 * The URIs go into the JSON of a Web API request as they are: only what
 * needs no escaping there.
 */
static bool is_uri(const char *text, size_t size)
{
	const char *end = memchr(text, '\0', size);
	size_t len;

	if (end == NULL || strncmp(text, "spotify:", 8) != 0) {
		return false;
	}
	len = end - text;
	for (size_t i = 0; i < len; i++) {
		if (!isalnum((unsigned char)text[i]) && text[i] != ':') {
			return false;
		}
	}
	return true;
}

static bool is_track_uri(const char *uri, size_t size)
{
	return is_uri(uri, size) && (strncmp(uri, "spotify:track:", 14) == 0 ||
				     strncmp(uri, "spotify:episode:", 16) == 0);
}

/* Copies up to the terminator, without splitting a UTF-8 sequence. */
static void copy_text(char *out, size_t size, const char *text)
{
	size_t len = MIN(strlen(text), size - 1);

	while (len > 0 && ((uint8_t)text[len] & 0xC0) == 0x80) {
		len--;
	}
	memcpy(out, text, len);
	out[len] = '\0';
}

/*
 * Fills in @p track, without a context and at position 0; text that is too
 * long is cut. Returns false when @p uri is not the URI of a track or an
 * episode: @p track is not usable then.
 */
static bool track_fill(struct track *track, const char *title, const char *artist,
		       const char *image_url, const char *uri, uint32_t duration_ms)
{
	if (uri == NULL || !is_track_uri(uri, sizeof(track->uri))) {
		return false;
	}

	/* The same track gives the same bytes, which the settings do not write twice */
	memset(track, 0, sizeof(*track));
	track->format = TRACK_FORMAT;
	copy_text(track->title, sizeof(track->title), title != NULL ? title : "");
	copy_text(track->artist, sizeof(track->artist), artist != NULL ? artist : "");
	strcpy(track->uri, uri);
	track->duration_ms = duration_ms;

	/* A cover whose address is too long is not shown */
	if (image_url != NULL && strlen(image_url) < sizeof(track->image_url)) {
		strcpy(track->image_url, image_url);
	}
	return true;
}

/*
 * Sets what the track is played from. NULL or a context that cannot be kept
 * leaves it without one, as the track can be played alone.
 */
static void track_fill_context(struct track *track, const char *context)
{
	memset(track->context, 0, sizeof(track->context));
	if (context != NULL && is_uri(context, sizeof(track->context))) {
		strcpy(track->context, context);
	}
}

/* What was read from the flash is not taken on trust. */
static bool track_is_valid(struct track *track)
{
	track->title[sizeof(track->title) - 1] = '\0';
	track->artist[sizeof(track->artist) - 1] = '\0';
	track->image_url[sizeof(track->image_url) - 1] = '\0';

	return track->format == TRACK_FORMAT && is_track_uri(track->uri, sizeof(track->uri)) &&
	       (track->context[0] == '\0' || is_uri(track->context, sizeof(track->context))) &&
	       track->position_ms <= track->duration_ms;
}

/* Removes the stored track. */
static int track_forget(void)
{
	int ret = settings_subsys_init();

	if (ret == 0) {
		ret = settings_delete(TRACK_SETTING);
	}
	if (ret != 0) {
		LOG_ERR("Cannot remove the last track (%d)", ret);
	}
	return ret;
}

/*
 * Reads the stored track: 0, -ENOENT when none is stored, or another negative
 * errno. A stored track that cannot be used is removed.
 */
static int track_load(struct track *track)
{
	ssize_t len;
	int ret;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* The length of the value, also of one that does not fit; 0 without one */
	len = settings_load_one(TRACK_SETTING, track, sizeof(*track));
	if (len == 0) {
		return -ENOENT;
	}
	if (len < 0) {
		LOG_ERR("Cannot read the last track (%d)", (int)len);
		return (int)len;
	}
	if (len != sizeof(*track) || !track_is_valid(track)) {
		LOG_WRN("The stored last track cannot be used, removing it");
		(void)track_forget();
		return -EINVAL;
	}
	return 0;
}

/*
 * Stores @p track, which track_fill() or track_load() filled in.
 * Writes to the flash, which stops the whole system for as long as an erase
 * takes: only while no audio goes out.
 */
static int track_store(const struct track *track)
{
	int ret;

	if (track->format != TRACK_FORMAT) {
		return -EINVAL;
	}

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Settings are not available (%d)", ret);
		return ret;
	}

	/* NVS and ZMS write nothing when the value is the one they hold. */
	ret = settings_save_one(TRACK_SETTING, track, sizeof(*track));
	if (ret != 0) {
		LOG_ERR("Cannot store the last track (%d)", ret);
	}
	return ret;
}

/* --- What plays, and the song after a restart ------------------------------- */

/*
 * The flash is written from the system work queue: the callbacks of the
 * library must not take that long, and their stacks may be in external RAM,
 * which is out of reach while the flash is written.
 */
static void save_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&lock, K_FOREVER);
	if (dirty && current_valid) {
		dirty = false;
		(void)track_store(&current);
	}
	k_mutex_unlock(&lock);
}

void track_set_context(const char *context_uri)
{
	k_mutex_lock(&lock, K_FOREVER);
	queue_context[0] = '\0';
	if (context_uri != NULL && strlen(context_uri) < sizeof(queue_context)) {
		strcpy(queue_context, context_uri);
	}
	k_mutex_unlock(&lock);
}

void track_set(const struct zspot_track_info *track, uint32_t position_ms, bool idle)
{
	k_mutex_lock(&lock, K_FOREVER);
	current_valid = track_fill(&current, track->name, track->artist, track->image_url,
				   track->uri, track->duration_ms);
	if (current_valid) {
		track_fill_context(&current, queue_context);
		current.position_ms = MIN(position_ms, current.duration_ms);
		dirty = true;
	}
	k_mutex_unlock(&lock);

	/* Otherwise with the next call of track_set_position() */
	if (idle) {
		k_work_submit(&save_work);
	}
}

void track_set_position(uint32_t position_ms)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (current_valid) {
		current.position_ms = MIN(position_ms, current.duration_ms);
		dirty = true;
	}
	k_mutex_unlock(&lock);

	k_work_submit(&save_work);
}

/* The song was started, but nothing plays: let Play be tapped again. */
static void start_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (atomic_get(&waiting)) {
		ui_set_paused(true);
	}
}

void track_playback_begun(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	begun = true;
	if (atomic_clear(&waiting)) {
		(void)k_work_cancel_delayable(&start_timeout_work);
	}
	k_mutex_unlock(&lock);
}

bool track_show(void (*set_position)(uint32_t position_ms))
{
	bool shown = false;

	/* A song that cannot be started is of no use on the screen */
	if (!webapi_configured()) {
		return false;
	}

	/* Under the lock, so that a track that begins right now is shown after this one */
	k_mutex_lock(&lock, K_FOREVER);
	if (!begun && track_load(&current) == 0) {
		current_valid = true;
		dirty = false;
		show_position = set_position;
		atomic_set(&waiting, 1);

		set_position(current.position_ms);
		ui_show_track(current.title, current.artist, current.image_url,
			      current.duration_ms);
		ui_set_paused(true);
		shown = true;
	}
	k_mutex_unlock(&lock);

	return shown;
}

bool track_waiting(void)
{
	return atomic_get(&waiting);
}

/* The song could not be started: say so, and how to go on. */
static void start_failed(void)
{
	if (atomic_clear(&waiting)) {
		(void)k_work_cancel_delayable(&start_timeout_work);
		ui_show_message("Cannot play the last song",
				"Pick this device in Spotify to start playing");
	}
}

/* From the thread of the Web API */
static void on_play_request_done(bool accepted)
{
	if (!accepted) {
		start_failed();
	} else if (atomic_get(&waiting)) {
		/* The player takes over from here, as when the app starts a track */
		(void)k_work_reschedule(&start_timeout_work, TRACK_START_TIMEOUT);
	}
}

bool track_pause(bool pause)
{
	char uri[TRACK_URI_MAX];
	char context[TRACK_CONTEXT_MAX];
	uint32_t position_ms;
	int ret;

	if (!atomic_get(&waiting)) {
		return false;
	}
	/* It shows as playing while it is being started: nothing to pause yet */
	if (pause) {
		return true;
	}

	k_mutex_lock(&lock, K_FOREVER);
	strcpy(uri, current.uri);
	strcpy(context, current.context);
	position_ms = current.position_ms;
	k_mutex_unlock(&lock);

	ret = webapi_play(uri, context, position_ms, on_play_request_done);
	if (ret == 0) {
		ui_set_paused(false);
	} else if (ret != -EBUSY) {
		LOG_WRN("Cannot ask for the last song (%d)", ret);
		start_failed();
	}
	return true;
}

bool track_seek(uint32_t position_ms)
{
	if (!atomic_get(&waiting)) {
		return false;
	}

	k_mutex_lock(&lock, K_FOREVER);
	current.position_ms = MIN(position_ms, current.duration_ms);
	dirty = true;
	show_position(current.position_ms);
	k_mutex_unlock(&lock);

	/* Nothing plays while the song waits */
	k_work_submit(&save_work);
	return true;
}
