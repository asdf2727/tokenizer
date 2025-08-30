#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>

class ThreadPool {
	std::vector <std::thread> threads_;
	std::condition_variable run_once_;
	std::condition_variable empty_queue_;
	std::atomic <size_t> queue_size_ = 0;

	std::mutex queue_mutex_;
	std::queue<std::function<void()>> queue_;

	void ThreadRoutine();

public:
	std::atomic<bool> stop_ = false;

	explicit ThreadPool (size_t size = std::thread::hardware_concurrency());
	~ThreadPool ();

	template<class F, class... Args>
	auto Enqueue(F&& f, Args&&... args) {
		using ReturnT = std::invoke_result<F, Args...>::type;

		auto task = std::make_shared<std::packaged_task<ReturnT()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<ReturnT> future = task->get_future();

		++queue_size_;
		{
			std::lock_guard lock(queue_mutex_);
			queue_.emplace([this, task] {
				(*task)();
				if (--queue_size_ == 0) empty_queue_.notify_all();
			});
		}
		run_once_.notify_one();

		return future;
	}

	void Wait();
};