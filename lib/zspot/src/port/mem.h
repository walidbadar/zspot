/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Allocator for the library's large, latency-tolerant buffers.
 *
 * With CONFIG_ZSPOT_EXTERNAL_HEAP the blocks come from the external memory
 * region of Zephyr's shared multi heap (PSRAM on ESP32) and fall back to the
 * C heap when that is unavailable. Without it, everything goes to the C heap.
 */

#ifndef ZSPOT_PORT_MEM_H_
#define ZSPOT_PORT_MEM_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *zspot_mem_alloc(size_t size);
void *zspot_mem_calloc(size_t count, size_t size);
void *zspot_mem_realloc(void *ptr, size_t size);
void *zspot_mem_alloc_aligned(size_t align, size_t size);
void zspot_mem_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* ZSPOT_PORT_MEM_H_ */
