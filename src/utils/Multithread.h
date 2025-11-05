#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>

class IThreadPool {
public:
	typedef size_t TaskRef;

	TaskRef Enqueue(std::function <void()> &&func, std::vector <TaskRef> &&dependencies);
	TaskRef Enqueue(std::function <void()> &&func);

	void Wait(std::vector<TaskRef> &&tasks);
	void Wait();
};

class ThreadPoolDummy : public IThreadPool {
public:
	TaskRef Enqueue(std::function <void()> &&func, std::vector <TaskRef> &&dependencies) { func(); return 0; }
	TaskRef Enqueue(std::function <void()> &&func) { func(); return 0; }

	void Wait(std::vector<TaskRef> &&tasks) {}
	void Wait() {}
};

class ThreadPool : public IThreadPool {
	class Task;

	std::mutex tasks_mutex_;
	std::deque <Task*> tasks_;
	size_t task_offset_ = 1;

	std::mutex ready_mutex_;
	std::queue <Task*> ready_;
	std::condition_variable new_task_;
	std::condition_variable tasks_done_;

	// Slave threads
	std::atomic<bool> stop_ = false;
	std::vector <std::thread> threads_;

	// Stats
	std::atomic <size_t> tasks_started_ = 0;
	std::atomic <size_t> old_started_ = -1;
	std::atomic <size_t> threads_available_ = 0; // only modified under ready_mutex_

	void EnqueueReady(std::queue <Task*> &ready);

	void ThreadRoutine();

public:
	explicit ThreadPool (size_t size = std::thread::hardware_concurrency());
	~ThreadPool ();

	TaskRef Enqueue(std::function <void()> &&func, std::vector <TaskRef> &&dependencies);
	TaskRef Enqueue(std::function <void()> &&func) { return Enqueue(std::move(func), {}); }

	/*template<class F, class... Args>
	auto Enqueue(F&& f, Args&&... args) {
		using ReturnT = std::invoke_result<F, Args...>::type;

		auto task = std::make_shared<std::packaged_task<ReturnT()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<ReturnT> future = task->get_future();

		Enqueue([task] { (*task)(); });

		return future;
	}*/

	void Wait(std::vector<TaskRef> &&tasks);
	void Wait();
};
