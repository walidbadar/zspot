/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "pcm_tap.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

LOG_MODULE_DECLARE(zspot_player, LOG_LEVEL_INF);

/*
 * Audio queued for the client: about 1.5 s of 44.1 kHz stereo, to ride out
 * the moments in which the Wi-Fi link stalls
 */
#define BUFFER_SIZE   (256 * 1024)
/* Sent per call: large enough to fill whole TCP segments */
#define CHUNK_SIZE    4096
#define STACK_SIZE    4096
#define PRIORITY      8
#define SEND_TIMEOUT_S 2

/*
 * The buffer is too large for the internal RAM of the ESP32: there it goes
 * into the section that the SoC linker script places in PSRAM.
 */
#if defined(CONFIG_ESP_SPIRAM)
#define BUFFER_SECTION Z_GENERIC_SECTION(.ext_ram.bss.pcm_tap)
#else
#define BUFFER_SECTION
#endif

static uint8_t buffer_memory[BUFFER_SIZE] BUFFER_SECTION;
static uint8_t chunk[CHUNK_SIZE] BUFFER_SECTION;
static struct ring_buf buffer;
static K_SEM_DEFINE(data_ready, 0, 1);
static atomic_t client_connected;
static atomic_t dropped_bytes;

/* Sends everything or fails; a client that stalls is dropped by the timeout. */
static int send_all(int sock, const uint8_t *data, size_t len)
{
	while (len > 0) {
		ssize_t sent = zsock_send(sock, data, len, 0);

		if (sent <= 0) {
			return -EIO;
		}
		data += sent;
		len -= sent;
	}
	return 0;
}

static void serve(int client)
{
	const struct timeval timeout = {.tv_sec = SEND_TIMEOUT_S};
	uint32_t len;

	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	ring_buf_reset(&buffer);
	atomic_clear(&dropped_bytes);
	atomic_set(&client_connected, 1);
	LOG_INF("PCM tap: client connected");

	while (true) {
		atomic_val_t dropped;

		/* Also wakes up without data, so that a pause does not hide a lost client. */
		k_sem_take(&data_ready, K_SECONDS(1));

		while ((len = ring_buf_get(&buffer, chunk, sizeof(chunk))) > 0) {
			if (send_all(client, chunk, len) != 0) {
				goto disconnected;
			}
		}

		dropped = atomic_clear(&dropped_bytes);
		if (dropped > 0) {
			LOG_WRN("PCM tap: client too slow, %ld bytes dropped", (long)dropped);
		}

		/* A closed connection shows as end of stream (0) or an error here. */
		if (zsock_recv(client, chunk, sizeof(chunk), ZSOCK_MSG_DONTWAIT) == 0) {
			break;
		}
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			break;
		}
	}

disconnected:
	atomic_clear(&client_connected);
	LOG_INF("PCM tap: client disconnected");
}

static void tap_thread(void *p1, void *p2, void *p3)
{
	const struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(CONFIG_ZSPOT_PCM_TAP_PORT),
	};
	const int reuse = 1;
	int server;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	server = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	zsock_setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (server < 0 ||
	    zsock_bind(server, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
	    zsock_listen(server, 1) != 0) {
		LOG_ERR("PCM tap: cannot listen on port %d (errno %d)", CONFIG_ZSPOT_PCM_TAP_PORT,
			errno);
		return;
	}
	LOG_INF("PCM tap: nc <this device> %d | aplay -f S16_LE -r 44100 -c 2"
		" -B 1000000 -R 1000000",
		CONFIG_ZSPOT_PCM_TAP_PORT);

	while (true) {
		int client = zsock_accept(server, NULL, NULL);

		if (client < 0) {
			k_sleep(K_SECONDS(1));
			continue;
		}
		serve(client);
		zsock_close(client);
	}
}

K_THREAD_DEFINE(pcm_tap_tid, STACK_SIZE, tap_thread, NULL, NULL, NULL, PRIORITY, 0, K_TICKS_FOREVER);

int pcm_tap_init(void)
{
	ring_buf_init(&buffer, sizeof(buffer_memory), buffer_memory);
	k_thread_name_set(pcm_tap_tid, "pcm_tap");
	k_thread_start(pcm_tap_tid);
	return 0;
}

void pcm_tap_write(const uint8_t *pcm, size_t len)
{
	if (!atomic_get(&client_connected)) {
		return;
	}

	/* Whole chunks only, so that a drop cannot split a stereo frame. */
	if (ring_buf_space_get(&buffer) < len) {
		atomic_add(&dropped_bytes, len);
		return;
	}
	ring_buf_put(&buffer, pcm, len);
	k_sem_give(&data_ready);
}
