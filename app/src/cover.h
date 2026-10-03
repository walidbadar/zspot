/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef ZSPOT_SAMPLE_COVER_H_
#define ZSPOT_SAMPLE_COVER_H_

#include <lvgl.h>

/**
 * Called with the LVGL lock held once a requested cover was downloaded and
 * decoded. Ownership of @p image passes to the callee, which releases it with
 * lv_draw_buf_destroy(). @p accent is the average colour of the image.
 */
typedef void (*cover_ready_cb_t)(lv_draw_buf_t *image, lv_color_t accent);

/**
 * Starts the cover loader thread. Call with the LVGL lock held.
 * @return 0 on success, -ENOMEM when the LVGL pool is exhausted.
 */
int cover_init(cover_ready_cb_t ready_cb);

/**
 * Requests the JPEG behind @p url. A request that is still queued or being
 * processed is superseded: only the most recent one reaches the callback.
 * NULL or an empty URL just cancels the outstanding request.
 */
void cover_request(const char *url);

#endif /* ZSPOT_SAMPLE_COVER_H_ */
