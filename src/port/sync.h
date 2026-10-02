/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Synchronisation primitives built on Zephyr kernel objects.
 *
 * The Zephyr SDK ships libstdc++ without thread support, so std::mutex and
 * std::condition_variable do not exist. These classes provide what the
 * protocol core needs and work with std::lock_guard, std::unique_lock and
 * std::scoped_lock. Method names follow the std / upstream cspot API they
 * stand in for.
 */

#ifndef CSPOT_PORT_SYNC_H_
#define CSPOT_PORT_SYNC_H_

#include <zephyr/kernel.h>

#include <cstdint>
#include <mutex>

namespace cspot
{

class Mutex
{
public:
	Mutex()
	{
		k_mutex_init(&mutex_);
	}
	Mutex(const Mutex &) = delete;
	Mutex &operator=(const Mutex &) = delete;

	void lock()
	{
		k_mutex_lock(&mutex_, K_FOREVER);
	}
	bool try_lock()
	{
		return k_mutex_lock(&mutex_, K_NO_WAIT) == 0;
	}
	void unlock()
	{
		k_mutex_unlock(&mutex_);
	}

	struct k_mutex *native()
	{
		return &mutex_;
	}

private:
	struct k_mutex mutex_;
};

class ConditionVariable
{
public:
	ConditionVariable()
	{
		k_condvar_init(&condvar_);
	}
	ConditionVariable(const ConditionVariable &) = delete;
	ConditionVariable &operator=(const ConditionVariable &) = delete;

	void wait(std::unique_lock<Mutex> &lock)
	{
		k_condvar_wait(&condvar_, lock.mutex()->native(), K_FOREVER);
	}

	template <class Predicate> void wait(std::unique_lock<Mutex> &lock, Predicate pred)
	{
		while (!pred()) {
			wait(lock);
		}
	}

	/** @return the predicate's value when returning. */
	template <class Predicate>
	bool waitFor(std::unique_lock<Mutex> &lock, int64_t milliseconds, Predicate pred)
	{
		const int64_t deadline = k_uptime_get() + milliseconds;

		while (!pred()) {
			const int64_t left = deadline - k_uptime_get();

			if (left <= 0) {
				return pred();
			}
			k_condvar_wait(&condvar_, lock.mutex()->native(), K_MSEC(left));
		}
		return true;
	}

	void notifyOne()
	{
		k_condvar_signal(&condvar_);
	}
	void notifyAll()
	{
		k_condvar_broadcast(&condvar_);
	}

private:
	struct k_condvar condvar_;
};

/** Counting semaphore with the call shape of upstream bell::WrappedSemaphore. */
class Semaphore
{
public:
	explicit Semaphore(unsigned int max_count = K_SEM_MAX_LIMIT)
	{
		k_sem_init(&sem_, 0, max_count);
	}
	Semaphore(const Semaphore &) = delete;
	Semaphore &operator=(const Semaphore &) = delete;

	/** Blocks until given. @return 0 */
	int wait()
	{
		return k_sem_take(&sem_, K_FOREVER);
	}

	/** Waits up to @p milliseconds. @return 0 when taken, non-zero on timeout. */
	int twait(long milliseconds = 10)
	{
		return k_sem_take(&sem_, K_MSEC(milliseconds));
	}

	void give()
	{
		k_sem_give(&sem_);
	}

private:
	struct k_sem sem_;
};

} /* namespace cspot */

#endif /* CSPOT_PORT_SYNC_H_ */
