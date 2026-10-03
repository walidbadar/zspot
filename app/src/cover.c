/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Cover art loader: downloads the JPEG on its own thread (the transfer takes
 * a TLS handshake) and decodes it into an RGB565 image with the TJpgDec copy
 * that ships with LVGL. Spotify serves baseline JPEGs, 300x300 for the
 * default size, which is what TJpgDec supports.
 */

#include "cover.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl_zephyr.h>
#include <libs/tjpgd/tjpgd.h>

#include <zspot/zspot.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

#define COVER_URL_MAX    128
#define COVER_JPEG_MAX   (96 * 1024)
/* Larger images are decimated while decoding to stay below this edge length */
#define COVER_EDGE_MAX   320
#define COVER_STACK_SIZE 8192
#define COVER_PRIORITY   10
/* TJpgDec needs about 3.5 KB for a colour image */
#define JPEG_POOL_SIZE   4096

struct jpeg_session {
	const uint8_t *data;
	size_t len;
	size_t pos;

	lv_draw_buf_t *image;
	uint32_t step;
	uint32_t sum[3];
};

struct cover_request {
	atomic_val_t generation;
	char url[COVER_URL_MAX];
};

K_MSGQ_DEFINE(cover_requests, sizeof(struct cover_request), 1, 4);

/* Bumped by every request; a result of an older generation is stale. */
static atomic_t cover_generation;
static cover_ready_cb_t cover_ready_cb;

/*
 * The thread stack and the decoder's work area come from the LVGL pool, which
 * boards with external RAM place there.
 */
static struct k_thread cover_thread_data;
static uint8_t *jpeg_pool;

static size_t jpeg_input(JDEC *jd, uint8_t *buf, size_t len)
{
	struct jpeg_session *session = jd->device;

	len = MIN(len, session->len - session->pos);
	if (buf != NULL) {
		memcpy(buf, &session->data[session->pos], len);
	}
	session->pos += len;
	return len;
}

static int jpeg_output(JDEC *jd, void *bitmap, JRECT *rect)
{
	struct jpeg_session *session = jd->device;
	const lv_image_header_t *header = &session->image->header;
	const uint8_t *bgr = bitmap;

	for (uint32_t y = rect->top; y <= rect->bottom; y++) {
		for (uint32_t x = rect->left; x <= rect->right; x++, bgr += 3) {
			uint32_t dx = x / session->step;
			uint32_t dy = y / session->step;
			uint16_t *row;

			if (x % session->step != 0 || y % session->step != 0 ||
			    dx >= header->w || dy >= header->h) {
				continue;
			}

			row = (uint16_t *)(session->image->data + dy * header->stride);
			row[dx] = ((bgr[2] & 0xF8) << 8) | ((bgr[1] & 0xFC) << 3) | (bgr[0] >> 3);

			session->sum[0] += bgr[2];
			session->sum[1] += bgr[1];
			session->sum[2] += bgr[0];
		}
	}
	return 1;
}

static lv_draw_buf_t *cover_decode(const uint8_t *jpeg, size_t len, lv_color_t *accent)
{
	struct jpeg_session session = {.data = jpeg, .len = len, .step = 1};
	uint32_t pixels;
	JRESULT res;
	JDEC jd;

	res = jd_prepare(&jd, jpeg_input, jpeg_pool, JPEG_POOL_SIZE, &session);
	if (res != JDR_OK || jd.width == 0 || jd.height == 0) {
		LOG_WRN("Cover is not a baseline JPEG (%d)", res);
		return NULL;
	}

	while (MAX(jd.width, jd.height) / session.step > COVER_EDGE_MAX) {
		session.step++;
	}

	lvgl_lock();
	session.image = lv_draw_buf_create(MAX(jd.width / session.step, 1),
					   MAX(jd.height / session.step, 1),
					   LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
	lvgl_unlock();
	if (session.image == NULL) {
		LOG_WRN("No memory for a %ux%u cover", jd.width, jd.height);
		return NULL;
	}

	res = jd_decomp(&jd, jpeg_output, 0);
	if (res != JDR_OK) {
		LOG_WRN("Cover decoding failed (%d)", res);
		lvgl_lock();
		lv_draw_buf_destroy(session.image);
		lvgl_unlock();
		return NULL;
	}

	pixels = session.image->header.w * session.image->header.h;
	*accent = lv_color_make(session.sum[0] / pixels, session.sum[1] / pixels,
				session.sum[2] / pixels);
	return session.image;
}

static void cover_thread(void *p1, void *p2, void *p3)
{
	static struct cover_request request;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		lv_draw_buf_t *image = NULL;
		lv_color_t accent;
		uint8_t *jpeg;
		int len;

		/* cover_request() purging the queue ends the wait without a message. */
		if (k_msgq_get(&cover_requests, &request, K_FOREVER) != 0) {
			continue;
		}

		lvgl_lock();
		jpeg = lv_malloc(COVER_JPEG_MAX);
		lvgl_unlock();
		if (jpeg == NULL) {
			LOG_WRN("No memory for the cover download");
			continue;
		}

		len = zspot_http_get(request.url, jpeg, COVER_JPEG_MAX);
		if (len > 0) {
			image = cover_decode(jpeg, len, &accent);
		} else {
			LOG_WRN("Cover download failed (%d)", len);
		}

		lvgl_lock();
		lv_free(jpeg);
		if (image != NULL) {
			/* The track may have changed during the download. */
			if (request.generation == atomic_get(&cover_generation)) {
				cover_ready_cb(image, accent);
			} else {
				lv_draw_buf_destroy(image);
			}
		}
		lvgl_unlock();
	}
}

int cover_init(cover_ready_cb_t ready_cb)
{
	uint8_t *stack = lv_malloc(K_KERNEL_STACK_LEN(COVER_STACK_SIZE) + Z_KERNEL_STACK_OBJ_ALIGN);

	jpeg_pool = lv_malloc(JPEG_POOL_SIZE);
	if (stack == NULL || jpeg_pool == NULL) {
		return -ENOMEM;
	}

	cover_ready_cb = ready_cb;
	k_thread_create(&cover_thread_data,
			(k_thread_stack_t *)ROUND_UP(stack, Z_KERNEL_STACK_OBJ_ALIGN),
			COVER_STACK_SIZE, cover_thread, NULL, NULL, NULL, COVER_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&cover_thread_data, "cover");
	return 0;
}

void cover_request(const char *url)
{
	struct cover_request request = {.generation = atomic_inc(&cover_generation) + 1};

	k_msgq_purge(&cover_requests);
	if (url == NULL || url[0] == '\0') {
		return;
	}
	if (strlen(url) >= sizeof(request.url)) {
		LOG_WRN("Cover URL too long");
		return;
	}

	strcpy(request.url, url);
	k_msgq_put(&cover_requests, &request, K_NO_WAIT);
}
