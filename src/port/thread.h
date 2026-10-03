/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Runnable task backed by a Zephyr thread with a dynamically allocated
 *        stack (CONFIG_DYNAMIC_THREAD + CONFIG_DYNAMIC_THREAD_ALLOC).
 */

#ifndef ZSPOT_PORT_THREAD_H_
#define ZSPOT_PORT_THREAD_H_

#include <zephyr/kernel.h>

#include <cstddef>
#include <string>

namespace zspot
{

class Task
{
public:
	/**
	 * @param name       Thread name.
	 * @param stack_size Stack size in bytes.
	 * @param priority   Relative importance, higher means more urgent. It is
	 *                   subtracted from CONFIG_ZSPOT_THREAD_PRIORITY to obtain
	 *                   the Zephyr preemptive priority.
	 */
	Task(const char *name, size_t stack_size, int priority);
	virtual ~Task();

	Task(const Task &) = delete;
	Task &operator=(const Task &) = delete;

	/** Starts (or restarts, once the previous run finished) the thread. */
	bool startTask();

	/** Waits for the thread to return from runTask(). */
	void joinTask();

protected:
	virtual void runTask() = 0;

private:
	static void entry(void *self, void *, void *);

	std::string name_;
	size_t stack_size_;
	int priority_;

	struct k_thread thread_ {};
	k_thread_stack_t *stack_ = nullptr;
	k_tid_t tid_ = nullptr;
};

} /* namespace zspot */

#endif /* ZSPOT_PORT_THREAD_H_ */
