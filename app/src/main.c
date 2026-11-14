/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * zspot player: Spotify Connect receiver with I2S output and a touchscreen UI.
 *
 * Flow: bring the network up, advertise the device, wait for the Spotify app
 * to hand over credentials (or use stored ones), connect, and route decoded
 * PCM to the I2S sink. With CONFIG_ZSPOT_UI a "Now Playing" screen shows
 * the track and controls playback.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>

#include <zspot/zspot.h>

#if defined(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED)
#include "wifi.h"
#endif
#if defined(CONFIG_ZSPOT_I2S_SINK)
#include <zspot/zspot_i2s_sink.h>
#endif
#include "pcm_tap.h"
#include "ui.h"
#include "battery.h"
#include "certs.h"
#include "credentials.h"
#include "resume.h"
#if defined(CONFIG_ZSPOT_UI)
#include "library.h"
#include "lyrics.h"
#endif

/* Decoded PCM: 44.1 kHz, 16 bit, stereo */
#define PCM_BYTES_PER_SECOND (44100 * 4)

/* What the I2S driver has left to play when the sink has passed everything on */
#if defined(CONFIG_ZSPOT_I2S_SINK)
#define SINK_TAIL_MS                                                                               \
	(CONFIG_ZSPOT_I2S_BLOCK_COUNT * CONFIG_ZSPOT_I2S_BLOCK_SIZE * 1000 / PCM_BYTES_PER_SECOND)
#else
#define SINK_TAIL_MS 0
#endif

LOG_MODULE_REGISTER(zspot_player, LOG_LEVEL_INF);

static K_SEM_DEFINE(ipv4_ready, 0, 1);
static struct net_mgmt_event_callback net_cb;

static volatile bool paused;

/*
 * Playback position: where the track was (re)started plus the PCM handed to
 * the sink since then.
 */
static volatile uint32_t position_base_ms;
static atomic_t position_bytes;
/* PLAYBACK_START already set the position of the track that begins next */
static bool position_preset;
/* The track that begins was started anew: the sink is empty, no audio goes out */
static bool output_idle;

static void position_set(uint32_t position_ms)
{
	position_base_ms = position_ms;
	atomic_clear(&position_bytes);
}

/*
 * End of playback: when the decoder is through with the last track of the
 * queue, the sink still plays what it buffered. The main thread tells Spotify
 * that playback ended once that audio is out, unless something else was
 * started in the meantime.
 */
static K_SEM_DEFINE(end_sem, 0, 1);
static atomic_t end_pending;
/* PCM went to the sink since the last end; a track that cannot be loaded ends without */
static atomic_t audio_played;

/* Something else plays or the audio was dropped: there is no end to report. */
static void end_cancel(void)
{
	atomic_clear(&end_pending);
}

/* PCM that the sink accepted and has not passed on to the I2S driver yet */
static size_t sink_buffered(void)
{
#if defined(CONFIG_ZSPOT_I2S_SINK)
	return zspot_i2s_sink_buffered();
#else
	return 0;
#endif
}

/* Polled by the Now Playing screen */
static __maybe_unused uint32_t position_get(void)
{
	int64_t bytes = atomic_get(&position_bytes);

	/* What the sink still holds is not audible yet. */
	bytes = MAX(bytes - (int64_t)sink_buffered(), 0);
	return position_base_ms + (uint32_t)(bytes * 1000 / PCM_BYTES_PER_SECOND);
}

static void net_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			      struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&ipv4_ready);
	}
}

static int network_connect(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_ERR("No network interface");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&net_cb, net_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&net_cb);

#if defined(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED)
	/* Credentials come from the wifi_credentials library (settings backend). */
	int ret = wifi_init();

	if (ret != 0) {
		LOG_ERR("Wi-Fi init failed (%d)", ret);
		return ret;
	}
#endif

#if defined(CONFIG_NET_DHCPV4)
	/* The client waits for the interface to come up before sending DISCOVER. */
	net_dhcpv4_start(iface);
#endif

	/* A static address (CONFIG_NET_CONFIG_SETTINGS) is assigned before main() runs. */
	if (net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) == NULL) {
		LOG_INF("Waiting for an IPv4 address...");
		k_sem_take(&ipv4_ready, K_FOREVER);
	}
	LOG_INF("Network ready");
	ui_set_network(true);
	return 0;
}

