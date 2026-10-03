/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/mem.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_ZSPOT_EXTERNAL_HEAP
#include <zephyr/multi_heap/shared_multi_heap.h>
#endif

/* Bookkeeping stored right before every block handed out. */
struct block_header {
	void *raw;      /* pointer returned by the underlying allocator */
	uint32_t size;  /* usable size in bytes */
	uint32_t external;
};

#define HEADER_SIZE ROUND_UP(sizeof(struct block_header), 16)

static struct block_header *header_of(void *ptr)
{
	return (struct block_header *)((uint8_t *)ptr - HEADER_SIZE);
}

void *zspot_mem_alloc_aligned(size_t align, size_t size)
{
	size_t total;
	uint8_t *raw = NULL;
	bool external = false;
	struct block_header *header;

	if (align < HEADER_SIZE) {
		align = HEADER_SIZE;
	}
	/* One alignment unit in front of the block holds the header. */
	total = ROUND_UP(size + align, align);

#ifdef CONFIG_ZSPOT_EXTERNAL_HEAP
	raw = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, align, total);
	external = (raw != NULL);
#endif
	if (raw == NULL) {
		raw = aligned_alloc(align, total);
	}
	if (raw == NULL) {
		return NULL;
	}

	header = (struct block_header *)(raw + align - HEADER_SIZE);
	header->raw = raw;
	header->size = size;
	header->external = external;
	return raw + align;
}

void *zspot_mem_alloc(size_t size)
{
	return zspot_mem_alloc_aligned(16, size);
}

void *zspot_mem_calloc(size_t count, size_t size)
{
	size_t total;
	void *ptr;

	if (size_mul_overflow(count, size, &total)) {
		return NULL;
	}
	ptr = zspot_mem_alloc(total);
	if (ptr != NULL) {
		memset(ptr, 0, total);
	}
	return ptr;
}

void *zspot_mem_realloc(void *ptr, size_t size)
{
	void *fresh;

	if (ptr == NULL) {
		return zspot_mem_alloc(size);
	}
	if (size == 0) {
		zspot_mem_free(ptr);
		return NULL;
	}

	fresh = zspot_mem_alloc(size);
	if (fresh != NULL) {
		memcpy(fresh, ptr, MIN(size, header_of(ptr)->size));
		zspot_mem_free(ptr);
	}
	return fresh;
}

void zspot_mem_free(void *ptr)
{
	struct block_header *header;

	if (ptr == NULL) {
		return;
	}
	header = header_of(ptr);
#ifdef CONFIG_ZSPOT_EXTERNAL_HEAP
	if (header->external) {
		shared_multi_heap_free(header->raw);
		return;
	}
#endif
	free(header->raw);
}
