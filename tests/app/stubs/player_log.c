/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * The sources of the player log under the name that main.c registers. A test
 * that includes one of them cannot register the name in the same file: it is
 * done here.
 */

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zspot_player, LOG_LEVEL_INF);