static void sink_flush(void)
{
#if defined(CONFIG_ZSPOT_I2S_SINK)
	zspot_i2s_sink_flush();
#endif
}

static void sink_set_volume(uint16_t volume)
{
#if defined(CONFIG_ZSPOT_I2S_SINK)
	zspot_i2s_sink_set_volume(volume);
#else
	ARG_UNUSED(volume);
#endif
}

static int sink_init(void)
{
#if defined(CONFIG_ZSPOT_I2S_SINK)
#if defined(CONFIG_I2S_NATIVE_SIM)
	/* An ALSA device ("alsa:<name>") or a host file */
	LOG_INF("Audio output: %s (--%s_tx=<path> overrides)", CONFIG_I2S_NATIVE_SIM_TX_PATH,
		DEVICE_DT_NAME(DT_ALIAS(zspot_i2s)));
#endif
	return zspot_i2s_sink_init(DEVICE_DT_GET(DT_ALIAS(zspot_i2s)),
				   DEVICE_DT_GET_OR_NULL(DT_ALIAS(zspot_codec)), 44100, 2, 16);
#else
	return 0;
#endif
}

static size_t sink_write(const uint8_t *pcm, size_t len, void *user_data)
{
#if defined(CONFIG_ZSPOT_I2S_SINK)
	return zspot_i2s_sink_write(pcm, len, user_data);
#else
	ARG_UNUSED(pcm);
	ARG_UNUSED(user_data);
	return len;
#endif
}

static void on_event(const struct zspot_event *event, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (event->type) {
	case ZSPOT_EVENT_PLAY_PAUSE:
		LOG_INF("%s", event->paused ? "Paused" : "Playing");
		paused = event->paused;
#if defined(CONFIG_ZSPOT_I2S_SINK)
		zspot_i2s_sink_set_paused(event->paused);
#endif
		ui_set_paused(event->paused);
		/* Not for a track that is loaded paused: it is not known yet */
		if (event->paused && !position_preset) {
			resume_set_position(position_get());
		}
		break;
	case ZSPOT_EVENT_VOLUME:
		LOG_INF("Volume %u", event->volume);
		sink_set_volume(event->volume);
		ui_set_volume(event->volume);
		break;
	case ZSPOT_EVENT_TRACK_INFO:
		LOG_INF("Now playing: %s - %s (%s)", event->track.artist, event->track.name,
			event->track.album);
		ui_show_track(event->track.name, event->track.artist, event->track.image_url,
			      event->track.duration_ms);
		resume_set_track(&event->track, position_get(), output_idle);
		break;
	case ZSPOT_EVENT_SEEK:
		LOG_INF("Seek to %u ms", event->position_ms);
		end_cancel();
		sink_flush();
		position_set(event->position_ms);
		resume_set_position(event->position_ms);
		break;
	case ZSPOT_EVENT_FLUSH:
		end_cancel();
		sink_flush();
		break;
	case ZSPOT_EVENT_PLAYBACK_START:
		end_cancel();
		resume_playback_begun();
		sink_flush();
		position_set(event->position_ms);
		position_preset = true;
		break;
	case ZSPOT_EVENT_TRACK_BEGIN:
		end_cancel();
		resume_playback_begun();
		/* A gapless transition comes without PLAYBACK_START. */
		if (!position_preset) {
			position_set(0);
		}
		output_idle = position_preset;
		position_preset = false;
		zspot_notify_audio_reached_playback();
		break;
	case ZSPOT_EVENT_QUEUE_CHANGED:
		end_cancel();
		resume_set_context(event->context_uri);
		resume_playback_begun();
		break;
	case ZSPOT_EVENT_DISCONNECT:
		LOG_INF("Playback moved to another device");
		end_cancel();
		resume_set_position(position_get());
		sink_flush();
		ui_show_message("Playing on another device",
				"Pick this device in Spotify to listen here");
		break;
	case ZSPOT_EVENT_NEXT:
	case ZSPOT_EVENT_PREV:
		end_cancel();
		sink_flush();
		break;
	case ZSPOT_EVENT_DEPLETED:
		LOG_INF("Queue finished");
		if (atomic_clear(&audio_played)) {
			atomic_set(&end_pending, 1);
			k_sem_give(&end_sem);
		}
		break;
	case ZSPOT_EVENT_REPEAT:
		LOG_INF("Repeat %s", event->repeat == ZSPOT_REPEAT_TRACK ? "track"
				     : event->repeat == ZSPOT_REPEAT_ALL ? "all"
									  : "off");
		ui_set_repeat((enum ui_repeat)event->repeat);
		break;
	case ZSPOT_EVENT_CONNECTION_LOST:
		LOG_WRN("Connection to Spotify lost, reconnecting");
		ui_set_network(false);
		break;
	case ZSPOT_EVENT_CONNECTION_RESTORED:
		LOG_INF("Connection to Spotify restored");
		ui_set_network(true);
		break;
	}
}

