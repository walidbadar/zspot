/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* Stand-in for the LVGL lock, see lvgl.h next to it: there is no LVGL thread to keep out. */

#ifndef ZSPOT_TESTS_LVGL_ZEPHYR_H_
#define ZSPOT_TESTS_LVGL_ZEPHYR_H_

static inline void lvgl_lock(void)
{
}

static inline void lvgl_unlock(void)
{
}

#endif /* ZSPOT_TESTS_LVGL_ZEPHYR_H_ */
