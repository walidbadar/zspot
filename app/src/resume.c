/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "resume.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "last_track.h"
#include "library.h"
#include "ui.h"
#include "webapi.h"

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

/* Without playback this long after Spotify accepted the request, Play is offered again */
#define START_TIMEOUT K_SECONDS(10)

static K_MUTEX_DEFINE(lock);

/* The song that plays, or the one that waits to be started after the restart */
static struct last_track current;
static bool current_valid;
/* current differs from what is stored */
static bool dirty;
/* What the queue plays from; goes with the tracks that begin from now on */
static char queue_context[LAST_TRACK_CONTEXT_MAX];

/* The song of resume_show() has not been started yet */
static atomic_t waiting;
/* Something was started since the restart */
static bool begun;
static void (*show_position)(uint32_t position_ms);

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
		(void)last_track_store(&current);
	}
	k_mutex_unlock(&lock);
}

static K_WORK_DEFINE(save_work, save_handler);

void resume_set_context(const char *context_uri)
{
	k_mutex_lock(&lock, K_FOREVER);
	queue_context[0] = '\0';
	if (context_uri != NULL && strlen(context_uri) < sizeof(queue_context)) {
		strcpy(queue_context, context_uri);
	}
	k_mutex_unlock(&lock);
}

void resume_set_track(const struct zspot_track_info *track, uint32_t position_ms, bool idle)
{
	k_mutex_lock(&lock, K_FOREVER);
	current_valid = last_track_set(&current, track->name, track->artist, track->image_url,
				       track->uri, track->duration_ms);
	if (current_valid) {
		last_track_set_context(&current, queue_context);
		current.position_ms = MIN(position_ms, current.duration_ms);
		dirty = true;
	}
	k_mutex_unlock(&lock);

	/* Otherwise with the next call of resume_set_position() */
	if (idle) {
		k_work_submit(&save_work);
	}
}

void resume_set_position(uint32_t position_ms)
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

static K_WORK_DELAYABLE_DEFINE(start_timeout_work, start_timeout_handler);

void resume_playback_begun(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	begun = true;
	if (atomic_clear(&waiting)) {
		(void)k_work_cancel_delayable(&start_timeout_work);
	}
	k_mutex_unlock(&lock);
}

bool resume_show(void (*set_position)(uint32_t position_ms))
{
	bool shown = false;

	/* A song that cannot be started is of no use on the screen */
	if (!webapi_configured()) {
		return false;
	}

	/* Under the lock, so that a track that begins right now is shown after this one */
	k_mutex_lock(&lock, K_FOREVER);
	if (!begun && last_track_load(&current) == 0) {
		current_valid = true;
		dirty = false;
		show_position = set_position;
		atomic_set(&waiting, 1);

		set_position(current.position_ms);
		ui_show_track(current.title, current.artist, current.image_url, current.duration_ms);
		ui_set_paused(true);
		shown = true;
	}
	k_mutex_unlock(&lock);

	return shown;
}

bool resume_waiting(void)
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

/* From the library thread */
static void on_play_request_done(bool accepted)
{
	if (!accepted) {
		start_failed();
	} else if (atomic_get(&waiting)) {
		/* The player takes over from here, as when the app starts a track */
		(void)k_work_reschedule(&start_timeout_work, START_TIMEOUT);
	}
}

bool resume_pause(bool pause)
{
	char uri[LAST_TRACK_URI_MAX];
	char context[LAST_TRACK_CONTEXT_MAX];
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

	ret = library_play(uri, context, position_ms, on_play_request_done);
	if (ret == 0) {
		ui_set_paused(false);
	} else if (ret != -EBUSY) {
		LOG_WRN("Cannot ask for the last song (%d)", ret);
		start_failed();
	}
	return true;
}

bool resume_seek(uint32_t position_ms)
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