static size_t on_pcm(const uint8_t *pcm, size_t len, void *user_data)
{
	size_t accepted;

	if (paused) {
		return 0; /* player retries shortly */
	}

	accepted = sink_write(pcm, len, user_data);
	if (accepted > 0) {
		atomic_set(&audio_played, 1);
	}
	atomic_add(&position_bytes, accepted);
	pcm_tap_write(pcm, accepted);
	return accepted;
}

/*
 * Waits for the end of the queue and reports it when the last audio has been
 * played. Runs on the main thread: the event comes from the player thread,
 * which must not be held up for that long.
 */
static void end_report(void)
{
	while (true) {
		k_sem_take(&end_sem, K_FOREVER);

		/* A pause keeps the audio in the sink, and the end waiting. */
		while (atomic_get(&end_pending) && sink_buffered() > 0) {
			k_msleep(50);
		}
		if (!atomic_get(&end_pending)) {
			continue;
		}
		k_msleep(SINK_TAIL_MS);

		if (atomic_cas(&end_pending, 1, 0)) {
			LOG_INF("Playback ended");
			zspot_notify_audio_ended();
		}
	}
}

#if defined(CONFIG_ZSPOT_UI)
/*
 * While the song shown after a restart waits to be started, Play and the
 * progress slider act on it; there is no queue yet to skip in.
 */
static void ui_pause(bool pause)
{
	if (!resume_pause(pause)) {
		zspot_set_pause(pause);
	}
}

static void ui_seek(uint32_t position_ms)
{
	if (!resume_seek(position_ms)) {
		zspot_seek(position_ms);
	}
}

static void ui_next(void)
{
	if (!resume_waiting() && zspot_next()) {
		end_cancel();
		sink_flush();
	}
}

static void ui_previous(void)
{
	if (!resume_waiting() && zspot_previous()) {
		end_cancel();
		sink_flush();
	}
}

/* The button shows the modes of the library under its own names */
BUILD_ASSERT((int)UI_REPEAT_OFF == (int)ZSPOT_REPEAT_OFF &&
	     (int)UI_REPEAT_ALL == (int)ZSPOT_REPEAT_ALL &&
	     (int)UI_REPEAT_TRACK == (int)ZSPOT_REPEAT_TRACK);

static void ui_repeat(enum ui_repeat mode)
{
	zspot_set_repeat((enum zspot_repeat)mode);
}

static void ui_volume(uint16_t volume, bool commit)
{
	sink_set_volume(volume);
	if (commit) {
		zspot_set_volume(volume);
	}
}

#if !defined(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED)
/* Boards without Wi-Fi, such as native_sim, are connected by other means. */
static void wifi_scan(void)
{
	ui_list_reset(UI_LIST_WIFI, 1, "Wi-Fi", true, "This board has no Wi-Fi");
}

static void wifi_join(const char *ssid, const char *password)
{
	ARG_UNUSED(ssid);
	ARG_UNUSED(password);
}

static void wifi_forget(const char *ssid)
{
	ARG_UNUSED(ssid);
}
#endif

static const struct ui_ops ui_ops = {
	.set_paused = ui_pause,
	.next = ui_next,
	.previous = ui_previous,
	.set_repeat = ui_repeat,
	.seek = ui_seek,
	.set_volume = ui_volume,
	.position_ms = position_get,
	.library_open = library_open,
	.library_back = library_back,
	.library_select = library_select,
	.wifi_scan = wifi_scan,
	.wifi_connect = wifi_join,
	.wifi_forget = wifi_forget,
	.lyrics_request = lyrics_request,
	.search = library_search,
	.search_select = library_search_select,
};
#endif

