#include "Multithread.h"

#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>

//#define SINGLETHREAD_DEBUG

void ThreadPool::ThreadRoutine() {
	while (!stop_) {
		std::function<void()> task;
		{
			std::unique_lock lock(queue_mutex_);
			if (queue_.empty() && !stop_) run_once_.wait(lock, [&] {
				return !queue_.empty() || stop_;
			});
			if (stop_) return;
			task = queue_.front();
			queue_.pop();
		}
		task();
		if (--queue_size_ == 0) empty_queue_.notify_all();
	}
}

ThreadPool::ThreadPool (const size_t size) {
	for (size_t i = 0; i < size; i++) {
		threads_.emplace_back(&ThreadPool::ThreadRoutine, this);
	}
}
ThreadPool::~ThreadPool () {
	stop_ = true;
	run_once_.notify_all();
	for (auto &thread : threads_) {
		thread.join();
	}
}

void ThreadPool::Enqueue(std::function <void()> &&func) {
#ifdef SINGLETHREAD_DEBUG
	func();
#else
	++queue_size_;
	{
		std::lock_guard lock(queue_mutex_);
		queue_.push(std::move(func));
	}
	run_once_.notify_one();
#endif
}

void ThreadPool::Wait() {
	std::unique_lock lock(queue_mutex_);
	empty_queue_.wait(lock, [&] { return queue_size_ == 0; });
}
