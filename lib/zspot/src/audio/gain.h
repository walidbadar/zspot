/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Software volume for 16-bit PCM: the gain that goes with a volume, and its
 * application without steps in the signal.
 */

#ifndef ZSPOT_AUDIO_GAIN_H_
#define ZSPOT_AUDIO_GAIN_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Gain that leaves the samples as they are; gains are fractions of it */
#define ZSPOT_GAIN_UNITY 32768U

/**
 * Gain for a volume of 0..65535: silence at 0, ZSPOT_GAIN_UNITY at 65535 and
 * the cube of the volume in between. Loudness is heard on a logarithmic
 * scale, which a gain in proportion to the volume would squeeze into the
 * bottom of the range.
 */
uint16_t zspot_gain_from_volume(uint16_t volume);

/**
 * Applies a gain to interleaved frames, moving it from @p current towards
 * @p target by @p step per frame, so that a change is not heard as a click.
 *
 * @return the gain of the last frame, to go on with
 */
uint16_t zspot_gain_apply(uint16_t current, uint16_t target, uint16_t step, int16_t *samples,
			  size_t frames, unsigned int channels);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_AUDIO_GAIN_H_ */
