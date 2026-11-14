/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "audio/gain.h"

/* From silence to full volume in 20 ms at 44.1 kHz, as the sink does */
#define STEP   (ZSPOT_GAIN_UNITY / 882)
#define FRAMES 1024

static int16_t stereo[2 * FRAMES];

static void fill(int16_t left, int16_t right)
{
	for (size_t i = 0; i < FRAMES; i++) {
		stereo[2 * i] = left;
		stereo[2 * i + 1] = right;
	}
}

ZTEST(zspot_gain, test_volume_ends)
{
	zassert_equal(zspot_gain_from_volume(0), 0);
	zassert_equal(zspot_gain_from_volume(65535), ZSPOT_GAIN_UNITY);
}

/* Half the volume is an eighth of the amplitude, 18 dB down. */
ZTEST(zspot_gain, test_volume_is_cubed)
{
	zassert_within(zspot_gain_from_volume(32768), ZSPOT_GAIN_UNITY / 8, 2);
	zassert_within(zspot_gain_from_volume(16384), ZSPOT_GAIN_UNITY / 64, 2);
	zassert_within(zspot_gain_from_volume(49152), ZSPOT_GAIN_UNITY * 27 / 64, 2);
}

ZTEST(zspot_gain, test_volume_never_falls)
{
	uint16_t last = 0;

	for (uint32_t volume = 0; volume <= 65535; volume++) {
		uint16_t gain = zspot_gain_from_volume(volume);

		zassert_true(gain >= last, "falls at %u", volume);
		zassert_true(gain <= ZSPOT_GAIN_UNITY);
		last = gain;
	}
}

/* At full volume the audio is not touched at all. */
ZTEST(zspot_gain, test_unity_keeps_the_samples)
{
	static int16_t copy[2 * FRAMES];

	for (size_t i = 0; i < ARRAY_SIZE(stereo); i++) {
		stereo[i] = (int16_t)(i * 517 - 32768);
	}
	stereo[0] = INT16_MIN;
	stereo[1] = INT16_MAX;
	memcpy(copy, stereo, sizeof(copy));

	zassert_equal(zspot_gain_apply(ZSPOT_GAIN_UNITY, ZSPOT_GAIN_UNITY, STEP, stereo, FRAMES, 2),
		      ZSPOT_GAIN_UNITY);
	zassert_mem_equal(stereo, copy, sizeof(copy));
}

ZTEST(zspot_gain, test_steady_gain)
{
	fill(20000, -20000);

	zassert_equal(zspot_gain_apply(ZSPOT_GAIN_UNITY / 2, ZSPOT_GAIN_UNITY / 2, STEP, stereo,
				       FRAMES, 2),
		      ZSPOT_GAIN_UNITY / 2);
	for (size_t i = 0; i < FRAMES; i++) {
		zassert_equal(stereo[2 * i], 10000);
		zassert_equal(stereo[2 * i + 1], -10000);
	}

	fill(INT16_MAX, INT16_MIN);
	zspot_gain_apply(0, 0, STEP, stereo, FRAMES, 2);
	for (size_t i = 0; i < ARRAY_SIZE(stereo); i++) {
		zassert_equal(stereo[i], 0);
	}
}

/* A fade from silence rises frame by frame, both channels alike, and arrives. */
ZTEST(zspot_gain, test_fade_in)
{
	uint16_t gain;

	fill(30000, -30000);
	gain = zspot_gain_apply(0, ZSPOT_GAIN_UNITY, STEP, stereo, FRAMES, 2);
	zassert_equal(gain, ZSPOT_GAIN_UNITY);

	zassert_true(abs(stereo[0]) <= 30000 * STEP / ZSPOT_GAIN_UNITY + 1);
	for (size_t i = 1; i < FRAMES; i++) {
		int step = stereo[2 * i] - stereo[2 * (i - 1)];

		zassert_true(step >= 0 && step <= 30000 * STEP / ZSPOT_GAIN_UNITY + 1,
			     "step of %d at frame %u", step, (unsigned int)i);
		zassert_within(stereo[2 * i + 1], -stereo[2 * i], 1);
	}
	/* Full level once the 20 ms are over */
	zassert_equal(stereo[2 * 900], 30000);
	zassert_equal(stereo[2 * (FRAMES - 1) + 1], -30000);
}

/* A fade out ends in silence and stays there for the rest of the block. */
ZTEST(zspot_gain, test_fade_out)
{
	uint16_t gain;

	fill(30000, 30000);
	gain = zspot_gain_apply(ZSPOT_GAIN_UNITY, 0, STEP, stereo, FRAMES, 2);
	zassert_equal(gain, 0);

	zassert_true(stereo[0] < 30000 && stereo[0] > 29900);
	for (size_t i = 1; i < FRAMES; i++) {
		zassert_true(stereo[2 * i] <= stereo[2 * (i - 1)]);
	}
	for (size_t i = 900; i < FRAMES; i++) {
		zassert_equal(stereo[2 * i], 0);
		zassert_equal(stereo[2 * i + 1], 0);
	}
}

/* A fade that does not fit goes on in the next block from where it was. */
ZTEST(zspot_gain, test_fade_across_blocks)
{
	uint16_t gain = 0;
	int16_t last;

	fill(30000, 30000);
	gain = zspot_gain_apply(gain, ZSPOT_GAIN_UNITY, STEP, stereo, 400, 2);
	zassert_equal(gain, 400 * STEP);
	last = stereo[2 * 399];

	fill(30000, 30000);
	gain = zspot_gain_apply(gain, ZSPOT_GAIN_UNITY, STEP, stereo, FRAMES, 2);
	zassert_equal(gain, ZSPOT_GAIN_UNITY);
	zassert_true(stereo[0] >= last && stereo[0] - last <= 30000 * STEP / ZSPOT_GAIN_UNITY + 1);
}

/* A lower volume is approached from above, a target in reach is hit exactly. */
ZTEST(zspot_gain, test_towards_a_target)
{
	fill(1000, 1000);
	zassert_equal(zspot_gain_apply(ZSPOT_GAIN_UNITY, 20000, STEP, stereo, FRAMES, 2), 20000);
	zassert_equal(zspot_gain_apply(20000, 20010, STEP, stereo, 1, 2), 20010);
	zassert_equal(zspot_gain_apply(20000, 19990, STEP, stereo, 1, 2), 19990);
	zassert_equal(zspot_gain_apply(20000, 30000, STEP, stereo, 0, 2), 20000);
}

ZTEST(zspot_gain, test_mono)
{
	static int16_t mono[4] = {8000, 8000, 8000, 8000};

	zassert_equal(zspot_gain_apply(ZSPOT_GAIN_UNITY / 4, ZSPOT_GAIN_UNITY / 4, STEP, mono, 4, 1),
		      ZSPOT_GAIN_UNITY / 4);
	for (size_t i = 0; i < ARRAY_SIZE(mono); i++) {
		zassert_equal(mono[i], 2000);
	}
}

ZTEST_SUITE(zspot_gain, NULL, NULL, NULL, NULL, NULL);
