#include "Multithread.h"

#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>

void ThreadPool::ThreadRoutine() {
	while (!stop_) {
		std::function<void()> task;
		{
			std::unique_lock lock(queue_mutex_);
			run_once_.wait(lock, [&] {
				return !queue_.empty() || stop_;
			});
			if (stop_) return;
			task = queue_.front();
			queue_.pop();
		}
		task();
	}
}

ThreadPool::ThreadPool (const size_t size) {
	for (size_t i = 0; i < size; i++) {
		threads_.emplace_back(&ThreadPool::ThreadRoutine, this);
	}
}
ThreadPool::~ThreadPool () {
	stop_ = true;
	for (auto &thread : threads_) {
		thread.join();
	}
}

void ThreadPool::Wait() {
	std::unique_lock lock(queue_mutex_);
	empty_queue_.wait(lock, [&] { return queue_size_ == 0; });
}
