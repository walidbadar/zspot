/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "wifi.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

/* Delay before retrying after a disconnect, a failed attempt or missing credentials */
#define WIFI_RECONNECT_DELAY K_SECONDS(5)

static struct k_work_delayable reconnect_work;
static struct net_mgmt_event_callback wifi_mgmt_cb;

/* Networks of the scan shown in the UI; an access point may answer per band. */
struct scan_entry {
	char ssid[WIFI_SSID_MAX_LEN + 1];
	enum wifi_security_type security;
};

static struct scan_entry scan_entries[UI_LIST_MAX];
static int scan_count;
static uint32_t scan_generation;

static int wifi_connect(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	int ret;

	if (iface == NULL) {
		LOG_ERR("No Wi-Fi STA interface");
		return -ENODEV;
	}

	if (wifi_credentials_is_empty()) {
		LOG_WRN("No Wi-Fi credentials stored. Hold the Wi-Fi symbol on the screen for "
			"three seconds to add them");
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

static void on_scan_result(const struct wifi_scan_result *result)
{
	struct scan_entry *entry = &scan_entries[scan_count];
	char subtitle[48];

	/* Hidden networks cannot be picked by name. */
	if (result->ssid_length == 0 || scan_count == ARRAY_SIZE(scan_entries)) {
		return;
	}
	for (int i = 0; i < scan_count; i++) {
		if (strlen(scan_entries[i].ssid) == result->ssid_length &&
		    memcmp(scan_entries[i].ssid, result->ssid, result->ssid_length) == 0) {
			return;
		}
	}

	memcpy(entry->ssid, result->ssid, result->ssid_length);
	entry->ssid[result->ssid_length] = '\0';
	entry->security = result->security;

	/* The first result replaces the "Scanning..." notice. */
	if (scan_count++ == 0) {
		ui_list_reset(UI_LIST_WIFI, ++scan_generation, "Wi-Fi", true, NULL);
	}
	snprintf(subtitle, sizeof(subtitle), "%s \xE2\x80\xA2 %d dBm",
		 result->security == WIFI_SECURITY_TYPE_NONE ? "Open" : "Secured", result->rssi);
	ui_list_add(UI_LIST_WIFI, scan_generation, entry->ssid, subtitle, 0);
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		on_scan_result((const struct wifi_scan_result *)cb->info);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		if (scan_count == 0) {
			ui_list_reset(UI_LIST_WIFI, ++scan_generation, "Wi-Fi", true,
				      "No networks found");
		}
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (status->status != 0) {
			LOG_WRN("Wi-Fi connect failed (%d), retrying", status->status);
			(void)k_work_reschedule(&reconnect_work, WIFI_RECONNECT_DELAY);
		} else {
			/*
			 * DHCP is handled by the Wi-Fi driver (it starts the client
			 * after association and reports the result again once an
			 * address is bound), so nothing else to do here.
			 */
			LOG_INF("Wi-Fi connected");
			ui_set_network(true);
			(void)k_work_cancel_delayable(&reconnect_work);
		}
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		LOG_WRN("Wi-Fi disconnected (reason %d), reconnecting", status->disconn_reason);
		ui_set_network(false);
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
				     NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |
					     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	/* First attempt runs from the work queue, like every retry. */
	ret = k_work_schedule(&reconnect_work, K_NO_WAIT);

	return (ret < 0) ? ret : 0;
}

void wifi_scan(void)
{
	struct net_if *iface = net_if_get_wifi_sta();

	scan_count = 0;
	if (iface == NULL || net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0) != 0) {
		/* E.g. a connection attempt is in progress. */
		ui_list_reset(UI_LIST_WIFI, ++scan_generation, "Wi-Fi", true,
			      "Scan failed, try again");
		return;
	}
	ui_list_reset(UI_LIST_WIFI, ++scan_generation, "Wi-Fi", true, "Scanning...");
}

void wifi_join(const char *ssid, const char *password)
{
	struct net_if *iface = net_if_get_wifi_sta();
	enum wifi_security_type security =
		password[0] == '\0' ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	int ret;

	/* WPA3-only networks need the matching type; the scan knows it. */
	for (int i = 0; i < scan_count; i++) {
		if (strcmp(scan_entries[i].ssid, ssid) == 0 && password[0] != '\0' &&
		    (scan_entries[i].security == WIFI_SECURITY_TYPE_SAE ||
		     scan_entries[i].security == WIFI_SECURITY_TYPE_SAE_HNP ||
		     scan_entries[i].security == WIFI_SECURITY_TYPE_SAE_H2E ||
		     scan_entries[i].security == WIFI_SECURITY_TYPE_SAE_AUTO)) {
			security = WIFI_SECURITY_TYPE_SAE;
		}
	}

	ret = wifi_credentials_set_personal(ssid, strlen(ssid), security, NULL, 0, password,
					    strlen(password), 0, 0, 0);
	if (ret != 0) {
		LOG_ERR("Cannot store the credentials of \"%s\" (%d)", ssid, ret);
		return;
	}
	LOG_INF("Stored the credentials of \"%s\"", ssid);

	/* Leave the current network, if any; then connect with the stored ones. */
	if (iface != NULL) {
		(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
	}
	(void)k_work_reschedule(&reconnect_work, K_SECONDS(1));
}
