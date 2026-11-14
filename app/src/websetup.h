/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Setup of the Web API over the network (CONFIG_ZSPOT_WEB_SETUP): an HTTP
 * endpoint that takes the client ID, the client secret and the refresh token
 * from scripts/spotify_authorize.py, so that they need not be part of the
 * firmware:
 *
 *   POST /webapi   client_id=...&client_secret=...&refresh_token=...
 *   GET  /webapi   says whether credentials are set
 *
 * Nothing is ever given out. Credentials are taken while the device has
 * none, or for a few minutes after the Wi-Fi settings were opened on its
 * screen: to replace them takes somebody at the device. They cross the
 * local network unencrypted.
 */

#ifndef ZSPOT_SAMPLE_WEBSETUP_H_
#define ZSPOT_SAMPLE_WEBSETUP_H_

#if defined(CONFIG_ZSPOT_WEB_SETUP)

/** Starts the endpoint. Call once the network is up and after ui_init(). */
int websetup_init(void);

/** The Wi-Fi settings were opened: take new credentials for a while. */
void websetup_allow(void);

#else

static inline int websetup_init(void)
{
	return 0;
}

static inline void websetup_allow(void)
{
}

#endif /* CONFIG_ZSPOT_WEB_SETUP */

#endif /* ZSPOT_SAMPLE_WEBSETUP_H_ */
