/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <future>
#include <memory>
#include <object_pool.hpp>
#include "variant.hpp"
#include "intrusive.hpp"

namespace Granite
{
class ThreadGroup;

struct TaskSignal
{
	std::condition_variable cond;
	std::mutex lock;
	uint64_t counter = 0;

	void signal_increment();
	void wait_until_at_least(uint64_t count);
};

namespace Internal
{
struct TaskGroup;
struct TaskDeps;
struct Task;

struct TaskDepsDeleter
{
	void operator()(TaskDeps *deps);
};

struct TaskGroupDeleter
{
	void operator()(TaskGroup *group);
};

struct TaskDeps : Util::IntrusivePtrEnabled<TaskDeps, TaskDepsDeleter, Util::MultiThreadCounter>
{
	explicit TaskDeps(ThreadGroup *group)
	    : group(group)
	{
		count.store(0, std::memory_order_relaxed);
		dependency_count.store(0, std::memory_order_relaxed);
	}

	ThreadGroup *group;
	std::vector<Util::IntrusivePtr<TaskDeps>> pending;
	std::atomic_uint count;

	std::vector<Task *> pending_tasks;
	TaskSignal *signal = nullptr;
	std::atomic_uint dependency_count;

	void task_completed();
	void dependency_satisfied();
	void notify_dependees();

	std::condition_variable cond;
	std::mutex cond_lock;
	bool done = false;
};
using TaskDepsHandle = Util::IntrusivePtr<TaskDeps>;

struct TaskGroup : Util::IntrusivePtrEnabled<TaskGroup, TaskGroupDeleter, Util::MultiThreadCounter>
{
	explicit TaskGroup(ThreadGroup *group);
	~TaskGroup();
	void flush();
	void wait();

	ThreadGroup *group;
	TaskDepsHandle deps;
	void enqueue_task(std::function<void ()> func);
	void set_fence_counter_signal(TaskSignal *signal);

	unsigned id = 0;
	bool flushed = false;
};

struct Task
{
	Task(TaskDepsHandle deps, std::function<void ()> func)
		: deps(std::move(deps)), func(std::move(func))
	{
	}

	Task() = default;

	TaskDepsHandle deps;
	std::function<void ()> func;
};
}

using TaskGroup = Util::IntrusivePtr<Internal::TaskGroup>;

class ThreadGroup
{
public:
	ThreadGroup();
	~ThreadGroup();
	ThreadGroup(ThreadGroup &&) = delete;
	void operator=(ThreadGroup &&) = delete;

	void start(unsigned num_threads);

	unsigned get_num_threads() const
	{
		return unsigned(thread_group.size());
	}

	void stop();

	static unsigned get_current_thread_index();

	void enqueue_task(TaskGroup &group, std::function<void ()> func);
	TaskGroup create_task(std::function<void ()> func);
	TaskGroup create_task();

	void move_to_ready_tasks(const std::vector<Internal::Task *> &list);

	void add_dependency(TaskGroup &dependee, TaskGroup &dependency);

	void free_task_group(Internal::TaskGroup *group);
	void free_task_deps(Internal::TaskDeps *deps);

	void submit(TaskGroup &group);
	void wait_idle();
	bool is_idle();

	static void register_main_thread();

private:
	Util::ThreadSafeObjectPool<Internal::Task> task_pool;
	Util::ThreadSafeObjectPool<Internal::TaskGroup> task_group_pool;
	Util::ThreadSafeObjectPool<Internal::TaskDeps> task_deps_pool;

	std::queue<Internal::Task *> ready_tasks;

	std::vector<std::unique_ptr<std::thread>> thread_group;
	std::mutex cond_lock;
	std::condition_variable cond;

	void thread_looper(unsigned self_index);

	bool active = false;
	bool dead = false;

	std::condition_variable wait_cond;
	std::mutex wait_cond_lock;
	std::atomic_uint total_tasks;
	std::atomic_uint completed_tasks;
};
}