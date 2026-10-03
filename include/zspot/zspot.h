/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * cspot-zephyr: Spotify Connect receiver for Zephyr RTOS.
 *
 * Public C API. The protocol implementation (derived from cspot,
 * https://github.com/feelfreelinux/cspot, GPL-3.0) is internal to the
 * library; applications only need this header.
 *
 * Threading: zspot_connect() spawns the protocol threads. Event and PCM
 * callbacks are invoked from those threads, never from the caller's thread,
 * so handlers must be thread-safe and must not block for long.
 */
#ifndef ZSPOT_ZSPOT_H_
#define ZSPOT_ZSPOT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Preferred stream quality. Falls back to 96 kbps when unavailable. */
enum zspot_audio_format {
	ZSPOT_FORMAT_OGG_VORBIS_96 = 0,
	ZSPOT_FORMAT_OGG_VORBIS_160 = 1,
	ZSPOT_FORMAT_OGG_VORBIS_320 = 2,
};

struct zspot_config {
	/** Name shown in the Spotify app. NULL selects CONFIG_ZSPOT_DEVICE_NAME. */
	const char *device_name;
	enum zspot_audio_format audio_format;
	/** Volume reported to Spotify at connect time, 0..65535. */
	uint16_t initial_volume;
};

enum zspot_event_type {
	ZSPOT_EVENT_PLAY_PAUSE,     /**< event.paused */
	ZSPOT_EVENT_VOLUME,         /**< event.volume (0..65535) */
	ZSPOT_EVENT_TRACK_INFO,     /**< event.track, valid during the callback */
	ZSPOT_EVENT_DISCONNECT,     /**< another device took over playback */
	ZSPOT_EVENT_NEXT,
	ZSPOT_EVENT_PREV,
	ZSPOT_EVENT_SEEK,           /**< event.position_ms */
	ZSPOT_EVENT_DEPLETED,       /**< queue finished */
	ZSPOT_EVENT_FLUSH,          /**< drop buffered audio */
	ZSPOT_EVENT_PLAYBACK_START, /**< event.position_ms */
};

struct zspot_track_info {
	const char *name;
	const char *album;
	const char *artist;
	const char *image_url;
	const char *track_id;
	uint32_t duration_ms;
	uint32_t number;
	uint32_t disc_number;
};

struct zspot_event {
	enum zspot_event_type type;
	union {
		bool paused;
		uint16_t volume;
		uint32_t position_ms;
		struct zspot_track_info track;
	};
};

typedef void (*zspot_event_cb_t)(const struct zspot_event *event, void *user_data);

/**
 * Receives decoded PCM (16-bit signed little-endian, interleaved stereo,
 * 44.1 kHz). Returns the number of bytes accepted; returning 0 makes the
 * player retry the same data shortly, which is how back-pressure and pausing
 * are implemented.
 */
typedef size_t (*zspot_pcm_cb_t)(const uint8_t *pcm, size_t len, void *user_data);

/* --- Lifecycle ----------------------------------------------------------- */

/** Initialises the library and generates the device identity. */
int zspot_init(const struct zspot_config *config);

const char *zspot_device_id(void);
const char *zspot_device_name(void);

/* --- Credentials --------------------------------------------------------- */

/** Uses username/password credentials (premium accounts only). */
int zspot_credentials_set_user_pass(const char *username, const char *password);

/** Loads credentials previously produced by zspot_credentials_save_json(). */
int zspot_credentials_load_json(const char *json);

/**
 * Serialises the current credentials. After a successful zspot_connect()
 * these are Spotify's reusable stored credentials, suitable for persisting
 * and loading on the next boot to skip the zeroconf step.
 *
 * @return number of bytes written (excluding NUL) or negative errno.
 */
int zspot_credentials_save_json(char *buf, size_t size);

bool zspot_credentials_available(void);

/** Forgets the current credentials, e.g. after Spotify rejected them. */
void zspot_credentials_clear(void);

/* --- Zeroconf (CONFIG_ZSPOT_ZEROCONF) ------------------------------------ */

/**
 * Advertises the device via mDNS/DNS-SD and serves the Spotify zeroconf
 * endpoint (/spotify_info) so the Spotify app can hand over credentials.
 */
int zspot_zeroconf_start(void);

/**
 * Blocks until the Spotify app delivered credentials.
 * @param timeout_ms  milliseconds to wait, or a negative value to wait forever.
 * @return 0 on success, -EAGAIN on timeout.
 */
int zspot_zeroconf_wait(int32_t timeout_ms);

void zspot_zeroconf_stop(void);

/* --- Session ------------------------------------------------------------- */

/**
 * Connects to Spotify with the available credentials, authenticates and
 * starts the protocol threads. Blocks until the session is up.
 */
int zspot_connect(zspot_event_cb_t event_cb, zspot_pcm_cb_t pcm_cb, void *user_data);

void zspot_disconnect(void);

bool zspot_is_connected(void);

/* --- Playback control ---------------------------------------------------- */

void zspot_set_pause(bool paused);
bool zspot_next(void);
bool zspot_previous(void);

/** Reports a local volume change (0..65535) to Spotify. */
void zspot_set_volume(uint16_t volume);

/** Call when the first PCM of a new track reached the output. */
void zspot_notify_audio_reached_playback(void);

/** Call when the output drained after the last track. */
void zspot_notify_audio_ended(void);

/** Reports the playback position to keep the Spotify UI in sync. */
void zspot_update_position_ms(uint32_t position_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_ZSPOT_H_ */
