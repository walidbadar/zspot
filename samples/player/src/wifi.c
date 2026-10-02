/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "wifi.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_DECLARE(cspot_player, LOG_LEVEL_INF);

/* Delay before retrying after a disconnect, a failed attempt or missing credentials */
#define WIFI_RECONNECT_DELAY K_SECONDS(5)

static struct k_work_delayable reconnect_work;
static struct net_mgmt_event_callback wifi_mgmt_cb;

static int wifi_connect(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	int ret;

	if (iface == NULL) {
		LOG_ERR("No Wi-Fi STA interface");
		return -ENODEV;
	}

	if (wifi_credentials_is_empty()) {
		LOG_WRN("No Wi-Fi credentials stored. Add them with: "
			"wifi cred add -s <ssid> -k 1 -p <passphrase>");
		return -ENOENT;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
	if (ret != 0) {
		LOG_ERR("Connect stored failed: %d", ret);
		return ret;
	}

	LOG_INF("Connecting to WLAN");
	return 0;
}

static void reconnect_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (wifi_connect() != 0) {
		(void)k_work_reschedule(&reconnect_work, WIFI_RECONNECT_DELAY);
	}
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (status->status != 0) {
			LOG_WRN("Wi-Fi connect failed (%d), retrying", status->status);
			(void)k_work_reschedule(&reconnect_work, WIFI_RECONNECT_DELAY);
		} else {
			LOG_INF("Wi-Fi connected");
			(void)k_work_cancel_delayable(&reconnect_work);
		}
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		LOG_WRN("Wi-Fi disconnected (reason %d), reconnecting", status->disconn_reason);
		(void)k_work_reschedule(&reconnect_work, WIFI_RECONNECT_DELAY);
		break;
	default:
		break;
	}
}

int wifi_init(void)
{
	int ret;

	k_work_init_delayable(&reconnect_work, reconnect_handler);

	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	/* First attempt runs from the work queue, like every retry. */
	ret = k_work_schedule(&reconnect_work, K_NO_WAIT);

	return (ret < 0) ? ret : 0;
}
