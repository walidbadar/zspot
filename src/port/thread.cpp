/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "port/thread.h"

#include "port/log.h"
#include "port/mem.h"

CSPOT_LOG_MODULE_DECLARE();

namespace cspot
{

static int to_zephyr_priority(int task_priority)
{
	int prio = CONFIG_CSPOT_THREAD_PRIORITY - task_priority;

	if (prio < 0) {
		prio = 0;
	}
	if (prio > CONFIG_NUM_PREEMPT_PRIORITIES - 1) {
		prio = CONFIG_NUM_PREEMPT_PRIORITIES - 1;
	}
	return prio;
}

Task::Task(const char *name, size_t stack_size, int priority)
	: name_(name), stack_size_(stack_size), priority_(priority)
{
}

Task::~Task()
{
	joinTask();
	if (stack_ != nullptr) {
#ifdef CONFIG_CSPOT_STACKS_EXTERNAL
		cspot_mem_free(stack_);
#else
		k_thread_stack_free(stack_);
#endif
		stack_ = nullptr;
	}
}

void Task::entry(void *self, void *, void *)
{
	static_cast<Task *>(self)->runTask();
}

bool Task::startTask()
{
	/* A previous run may still be winding down; wait before reusing the stack. */
	joinTask();

	if (stack_ == nullptr) {
#ifdef CONFIG_CSPOT_STACKS_EXTERNAL
		stack_ = static_cast<k_thread_stack_t *>(cspot_mem_alloc_aligned(
			Z_KERNEL_STACK_OBJ_ALIGN, K_KERNEL_STACK_LEN(stack_size_)));
#else
		stack_ = k_thread_stack_alloc(stack_size_, 0);
#endif
		if (stack_ == nullptr) {
			LOG_ERR("Cannot allocate %u bytes of stack for %s",
				static_cast<unsigned int>(stack_size_), name_.c_str());
			return false;
		}
	}

	tid_ = k_thread_create(&thread_, stack_, stack_size_, entry, this, nullptr, nullptr,
			       to_zephyr_priority(priority_), 0, K_NO_WAIT);
	if (tid_ == nullptr) {
		LOG_ERR("Cannot create thread %s", name_.c_str());
		return false;
	}

	k_thread_name_set(tid_, name_.c_str());
	return true;
}

void Task::joinTask()
{
	if (tid_ != nullptr && tid_ != k_current_get()) {
		k_thread_join(tid_, K_FOREVER);
		tid_ = nullptr;
	}
}

} /* namespace cspot */
