/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * PCM tap (CONFIG_ZSPOT_PCM_TAP): a TCP server that sends a copy of the audio
 * handed to the sink to whoever connects, to check on another machine what
 * the output is fed with:
 *
 *   nc <device> <port> | aplay -f S16_LE -r 44100 -c 2 -B 1000000 -R 1000000
 *
 * (the last two options make aplay collect a second of audio before it
 * starts, which rides out the jitter of the network)
 *
 * The copy is taken before the sink applies the volume.
 */

#ifndef ZSPOT_SAMPLE_PCM_TAP_H_
#define ZSPOT_SAMPLE_PCM_TAP_H_

#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_ZSPOT_PCM_TAP)

/** Starts the server. Call once the network is up. */
int pcm_tap_init(void);

/**
 * Queues PCM for the connected client. Never blocks: without a client, or
 * when the client cannot keep up, the data is dropped.
 */
void pcm_tap_write(const uint8_t *pcm, size_t len);

#else

static inline int pcm_tap_init(void)
{
	return 0;
}

static inline void pcm_tap_write(const uint8_t *pcm, size_t len)
{
}

#endif /* CONFIG_ZSPOT_PCM_TAP */

#endif /* ZSPOT_SAMPLE_PCM_TAP_H_ */