/* The values of the option are those of enum zspot_audio_format */
BUILD_ASSERT(ZSPOT_FORMAT_OGG_VORBIS_96 == 0 && ZSPOT_FORMAT_OGG_VORBIS_160 == 1 &&
	     ZSPOT_FORMAT_OGG_VORBIS_320 == 2);

/* A start-up step failed for good: the player stays as it is. */
static void show_failure(const char *what, int error)
{
	char detail[UI_TEXT_MAX];

	snprintf(detail, sizeof(detail), "Error %d. Restart the device to try again", error);
	ui_show_message(what, detail);
}

int main(void)
{
	static char credentials[1024];
	bool credentials_stored = false;
	bool zeroconf_started = false;
	const struct zspot_config config = {
		.device_name = CONFIG_ZSPOT_DEVICE_NAME,
		.audio_format = CONFIG_ZSPOT_AUDIO_FORMAT,
		.initial_volume = 0xFFFF, /* unity gain at the sink */
	};
	int ret;

#if defined(CONFIG_ZSPOT_UI)
	if (ui_init(&ui_ops) == 0) {
		library_init();
		lyrics_init();
	}
#endif
	battery_init();
	ui_show_message("Connecting", "Waiting for the network");

	/* Without a log the screen is the only place to say what went wrong. */
	ret = network_connect();
	if (ret != 0) {
		show_failure("No network", ret);
		return 0;
	}

	pcm_tap_init();

	ret = certs_register();
	if (ret != 0) {
		show_failure("Cannot start the player", ret);
		return 0;
	}

	ret = zspot_init(&config);
	if (ret != 0) {
		LOG_ERR("zspot_init failed (%d)", ret);
		show_failure("Cannot start the player", ret);
		return 0;
	}

	ret = sink_init();
	if (ret != 0) {
		LOG_ERR("Audio sink init failed (%d)", ret);
		show_failure("No audio output", ret);
		return 0;
	}

	/* Credentials of the build are a development aid and come first. */
	if (strlen(CONFIG_ZSPOT_CREDENTIALS_JSON) > 0 &&
	    zspot_credentials_load_json(CONFIG_ZSPOT_CREDENTIALS_JSON) == 0) {
		LOG_INF("Using the credentials of the build");
	} else if (strlen(CONFIG_ZSPOT_USERNAME) > 0 &&
		   zspot_credentials_set_user_pass(CONFIG_ZSPOT_USERNAME,
						   CONFIG_ZSPOT_PASSWORD) == 0) {
		LOG_INF("Using username/password credentials");
	} else if (credentials_load() == 0) {
		LOG_INF("Using stored credentials");
		credentials_stored = true;
	}

	while (true) {
		if (!zspot_credentials_available()) {
			/* Not needed while there are credentials to sign in with */
			if (!zeroconf_started) {
				ret = zspot_zeroconf_start();
				if (ret != 0) {
					LOG_ERR("Zeroconf start failed (%d)", ret);
					show_failure("Cannot be found by Spotify", ret);
					return 0;
				}
				zeroconf_started = true;
			}

			LOG_INF("Open Spotify and select \"%s\" in the device list",
				zspot_device_name());
			ui_show_message(zspot_device_name(),
					"Open Spotify and pick this device");
			zspot_zeroconf_wait(-1);
		}

		ui_show_message("Connecting", "Signing in to Spotify");
		ret = zspot_connect(on_event, on_pcm, NULL);
		if (ret == 0) {
			break;
		}

		LOG_ERR("zspot_connect failed (%d)", ret);
		if (ret == -EACCES) {
			/* Rejected credentials: go back to waiting for a hand-over. */
			zspot_credentials_clear();
			if (credentials_stored) {
				(void)credentials_forget();
				credentials_stored = false;
			}
		} else {
			k_sleep(K_SECONDS(5));
		}
	}

	if (IS_ENABLED(CONFIG_ZSPOT_CREDENTIALS_STORED)) {
		(void)credentials_store();
	} else if (zspot_credentials_save_json(credentials, sizeof(credentials)) > 0) {
		LOG_INF("Reusable credentials (store these): %s", credentials);
	}

	/* The last song, ready to go on with, or how to get one */
	if (!resume_show(position_set)) {
		ui_show_message("Ready", "Pick this device in Spotify to start playing");
	}

	/* The protocol runs on the library threads; this one reports the end of playback. */
	end_report();
	return 0;
}
