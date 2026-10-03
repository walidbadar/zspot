/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * cspot player sample: Spotify Connect receiver with I2S output.
 *
 * Flow: bring the network up, advertise the device, wait for the Spotify app
 * to hand over credentials (or use stored ones), connect, and route decoded
 * PCM to the I2S sink.
 */
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>

#include <cspot/cspot.h>

#if defined(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED)
#include "wifi.h"
#endif
#if defined(CONFIG_CSPOT_I2S_SINK)
#include <cspot/cspot_i2s_sink.h>
#elif defined(CSPOT_SAMPLE_HAVE_PCM_FILE)
#include "pcm_file_sink.h"
#endif

LOG_MODULE_REGISTER(cspot_player, LOG_LEVEL_INF);

static K_SEM_DEFINE(ipv4_ready, 0, 1);
static struct net_mgmt_event_callback net_cb;

static volatile bool paused;

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
	/* Credentials come from the wifi_credentials library (settings or static). */
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
	return 0;
}

static void sink_flush(void)
{
#if defined(CONFIG_CSPOT_I2S_SINK)
	sink_flush();
#endif
}

static void sink_set_volume(uint16_t volume)
{
#if defined(CONFIG_CSPOT_I2S_SINK)
	cspot_i2s_sink_set_volume(volume);
#else
	ARG_UNUSED(volume);
#endif
}

static int sink_init(void)
{
#if defined(CONFIG_CSPOT_I2S_SINK)
	return cspot_i2s_sink_init(DEVICE_DT_GET(DT_ALIAS(cspot_i2s)), 44100, 2, 16);
#elif defined(CSPOT_SAMPLE_HAVE_PCM_FILE)
	return pcm_file_sink_init(CONFIG_CSPOT_SAMPLE_PCM_FILE);
#else
	return 0;
#endif
}

static size_t sink_write(const uint8_t *pcm, size_t len, void *user_data)
{
#if defined(CONFIG_CSPOT_I2S_SINK)
	return cspot_i2s_sink_write(pcm, len, user_data);
#elif defined(CSPOT_SAMPLE_HAVE_PCM_FILE)
	return pcm_file_sink_write(pcm, len, user_data);
#else
	ARG_UNUSED(pcm);
	ARG_UNUSED(user_data);
	return len;
#endif
}

static void on_event(const struct cspot_event *event, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (event->type) {
	case CSPOT_EVENT_PLAY_PAUSE:
		LOG_INF("%s", event->paused ? "Paused" : "Playing");
		paused = event->paused;
		break;
	case CSPOT_EVENT_VOLUME:
		LOG_INF("Volume %u", event->volume);
		sink_set_volume(event->volume);
		break;
	case CSPOT_EVENT_TRACK_INFO:
		LOG_INF("Now playing: %s - %s (%s)", event->track.artist, event->track.name,
			event->track.album);
		break;
	case CSPOT_EVENT_SEEK:
		LOG_INF("Seek to %u ms", event->position_ms);
		sink_flush();
		break;
	case CSPOT_EVENT_FLUSH:
	case CSPOT_EVENT_PLAYBACK_START:
		sink_flush();
		break;
	case CSPOT_EVENT_DISCONNECT:
		LOG_INF("Playback moved to another device");
		sink_flush();
		break;
	case CSPOT_EVENT_NEXT:
	case CSPOT_EVENT_PREV:
		sink_flush();
		break;
	case CSPOT_EVENT_DEPLETED:
		LOG_INF("Queue finished");
		break;
	}
}

static size_t on_pcm(const uint8_t *pcm, size_t len, void *user_data)
{
	if (paused) {
		return 0; /* player retries shortly */
	}
	return sink_write(pcm, len, user_data);
}

int main(void)
{
	static char credentials[1024];
	const struct cspot_config config = {
		.device_name = CONFIG_CSPOT_DEVICE_NAME,
		.audio_format = CSPOT_FORMAT_OGG_VORBIS_160,
	};
	int ret;

	if (network_connect() != 0) {
		return 0;
	}

	ret = cspot_init(&config);
	if (ret != 0) {
		LOG_ERR("cspot_init failed (%d)", ret);
		return 0;
	}

	ret = sink_init();
	if (ret != 0) {
		LOG_ERR("Audio sink init failed (%d)", ret);
		return 0;
	}

	if (strlen(CONFIG_CSPOT_SAMPLE_CREDENTIALS_JSON) > 0 &&
	    cspot_credentials_load_json(CONFIG_CSPOT_SAMPLE_CREDENTIALS_JSON) == 0) {
		LOG_INF("Using stored credentials");
	} else if (strlen(CONFIG_CSPOT_SAMPLE_USERNAME) > 0 &&
		   cspot_credentials_set_user_pass(CONFIG_CSPOT_SAMPLE_USERNAME,
						   CONFIG_CSPOT_SAMPLE_PASSWORD) == 0) {
		LOG_INF("Using username/password credentials");
	} else {
		ret = cspot_zeroconf_start();
		if (ret != 0) {
			LOG_ERR("Zeroconf start failed (%d)", ret);
			return 0;
		}
	}

	while (true) {
		if (!cspot_credentials_available()) {
			LOG_INF("Open Spotify and select \"%s\" in the device list",
				cspot_device_name());
			cspot_zeroconf_wait(-1);
		}

		ret = cspot_connect(on_event, on_pcm, NULL);
		if (ret == 0) {
			break;
		}

		LOG_ERR("cspot_connect failed (%d)", ret);
		if (ret == -EACCES) {
			/* Rejected credentials: go back to waiting for a hand-over. */
			cspot_credentials_clear();
		} else {
			k_sleep(K_SECONDS(5));
		}
	}

	if (cspot_credentials_save_json(credentials, sizeof(credentials)) > 0) {
		LOG_INF("Reusable credentials (store these): %s", credentials);
	}

	/* Everything else happens on the library threads. */
	return 0;
}
