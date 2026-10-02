/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef CSPOT_SAMPLE_WIFI_H_
#define CSPOT_SAMPLE_WIFI_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Joins the Wi-Fi network from the credentials stored with the
 *        wifi_credentials library, reconnecting whenever the link drops.
 */
int wifi_init(void);

#ifdef __cplusplus
}
#endif

#endif /* CSPOT_SAMPLE_WIFI_H_ */
