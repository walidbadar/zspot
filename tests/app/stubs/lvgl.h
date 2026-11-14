/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Stand-in for LVGL in the tests that build a source file of the player by
 * itself: the files only take memory from its pool, which is the C heap here.
 */

#ifndef ZSPOT_TESTS_LVGL_H_
#define ZSPOT_TESTS_LVGL_H_

#include <stdlib.h>

static inline void *lv_malloc(size_t size)
{
	return malloc(size);
}

static inline void *lv_zalloc(size_t size)
{
	return calloc(1, size);
}

static inline void lv_free(void *ptr)
{
	free(ptr);
}

#endif /* ZSPOT_TESTS_LVGL_H_ */
