/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* Host side of the native_sim PCM sink; only host types in this interface. */

#ifndef ZSPOT_SAMPLE_PCM_FILE_SINK_BOTTOM_H_
#define ZSPOT_SAMPLE_PCM_FILE_SINK_BOTTOM_H_

/** Creates or truncates @p path. Returns a file descriptor or -1. */
int pcm_host_open_file(const char *path);

/**
 * Starts the shell command @p command and returns a non-blocking descriptor
 * for its standard input, or -1.
 */
int pcm_host_open_command(const char *command);

/**
 * Returns the number of bytes written, 0 when the receiver is not ready for
 * more, or -1 when it is gone.
 */
long pcm_host_write(int fd, const void *data, unsigned long len);

#endif /* ZSPOT_SAMPLE_PCM_FILE_SINK_BOTTOM_H_ */
