/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* Host side of the native_sim PCM player; only host types in this interface. */

#ifndef ZSPOT_PCM_COMMAND_BOTTOM_H_
#define ZSPOT_PCM_COMMAND_BOTTOM_H_

/**
 * Starts the shell command @p command on the host and installs its standard
 * input at the descriptor number that @p path, the output file of the
 * native_sim I2S driver, names in the form "/dev/fd/<n>".
 *
 * Returns 0 when the command runs, -ENOENT when its program is not installed,
 * -EINVAL when @p path has another form, -EBUSY when the descriptor is in
 * use, or another negative errno. Except for -EINVAL, the driver finds
 * /dev/null at that descriptor after a failure, so that the audio is
 * discarded instead of stalling the simulator.
 */
int pcm_command_start(const char *command, const char *path);

#endif /* ZSPOT_PCM_COMMAND_BOTTOM_H_ */
