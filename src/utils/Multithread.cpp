#include "Multithread.h"

#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>

class ThreadPool::Task {
	const std::function<void()> func_;

	std::mutex mutex_;
	std::atomic <size_t> state_;
	std::vector <Task *> chd_;

public:
	explicit Task(std::function <void()> &&func) :
		func_(std::move(func)),
		state_(1) {}

	[[nodiscard]] bool IsFinished() {
		if (state_ != -2) return false;
		std::lock_guard lock(mutex_);
		return state_ == -2;
	}

	void AddChd(Task *task) {
		if (state_ >= -2) return;
		std::lock_guard lock(mutex_);
		if (state_ >= -2) return;

		++task->state_;
		chd_.emplace_back(task);
	}

	bool ParDone() {
		--state_;

		if (state_ != 0) return false;
		std::lock_guard lock(mutex_);
		return state_ == 0;
	}

	std::queue <Task*> RunTask() {
		func_();
		state_ = -1;

		std::lock_guard lock(mutex_);
		std::queue <Task*> ready;
		for (Task *chd : chd_) {
			if (chd->ParDone()) ready.push(chd);
		}
		state_ = -2;
		return ready;
	}
};

void ThreadPool::EnqueueReady(std::queue <Task*> &ready) {
	if (ready.empty()) return;

	const size_t size = ready_.size();
	while (!ready.empty()) {
		ready_.push(ready.front());
		ready.pop();
	}

	if (size >= threads_available_) {
		new_task_.notify_all();
		return;
	}
	// TODO test to see if this is fast
	for (size_t i = 0; i < size; ++i) {
		new_task_.notify_one();
	}
}

void ThreadPool::ThreadRoutine() {
	std::queue <Task*> ready;
	Task *next = nullptr;
	while (!stop_) {
		if (next == nullptr) {
			std::unique_lock lock(ready_mutex_);
			EnqueueReady(ready);
			++threads_available_;
			if (ready_.empty() && threads_available_ == threads_.size()) tasks_done_.notify_all();
			new_task_.wait(lock, [&] { return !ready_.empty() || stop_; });
			--threads_available_;
			if (stop_) return;
			next = ready_.front();
			ready_.pop();
		}

		++tasks_started_;
		ready = next->RunTask();
		if (ready.empty()) {
			next = nullptr;
		}
		else {
			next = ready.front();
			ready.pop();
		}
	}
}

ThreadPool::ThreadPool (const size_t size) {
	for (size_t i = 0; i < size; i++) {
		threads_.emplace_back(&ThreadPool::ThreadRoutine, this);
	}
}
ThreadPool::~ThreadPool () {
	stop_ = true;
	new_task_.notify_all();
	for (auto &thread : threads_) {
		thread.join();
	}
	for (const Task *task : tasks_) {
		delete task;
	}
}

ThreadPool::TaskRef ThreadPool::Enqueue(std::function <void()> &&func, std::vector <TaskRef> &&dependencies) {
	Task *task;
	TaskRef task_ref;
	{
		std::lock_guard lock(tasks_mutex_);
		while (!tasks_.empty() && tasks_.front()->IsFinished()) {
			delete tasks_.front();
			tasks_.pop_front();
			task_offset_++;
		}
		task = new Task(std::move(func));
		for (const size_t dep : dependencies) {
			if (task_offset_ <= dep) tasks_[dep - task_offset_]->AddChd(task);
		}
		task_ref = task_offset_ + tasks_.size();
		tasks_.push_back(task);
	}

	if (!task->ParDone()) return task_ref;

#ifdef SINGLETHREAD_DEBUG
	task->RunTask();
#else
	// Make master wait for slaves so it doesn't hog ready_mutex_
	while (old_started_ == tasks_started_ && threads_available_ != 0) {}
	old_started_ = tasks_started_.load();
	{
		std::lock_guard lock(ready_mutex_);
		ready_.push(task);
	}
	new_task_.notify_one();
#endif
	return task_ref;
}

void ThreadPool::Wait(std::vector <TaskRef> &&tasks) {
#ifndef SINGLETHREAD_DEBUG
	std::condition_variable wait_done;
	Enqueue([&wait_done] {
		wait_done.notify_one();
	}, std::move(tasks));
	std::mutex mutex;
	std::unique_lock lock(mutex);
	wait_done.wait(lock);
#endif
}
void ThreadPool::Wait() {
	std::unique_lock lock(ready_mutex_);
	tasks_done_.wait(lock, [this] { return ready_.empty() && threads_available_ == threads_.size(); });
}