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
 * Threading: cspot_connect() spawns the protocol threads. Event and PCM
 * callbacks are invoked from those threads, never from the caller's thread,
 * so handlers must be thread-safe and must not block for long.
 */
#ifndef CSPOT_CSPOT_H_
#define CSPOT_CSPOT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Preferred stream quality. Falls back to 96 kbps when unavailable. */
enum cspot_audio_format {
	CSPOT_FORMAT_OGG_VORBIS_96 = 0,
	CSPOT_FORMAT_OGG_VORBIS_160 = 1,
	CSPOT_FORMAT_OGG_VORBIS_320 = 2,
};

struct cspot_config {
	/** Name shown in the Spotify app. NULL selects CONFIG_CSPOT_DEVICE_NAME. */
	const char *device_name;
	enum cspot_audio_format audio_format;
};

enum cspot_event_type {
	CSPOT_EVENT_PLAY_PAUSE,     /**< event.paused */
	CSPOT_EVENT_VOLUME,         /**< event.volume (0..65535) */
	CSPOT_EVENT_TRACK_INFO,     /**< event.track, valid during the callback */
	CSPOT_EVENT_DISCONNECT,     /**< another device took over playback */
	CSPOT_EVENT_NEXT,
	CSPOT_EVENT_PREV,
	CSPOT_EVENT_SEEK,           /**< event.position_ms */
	CSPOT_EVENT_DEPLETED,       /**< queue finished */
	CSPOT_EVENT_FLUSH,          /**< drop buffered audio */
	CSPOT_EVENT_PLAYBACK_START, /**< event.position_ms */
};

struct cspot_track_info {
	const char *name;
	const char *album;
	const char *artist;
	const char *image_url;
	const char *track_id;
	uint32_t duration_ms;
	uint32_t number;
	uint32_t disc_number;
};

struct cspot_event {
	enum cspot_event_type type;
	union {
		bool paused;
		uint16_t volume;
		uint32_t position_ms;
		struct cspot_track_info track;
	};
};

typedef void (*cspot_event_cb_t)(const struct cspot_event *event, void *user_data);

/**
 * Receives decoded PCM (16-bit signed little-endian, interleaved stereo,
 * 44.1 kHz). Returns the number of bytes accepted; returning 0 makes the
 * player retry the same data shortly, which is how back-pressure and pausing
 * are implemented.
 */
typedef size_t (*cspot_pcm_cb_t)(const uint8_t *pcm, size_t len, void *user_data);

/* --- Lifecycle ----------------------------------------------------------- */

/** Initialises the library and generates the device identity. */
int cspot_init(const struct cspot_config *config);

const char *cspot_device_id(void);
const char *cspot_device_name(void);

/* --- Credentials --------------------------------------------------------- */

/** Uses username/password credentials (premium accounts only). */
int cspot_credentials_set_user_pass(const char *username, const char *password);

/** Loads credentials previously produced by cspot_credentials_save_json(). */
int cspot_credentials_load_json(const char *json);

/**
 * Serialises the current credentials. After a successful cspot_connect()
 * these are Spotify's reusable stored credentials, suitable for persisting
 * and loading on the next boot to skip the zeroconf step.
 *
 * @return number of bytes written (excluding NUL) or negative errno.
 */
int cspot_credentials_save_json(char *buf, size_t size);

bool cspot_credentials_available(void);

/* --- Zeroconf (CONFIG_CSPOT_ZEROCONF) ------------------------------------ */

/**
 * Advertises the device via mDNS/DNS-SD and serves the Spotify zeroconf
 * endpoint (/spotify_info) so the Spotify app can hand over credentials.
 */
int cspot_zeroconf_start(void);

/**
 * Blocks until the Spotify app delivered credentials.
 * @param timeout_ms  milliseconds to wait, or a negative value to wait forever.
 * @return 0 on success, -EAGAIN on timeout.
 */
int cspot_zeroconf_wait(int32_t timeout_ms);

void cspot_zeroconf_stop(void);

/* --- Session ------------------------------------------------------------- */

/**
 * Connects to Spotify with the available credentials, authenticates and
 * starts the protocol threads. Blocks until the session is up.
 */
int cspot_connect(cspot_event_cb_t event_cb, cspot_pcm_cb_t pcm_cb, void *user_data);

void cspot_disconnect(void);

bool cspot_is_connected(void);

/* --- Playback control ---------------------------------------------------- */

void cspot_set_pause(bool paused);
bool cspot_next(void);
bool cspot_previous(void);

/** Reports a local volume change (0..65535) to Spotify. */
void cspot_set_volume(uint16_t volume);

/** Call when the first PCM of a new track reached the output. */
void cspot_notify_audio_reached_playback(void);

/** Call when the output drained after the last track. */
void cspot_notify_audio_ended(void);

/** Reports the playback position to keep the Spotify UI in sync. */
void cspot_update_position_ms(uint32_t position_ms);

#ifdef __cplusplus
}
#endif

#endif /* CSPOT_CSPOT_H_ */
