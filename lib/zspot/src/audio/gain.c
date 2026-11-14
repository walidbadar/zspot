/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "audio/gain.h"

uint16_t zspot_gain_from_volume(uint16_t volume)
{
	const uint64_t full = 65535U;
	uint64_t cube = (uint64_t)volume * volume * volume;

	/* Rounded; the largest product is just below 2^63 */
	return (uint16_t)((cube * ZSPOT_GAIN_UNITY + full * full * full / 2) / (full * full * full));
}

uint16_t zspot_gain_apply(uint16_t current, uint16_t target, uint16_t step, int16_t *samples,
			  size_t frames, unsigned int channels)
{
	if (current == target && current == ZSPOT_GAIN_UNITY) {
		return current;
	}

	for (size_t frame = 0; frame < frames; frame++) {
		if (current < target) {
			current = (target - current > step) ? current + step : target;
		} else if (current > target) {
			current = (current - target > step) ? current - step : target;
		}

		for (unsigned int channel = 0; channel < channels; channel++) {
			*samples = (int16_t)(((int32_t)*samples * current) >> 15);
			samples++;
		}
	}
	return current;
}
