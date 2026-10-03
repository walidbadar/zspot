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

/**
 * Sets up the host (native_sim) output for raw 16-bit stereo PCM at 44.1 kHz:
 * the standard input of the shell command @p command when that is not empty,
 * the file @p path otherwise.
 */
int pcm_file_sink_init(const char *command, const char *path);

/** zspot_pcm_cb_t compatible writer. */
size_t pcm_file_sink_write(const uint8_t *pcm, size_t len, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_SAMPLE_PCM_FILE_SINK_H_ */
