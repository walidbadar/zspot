/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Spotify Web API access with the user's own developer application. The
 * session's access token is issued to Spotify's own client and is rate
 * limited on the public Web API (HTTP 429), so requests are authorised with
 * a token obtained through OAuth instead: with a client ID, a client secret
 * and a refresh token, the latter produced once by
 * scripts/spotify_authorize.py.
 *
 * A build carries these credentials when it is told to
 * (CONFIG_ZSPOT_WEB_CREDENTIALS_BUILT_IN). A player that keeps credentials
 * (CONFIG_ZSPOT_CREDENTIALS_STORED) stores them encrypted at its first start,
 * and uses the stored ones when a later build comes without: one build
 * carries them to the device, and the firmware after it no longer holds them.
 *
 * A request blocks for as long as the network takes. The requests have a
 * thread of their own, which webapi_submit() gives work to. Nothing here
 * needs the screen: a player without one has the Web API as well.
 */

#ifndef ZSPOT_SAMPLE_WEBAPI_H_
#define ZSPOT_SAMPLE_WEBAPI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/**
 * Takes up the credentials of the build, or else the stored ones, and starts
 * the thread of the requests. Reads the flash and may write to it: call from
 * the main thread, before anything else here.
 *
 * @return 0 on success or -ENOMEM
 */
int webapi_init(void);

/** Whether there are credentials for the requests. */
bool webapi_configured(void);

/**
 * Performs a Web API request, fetching or renewing the access token first
 * when necessary. Blocks; a request from another thread waits for it.
 *
 * @param body  JSON request body or NULL
 * @param buf   receives the response body, not NUL terminated
 * @param len   receives the number of bytes stored in @p buf
 * @return the HTTP status code, -ENOENT when the application credentials are
 *         not configured, -EACCES when Spotify rejected them, or another
 *         negative errno.
 */
int webapi_request(const char *method, const char *url, const char *body, uint8_t *buf, size_t size,
		   size_t *len);

/**
 * Runs @p work on the thread of the requests, where it may block in
 * webapi_request(). The stack of that thread may be in external RAM: no
 * writing to the flash from there.
 *
 * @return as k_work_submit_to_queue(), or -ENODEV without webapi_init()
 */
int webapi_submit(struct k_work *work);

/**
 * Asks Spotify to start playback on this device. Blocks: for work given to
 * webapi_submit().
 *
 * @param body  what to play, as the JSON of PUT /me/player/play
 * @return 0 when Spotify accepted the request or a negative errno
 */
int webapi_start_playback(const char *body);

/** Reports from the thread of the requests whether Spotify accepted one. */
typedef void (*webapi_play_cb_t)(bool accepted);

/**
 * Plays a track on this device, from @p position_ms on. Does not block.
 *
 * @param track_uri    "spotify:track:..." or "spotify:episode:..."
 * @param context_uri  the playlist or album to go on with after the track,
 *                     or empty; without one, or when the track is no longer
 *                     part of it, the track is played alone
 * @return 0 when the request is on its way and @p done will be called;
 *         -EBUSY while an earlier one is, or another negative errno
 */
int webapi_play(const char *track_uri, const char *context_uri, uint32_t position_ms,
		webapi_play_cb_t done);

#endif /* ZSPOT_SAMPLE_WEBAPI_H_ */
