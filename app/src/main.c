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
#elif defined(ZSPOT_SAMPLE_HAVE_PCM_FILE)
#include "pcm_file_sink.h"
#endif
#include "pcm_tap.h"
#include "ui.h"
#include "battery.h"
#if defined(CONFIG_ZSPOT_UI)
#include "library.h"
#include "lyrics.h"
#endif

/* Decoded PCM: 44.1 kHz, 16 bit, stereo */
#define PCM_BYTES_PER_SECOND (44100 * 4)

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

static void position_set(uint32_t position_ms)
{
	position_base_ms = position_ms;
	atomic_clear(&position_bytes);
}

static uint32_t position_get(void)
{
	int64_t bytes = atomic_get(&position_bytes);

#if defined(CONFIG_ZSPOT_I2S_SINK)
	/* What the sink still holds is not audible yet. */
	bytes = MAX(bytes - (int64_t)zspot_i2s_sink_buffered(), 0);
#endif
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
	return zspot_i2s_sink_init(DEVICE_DT_GET(DT_ALIAS(zspot_i2s)), 44100, 2, 16);
#elif defined(ZSPOT_SAMPLE_HAVE_PCM_FILE)
	return pcm_file_sink_init(CONFIG_ZSPOT_PCM_COMMAND, CONFIG_ZSPOT_PCM_FILE);
#else
	return 0;
#endif
}

static size_t sink_write(const uint8_t *pcm, size_t len, void *user_data)
{
#if defined(CONFIG_ZSPOT_I2S_SINK)
	return zspot_i2s_sink_write(pcm, len, user_data);
#elif defined(ZSPOT_SAMPLE_HAVE_PCM_FILE)
	return pcm_file_sink_write(pcm, len, user_data);
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
		break;
	case ZSPOT_EVENT_SEEK:
		LOG_INF("Seek to %u ms", event->position_ms);
		sink_flush();
		position_set(event->position_ms);
		break;
	case ZSPOT_EVENT_FLUSH:
		sink_flush();
		break;
	case ZSPOT_EVENT_PLAYBACK_START:
		sink_flush();
		position_set(event->position_ms);
		position_preset = true;
		break;
	case ZSPOT_EVENT_TRACK_BEGIN:
		/* A gapless transition comes without PLAYBACK_START. */
		if (!position_preset) {
			position_set(0);
		}
		position_preset = false;
		zspot_notify_audio_reached_playback();
		break;
	case ZSPOT_EVENT_QUEUE_CHANGED:
		break;
	case ZSPOT_EVENT_DISCONNECT:
		LOG_INF("Playback moved to another device");
		sink_flush();
		ui_show_message("Playing on another device",
				"Pick this device in Spotify to listen here");
		break;
	case ZSPOT_EVENT_NEXT:
	case ZSPOT_EVENT_PREV:
		sink_flush();
		break;
	case ZSPOT_EVENT_DEPLETED:
		LOG_INF("Queue finished");
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
	atomic_add(&position_bytes, accepted);
	pcm_tap_write(pcm, accepted);
	return accepted;
}

#if defined(CONFIG_ZSPOT_UI)
static void ui_next(void)
{
	if (zspot_next()) {
		sink_flush();
	}
}

static void ui_previous(void)
{
	if (zspot_previous()) {
		sink_flush();
	}
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
#endif

static const struct ui_ops ui_ops = {
	.set_paused = zspot_set_pause,
	.next = ui_next,
	.previous = ui_previous,
	.seek = zspot_seek,
	.set_volume = ui_volume,
	.position_ms = position_get,
	.library_open = library_open,
	.library_back = library_back,
	.library_select = library_select,
	.wifi_scan = wifi_scan,
	.wifi_connect = wifi_join,
	.lyrics_request = lyrics_request,
	.search = library_search,
	.search_select = library_search_select,
};
#endif

int main(void)
{
	static char credentials[1024];
	const struct zspot_config config = {
		.device_name = CONFIG_ZSPOT_DEVICE_NAME,
		.audio_format = ZSPOT_FORMAT_OGG_VORBIS_160,
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

	if (network_connect() != 0) {
		return 0;
	}

	pcm_tap_init();

	ret = zspot_init(&config);
	if (ret != 0) {
		LOG_ERR("zspot_init failed (%d)", ret);
		return 0;
	}

	ret = sink_init();
	if (ret != 0) {
		LOG_ERR("Audio sink init failed (%d)", ret);
		return 0;
	}

	if (strlen(CONFIG_ZSPOT_CREDENTIALS_JSON) > 0 &&
	    zspot_credentials_load_json(CONFIG_ZSPOT_CREDENTIALS_JSON) == 0) {
		LOG_INF("Using stored credentials");
	} else if (strlen(CONFIG_ZSPOT_USERNAME) > 0 &&
		   zspot_credentials_set_user_pass(CONFIG_ZSPOT_USERNAME,
						   CONFIG_ZSPOT_PASSWORD) == 0) {
		LOG_INF("Using username/password credentials");
	} else {
		ret = zspot_zeroconf_start();
		if (ret != 0) {
			LOG_ERR("Zeroconf start failed (%d)", ret);
			return 0;
		}
	}

	while (true) {
		if (!zspot_credentials_available()) {
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
		} else {
			k_sleep(K_SECONDS(5));
		}
	}

	if (zspot_credentials_save_json(credentials, sizeof(credentials)) > 0) {
		LOG_INF("Reusable credentials (store these): %s", credentials);
	}

	ui_show_message("Ready", "Pick this device in Spotify to start playing");

	/* Everything else happens on the library threads. */
	return 0;
}
