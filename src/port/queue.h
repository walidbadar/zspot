/*
 * Copyright (c) 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief Blocking FIFO used to hand packets between protocol threads.
 */

#ifndef ZSPOT_PORT_QUEUE_H_
#define ZSPOT_PORT_QUEUE_H_

#include <atomic>
#include <mutex>
#include <queue>

#include "port/sync.h"

namespace zspot
{

template <typename T> class Queue
{
public:
	void push(const T &data)
	{
		force_exit_.store(false);
		std::unique_lock<Mutex> lock(mutex_);
		queue_.push(data);
		lock.unlock();
		cv_.notifyOne();
	}

	bool isEmpty()
	{
		std::unique_lock<Mutex> lock(mutex_);
		return queue_.empty();
	}

	/** Non-blocking pop. @return false if the queue is empty. */
	bool pop(T &value)
	{
		std::unique_lock<Mutex> lock(mutex_);

		if (queue_.empty()) {
			return false;
		}
		value = queue_.front();
		queue_.pop();
		return true;
	}

	/** Blocking pop. @return false on forced exit. */
	bool wpop(T &value)
	{
		std::unique_lock<Mutex> lock(mutex_);

		cv_.wait(lock, [&]() { return !queue_.empty() || force_exit_.load(); });
		if (force_exit_.load()) {
			return false;
		}
		value = queue_.front();
		queue_.pop();
		return true;
	}

	/** Timed pop. @return false on timeout or forced exit. */
	bool wtpop(T &value, long milliseconds = 1000)
	{
		std::unique_lock<Mutex> lock(mutex_);

		cv_.waitFor(lock, milliseconds,
			    [&]() { return !queue_.empty() || force_exit_.load(); });
		if (force_exit_.load() || queue_.empty()) {
			return false;
		}
		value = queue_.front();
		queue_.pop();
		return true;
	}

	int size()
	{
		std::unique_lock<Mutex> lock(mutex_);
		return static_cast<int>(queue_.size());
	}

	/** Empties the queue and wakes up blocked readers. */
	void clear()
	{
		force_exit_.store(true);
		std::unique_lock<Mutex> lock(mutex_);
		while (!queue_.empty()) {
			queue_.pop();
		}
		lock.unlock();
		cv_.notifyAll();
	}

	bool isExit() const
	{
		return force_exit_.load();
	}

private:
	std::queue<T> queue_;
	Mutex mutex_;
	ConditionVariable cv_;
	std::atomic<bool> force_exit_ = false;
};

} /* namespace zspot */

#endif /* ZSPOT_PORT_QUEUE_H_ */
