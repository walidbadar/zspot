/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Routes the C++ free store through zspot_mem_* so that containers, strings
 * and protocol objects live in external memory where available.
 */

#include <new>

#include "port/mem.h"

static void *alloc_or_throw(size_t size, size_t align)
{
	void *ptr = zspot_mem_alloc_aligned(align, size == 0 ? 1 : size);

	if (ptr == nullptr) {
		throw std::bad_alloc();
	}
	return ptr;
}

void *operator new(size_t size)
{
	return alloc_or_throw(size, 16);
}

void *operator new[](size_t size)
{
	return alloc_or_throw(size, 16);
}

void *operator new(size_t size, std::align_val_t align)
{
	return alloc_or_throw(size, static_cast<size_t>(align));
}

void *operator new[](size_t size, std::align_val_t align)
{
	return alloc_or_throw(size, static_cast<size_t>(align));
}

void *operator new(size_t size, const std::nothrow_t &) noexcept
{
	return zspot_mem_alloc_aligned(16, size == 0 ? 1 : size);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
	return zspot_mem_alloc_aligned(16, size == 0 ? 1 : size);
}

void operator delete(void *ptr) noexcept
{
	zspot_mem_free(ptr);
}

void operator delete[](void *ptr) noexcept
{
	zspot_mem_free(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
	zspot_mem_free(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
	zspot_mem_free(ptr);
}

void operator delete(void *ptr, std::align_val_t) noexcept
{
	zspot_mem_free(ptr);
}

void operator delete[](void *ptr, std::align_val_t) noexcept
{
	zspot_mem_free(ptr);
}

void operator delete(void *ptr, size_t, std::align_val_t) noexcept
{
	zspot_mem_free(ptr);
}

void operator delete[](void *ptr, size_t, std::align_val_t) noexcept
{
	zspot_mem_free(ptr);
}
