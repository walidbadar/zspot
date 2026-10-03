/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef ZSPOT_SAMPLE_PCM_FILE_SINK_H_
#define ZSPOT_SAMPLE_PCM_FILE_SINK_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opens @p path on the host (native_sim) for raw 16-bit stereo PCM output. */
int pcm_file_sink_init(const char *path);

/** zspot_pcm_cb_t compatible writer. */
size_t pcm_file_sink_write(const uint8_t *pcm, size_t len, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_SAMPLE_PCM_FILE_SINK_H_ */
