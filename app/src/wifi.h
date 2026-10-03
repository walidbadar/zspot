/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef ZSPOT_SAMPLE_WIFI_H_
#define ZSPOT_SAMPLE_WIFI_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Joins the Wi-Fi network from the credentials stored with the
 *        wifi_credentials library, reconnecting whenever the link drops.
 */
int wifi_init(void);

/**
 * @brief Scans for networks and lists them in the UI (UI_LIST_WIFI).
 */
void wifi_scan(void);

/**
 * @brief Stores the credentials of a network with the wifi_credentials
 *        library and connects to it.
 *
 * @param password  empty for an open network
 */
void wifi_join(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_SAMPLE_WIFI_H_ */
