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

	void Enqueue(std::function <void()> &&func);

	/*template<class F, class... Args>
	auto Enqueue(F&& f, Args&&... args) {
		using ReturnT = std::invoke_result<F, Args...>::type;

		auto task = std::make_shared<std::packaged_task<ReturnT()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<ReturnT> future = task->get_future();

		Enqueue([task] { (*task)(); });

		return future;
	}*/

	void Wait();
};