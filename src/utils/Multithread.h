#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <ostream>

//#define SINGLETHREAD_DEBUG

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#elif defined(__linux__)
#include <sys/ioctl.h>
#endif // Windows/Linux

inline void get_terminal_size(uint16_t *width, uint16_t *height) {
#if defined(_WIN32)
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	if (width != nullptr) *width = csbi.srWindow.Right-csbi.srWindow.Left+1;
	if (height != nullptr) *height = csbi.srWindow.Bottom-csbi.srWindow.Top+1;
#elif defined(__linux__)
	winsize w{};
	ioctl(fileno(stdout), TIOCGWINSZ, &w);
	if (width != nullptr) *width = w.ws_col;
	if (height != nullptr) *height = w.ws_row;
#endif // Windows/Linux
}

template <typename EnvT, typename TaskT>
void DistributeTasks(std::ostream &out,
                     EnvT &env,
                     std::vector <TaskT> &tasks,
                     bool (*process)(EnvT &, TaskT &, size_t),
                     size_t thread_cnt = std::thread::hardware_concurrency()) {
	thread_cnt = std::min(thread_cnt, tasks.size());
	std::vector <std::thread> threads;

	std::mutex task_mutex;
	size_t next_task = 0;

	std::mutex print_mutex;
	uint16_t last_len = 0;
	uint16_t last_width = 0;
	size_t done_tasks = 0;

	std::cout << "Running " << tasks.size() << " tasks with " << thread_cnt << " threads:\n";
	get_terminal_size(&last_width, nullptr);
	last_width = std::max((uint16_t)12, last_width) - 12;
	out << "Progress: [" << std::string(last_width, '-') << "]\033[1G" << std::flush;

#ifdef SINGLETHREAD_DEBUG
	const int tid = 0;
	{
		{
#else
	for (int tid = 0; tid < thread_cnt; ++tid) {
		threads.emplace_back([&tasks, &task_mutex, &next_task, &print_mutex, tid, &out, process, &env, &done_tasks, &last_width, &last_len] {
#endif
			TaskT *running_task;
			while (true) {
				size_t running_index = 0;
				{
					std::unique_lock lock(task_mutex);
					if (next_task == tasks.size()) break;
					running_index = next_task;
					running_task = &tasks[next_task++];
				}
				const bool valid = process(env, *running_task, tid);
				{
					std::unique_lock lock(print_mutex);
					if (!valid) {
						out << "Thread " << tid << ": [" << running_index << '/' << tasks.size() << "] failed\033[0K\n";
						last_width = 0;
					}
					uint16_t width;
					get_terminal_size(&width, nullptr);
					width = std::max((uint16_t)12, width) - 12;
					done_tasks++;
					const size_t len = width * done_tasks / tasks.size();
					if (last_len != len || width != last_width) {
						last_width = width;
						last_len = len;
						out << "Progress: [" << std::string(len, '#') << std::string(width - len, '-') << "]\033[1G" << std::flush;
					}
				}
			}
#ifdef SINGLETHREAD_DEBUG
		}
	}
#else
		});
	}
	for (std::thread &thread : threads) {
		thread.join();
	}
#endif
	out << "\033[0KDone" << std::endl;
}

class ThreadPool {

public:
	ThreadPool (size_t thread_cnt = std::thread::hardware_concurrency()) {

	}
};