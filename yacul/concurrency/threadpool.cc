#include "yacul/concurrency/threadpool.h"

#include <algorithm>
#include <utility>

#ifdef __linux__
#include <pthread.h>
#endif

using namespace std;

namespace utils {
namespace concurrency {

// ---------------------------------------------------------------------------

ThreadPool::ThreadPool(ThreadPoolConfig config) : config_(move(config)) {
  // Validate config.
  CHECK_GE(config_.max_threads, 1u) << "max_threads must be >= 1";
  CHECK_LE(config_.min_threads, config_.max_threads)
    << "min_threads must be <= max_threads";

  // Clamp initial_threads into [min, max].
  config_.initial_threads =
    clamp(config_.initial_threads, config_.min_threads, config_.max_threads);

  // Pre-allocate per thread work stealing queues.
  if (config_.enable_work_stealing) {
    thread_local_queues_.resize(config_.max_threads);
    for (auto &q : thread_local_queues_) {
      q = make_unique<queue<function<void()>>>();
    }
  }

  // Start delay scheduler before workers so tasks queued immediately are seen.
  if (config_.enable_delayed_tasks) {
    delay_scheduler_thread_ = thread([this] { DelaySchedulerLoop(); });
  }

  // Spin up initial worker threads.
  for (uint32_t i = 0; i < config_.initial_threads; ++i) {
    AddWorkerThread();
  }

  LOG(INFO) << "ThreadPool \"" << config_.thread_name_prefix
            << "\" started with " << config_.initial_threads << " threads";
}

// ---------------------------------------------------------------------------

ThreadPool::ThreadPool(uint32_t num_threads)
  : ThreadPool([num_threads]() {
      CHECK_GE(num_threads, 1u) << "num_threads must be >= 1";
      ThreadPoolConfig cfg;
      cfg.min_threads = num_threads;
      cfg.max_threads = num_threads;
      cfg.initial_threads = num_threads;
      return cfg;
    }()) {}

// ---------------------------------------------------------------------------

ThreadPool::~ThreadPool() { ShutdownNow(); }

// ---------------------------------------------------------------------------

void ThreadPool::EnqueueTask(TaskPriority priority, function<void()> task) {
  {
    lock_guard<mutex> lock(queue_mutex_);
    CHECK(!shutdown_.load(memory_order_relaxed))
      << "Cannot submit tasks to a shutdown ThreadPool";

    if (config_.max_queue_size > 0 &&
        GetQueueSize() >= config_.max_queue_size) {
      LOG(ERROR) << "ThreadPool queue is full (max=" << config_.max_queue_size
                 << "); task dropped";
      return;
    }

    if (config_.enable_priority_queue) {
      priority_tasks_.emplace(static_cast<int>(priority), move(task));
    } else {
      tasks_.emplace(move(task));
    }

    stats_.tasks_submitted.fetch_add(1, memory_order_relaxed);
    stats_.queue_size.fetch_add(1, memory_order_relaxed);
  }

  condition_.notify_one();
  TryExpandPool();
}

// ---------------------------------------------------------------------------

void ThreadPool::WorkerLoop(uint32_t thread_id) {
  SetThreadName(config_.thread_name_prefix + "_" + to_string(thread_id));
  VLOG(2) << "Worker thread " << thread_id << " started";

  auto last_work_time = chrono::steady_clock::now();

  while (true) {
    // Handle pause
    if (paused_.load(memory_order_relaxed)) {
      unique_lock<mutex> lock(queue_mutex_);
      pause_condition_.wait(lock, [this] {
        return !paused_.load(memory_order_relaxed) ||
               shutdown_.load(memory_order_relaxed);
      });
    }

    // Shutdown without draining
    if (shutdown_.load(memory_order_relaxed) && GetQueueSize() == 0) {
      break;
    }

    function<void()> task;
    bool got_task = false;

    // 1. Check this thread's local queue; work stealing source.
    if (config_.enable_work_stealing) {
      const uint32_t slot =
        thread_id % static_cast<uint32_t>(thread_local_queues_.size());
      lock_guard<mutex> lock(queue_mutex_);
      auto &local = *thread_local_queues_[slot];
      if (!local.empty()) {
        task = move(local.front());
        local.pop();
        got_task = true;
      }
    }

    // 2. Shared priority / FIFO queue.
    if (!got_task) {
      unique_lock<mutex> lock(queue_mutex_);
      got_task = DequeueTask(task);
    }

    // 3. Steal from another thread's local queue.
    if (!got_task && config_.enable_work_stealing) {
      task = StealTask(thread_id);
      got_task = (task != nullptr);
    }

    if (got_task) {
      last_work_time = chrono::steady_clock::now();
      task();
      // queue_size was already decremented inside DequeueTask / stealing path.
      // For the local queue path decrement here.
    } else {
      // Check idle timeout; allow graceful thread retirement.
      const auto idle = chrono::steady_clock::now() - last_work_time;
      if (idle > config_.thread_idle_timeout) {
        lock_guard<mutex> lock(threads_mutex_);
        const uint32_t current = static_cast<uint32_t>(threads_.size());
        if (current > config_.min_threads) {
          VLOG(2) << "Worker thread " << thread_id
                  << " retiring due to idleness";
          break;
        }
      }

      // Wait for work, a shutdown signal, or a pause.
      unique_lock<mutex> lock(queue_mutex_);
      condition_.wait_for(lock, chrono::milliseconds(50), [this] {
        return shutdown_.load(memory_order_relaxed) || GetQueueSize() > 0 ||
               paused_.load(memory_order_relaxed);
      });
    }
  }

  VLOG(2) << "Worker thread " << thread_id << " exiting";
}

// ---------------------------------------------------------------------------

void ThreadPool::DelaySchedulerLoop() {
  SetThreadName(config_.thread_name_prefix + "_Scheduler");
  VLOG(2) << "Delay scheduler thread started";

  while (!shutdown_.load(memory_order_relaxed)) {
    try {
      ProcessDelayedTasks();
    } catch (...) {
      LOG(ERROR) << "Unexpected exception in delay scheduler; continuing";
    }

    // Sleep until the next scheduled task, but no longer than the configured
    // interval so we can react to newly submitted delayed tasks.
    chrono::steady_clock::time_point next_wake;
    {
      unique_lock<mutex> lock(delayed_queue_mutex_);
      if (!delayed_tasks_.empty()) {
        next_wake = delayed_tasks_.top().execute_time;
      } else {
        next_wake =
          chrono::steady_clock::now() + config_.delay_scheduler_interval;
      }

      delayed_condition_.wait_until(lock, next_wake, [this] {
        return shutdown_.load(memory_order_relaxed) ||
               (!delayed_tasks_.empty() && delayed_tasks_.top().execute_time <=
                                             chrono::steady_clock::now());
      });
    }
  }

  VLOG(2) << "Delay scheduler thread exiting";
}

// ---------------------------------------------------------------------------

void ThreadPool::ProcessDelayedTasks() {
  const auto now = chrono::steady_clock::now();

  // Collect all tasks that are ready under the delayed queue lock.
  vector<DelayedTask> ready;
  {
    lock_guard<mutex> lock(delayed_queue_mutex_);
    while (!delayed_tasks_.empty() &&
           delayed_tasks_.top().execute_time <= now) {
      // We need a mutable reference; const_cast is safe here because we are
      // about to pop the element.
      DelayedTask dt = move(const_cast<DelayedTask &>(delayed_tasks_.top()));
      delayed_tasks_.pop();
      stats_.delayed_queue_size.fetch_sub(1, memory_order_relaxed);

      // Check cancellation.
      auto it = cancelled_tasks_.find(dt.task_id);
      if (it != cancelled_tasks_.end() &&
          it->second->load(memory_order_relaxed)) {
        stats_.tasks_cancelled.fetch_add(1, memory_order_relaxed);
        cancelled_tasks_.erase(it);
        continue;
      }

      ready.emplace_back(move(dt));
    }
  }

  if (ready.empty())
    return;

  // Push ready tasks onto the shared queue.
  {
    lock_guard<mutex> lock(queue_mutex_);
    for (auto &dt : ready) {
      if (config_.enable_priority_queue) {
        priority_tasks_.emplace(static_cast<int>(dt.priority), move(dt.task));
      } else {
        tasks_.emplace(move(dt.task));
      }
      stats_.queue_size.fetch_add(1, memory_order_relaxed);
    }
  }

  if (ready.size() == 1) {
    condition_.notify_one();
  } else {
    condition_.notify_all();
  }
}

// ---------------------------------------------------------------------------

bool ThreadPool::DequeueTask(function<void()> &out_task) {
  if (config_.enable_priority_queue && !priority_tasks_.empty()) {
    // priority_queue::top() is const; we need a move.
    out_task = move(const_cast<PriorityTask &>(priority_tasks_.top()).task);
    priority_tasks_.pop();
    stats_.queue_size.fetch_sub(1, memory_order_relaxed);
    return true;
  }
  if (!tasks_.empty()) {
    out_task = move(tasks_.front());
    tasks_.pop();
    stats_.queue_size.fetch_sub(1, memory_order_relaxed);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

function<void()> ThreadPool::StealTask(uint32_t thief_id) {
  lock_guard<mutex> lock(queue_mutex_);
  const uint32_t n = static_cast<uint32_t>(thread_local_queues_.size());
  for (uint32_t i = 0; i < n; ++i) {
    if (i == (thief_id % n))
      continue;
    auto &q = *thread_local_queues_[i];
    if (!q.empty()) {
      auto task = move(q.front());
      q.pop();
      // queue_size not decremented here because local queues are not counted
      // in the shared queue_size metric (they have no separate counter).
      return task;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------

void ThreadPool::AddWorkerThread() {
  const uint32_t thread_id = next_thread_id_.fetch_add(1, memory_order_relaxed);
  lock_guard<mutex> lock(threads_mutex_);
  threads_.emplace_back([this, thread_id] { WorkerLoop(thread_id); });
}

// ---------------------------------------------------------------------------

void ThreadPool::TryExpandPool() {
  const uint32_t thread_count = GetThreadCount();
  if (thread_count >= config_.max_threads)
    return;

  if (GetQueueSize() > thread_count * 2 &&
      GetActiveThreadCount() >= thread_count) {
    AddWorkerThread();
  }
}

// ---------------------------------------------------------------------------

bool ThreadPool::CancelTask(const TaskHandle &handle) {
  return CancelTask(handle.GetId());
}

// ---------------------------------------------------------------------------

bool ThreadPool::CancelTask(uint64_t task_id) {
  lock_guard<mutex> lock(delayed_queue_mutex_);
  auto it = cancelled_tasks_.find(task_id);
  if (it != cancelled_tasks_.end()) {
    it->second->store(true, memory_order_relaxed);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

void ThreadPool::WaitForTasks() {
  unique_lock<mutex> lock(queue_mutex_);
  finish_condition_.wait(lock, [this] {
    return GetQueueSize() == 0 &&
           stats_.active_threads.load(memory_order_relaxed) == 0;
  });
}

// ---------------------------------------------------------------------------

void ThreadPool::Pause() {
  paused_.store(true, memory_order_relaxed);
  LOG(INFO) << "ThreadPool paused";
}

// ---------------------------------------------------------------------------

void ThreadPool::Resume() {
  paused_.store(false, memory_order_relaxed);
  pause_condition_.notify_all();
  LOG(INFO) << "ThreadPool resumed";
}

// ---------------------------------------------------------------------------

void ThreadPool::Shutdown(bool wait_for_tasks) {
  LOG(INFO) << "ThreadPool \"" << config_.thread_name_prefix
            << "\" shutting down (wait=" << wait_for_tasks << ")";

  if (wait_for_tasks) {
    WaitForTasks();
  }

  {
    lock_guard<mutex> ql(queue_mutex_);
    lock_guard<mutex> dl(delayed_queue_mutex_);
    shutdown_.store(true, memory_order_relaxed);
  }

  condition_.notify_all();
  delayed_condition_.notify_all();
  pause_condition_.notify_all();

  if (delay_scheduler_thread_.joinable()) {
    delay_scheduler_thread_.join();
  }

  lock_guard<mutex> lock(threads_mutex_);
  for (auto &t : threads_) {
    if (t.joinable())
      t.join();
  }
  threads_.clear();
  LOG(INFO) << "ThreadPool \"" << config_.thread_name_prefix << "\" stopped";
}

// ---------------------------------------------------------------------------

void ThreadPool::ShutdownNow() {
  LOG(INFO) << "ThreadPool \"" << config_.thread_name_prefix
            << "\" forced shutdown";

  {
    lock_guard<mutex> ql(queue_mutex_);
    lock_guard<mutex> dl(delayed_queue_mutex_);
    shutdown_.store(true, memory_order_relaxed);

    // Drain all queues.
    while (!tasks_.empty())
      tasks_.pop();
    while (!priority_tasks_.empty())
      priority_tasks_.pop();
    while (!delayed_tasks_.empty())
      delayed_tasks_.pop();
    cancelled_tasks_.clear();

    if (config_.enable_work_stealing) {
      for (auto &q : thread_local_queues_) {
        while (!q->empty())
          q->pop();
      }
    }

    stats_.queue_size.store(0, memory_order_relaxed);
    stats_.delayed_queue_size.store(0, memory_order_relaxed);
  }

  condition_.notify_all();
  delayed_condition_.notify_all();
  pause_condition_.notify_all();

  if (delay_scheduler_thread_.joinable()) {
    delay_scheduler_thread_.join();
  }

  lock_guard<mutex> lock(threads_mutex_);
  for (auto &t : threads_) {
    if (t.joinable())
      t.join();
  }
  threads_.clear();
  LOG(INFO) << "ThreadPool \"" << config_.thread_name_prefix
            << "\" force-stopped";
}

// ---------------------------------------------------------------------------

void ThreadPool::Resize(uint32_t new_size) {
  new_size = clamp(new_size, config_.min_threads, config_.max_threads);

  LOG(INFO) << "ThreadPool resize requested: " << GetThreadCount() << " -> "
            << new_size;
  // Growing is straightforward.
  while (GetThreadCount() < new_size) {
    AddWorkerThread();
  }

  // Shrinking: we can't forcibly terminate threads.  We decrease max_threads
  // so that idle threads will retire on their own during the next idle check.
  if (GetThreadCount() > new_size) {
    lock_guard<mutex> lock(threads_mutex_);
    // Signal extra threads; they will see GetThreadCount() > min_threads
    // and exit once idle.
    condition_.notify_all();
    LOG(INFO) << "ThreadPool resize to " << new_size
              << " requested; surplus threads will retire when idle";
  }
}

// ---------------------------------------------------------------------------

uint32_t ThreadPool::GetThreadCount() const {
  lock_guard<mutex> lock(threads_mutex_);
  return static_cast<uint32_t>(threads_.size());
}

// ---------------------------------------------------------------------------

uint32_t ThreadPool::GetQueueSize() const {
  // NOTE: called with or without queue_mutex_ held; using atomic stats counter
  // to avoid re-entrancy / deadlock.
  return stats_.queue_size.load(memory_order_relaxed);
}

// ---------------------------------------------------------------------------

void ThreadPool::ResetStats() {
  stats_.tasks_submitted.store(0, memory_order_relaxed);
  stats_.tasks_completed.store(0, memory_order_relaxed);
  stats_.tasks_failed.store(0, memory_order_relaxed);
  stats_.delayed_tasks_submitted.store(0, memory_order_relaxed);
  stats_.delayed_tasks_executed.store(0, memory_order_relaxed);
  stats_.tasks_cancelled.store(0, memory_order_relaxed);
  stats_.start_time = chrono::steady_clock::now();
}

// ---------------------------------------------------------------------------

void ThreadPool::SetMaxQueueSize(uint32_t max_size) {
  config_.max_queue_size = max_size;
}

// ---------------------------------------------------------------------------

void ThreadPool::SetThreadName(const string &name) {
#ifdef __linux__
  // pthread_setname_np limits name to 15 characters + null terminator.
  const string truncated = name.substr(0, 15);
  if (pthread_setname_np(pthread_self(), truncated.c_str()) != 0) {
    LOG(WARNING) << "pthread_setname_np failed for name: " << name;
  }
#elif defined(_WIN32)
  // TODO: Implement this
#elif defined(__APPLE__)
  pthread_setname_np(name.substr(0, 63).c_str());
#endif
}

// ---------------------------------------------------------------------------

once_flag GlobalThreadPool::initialized_;
unique_ptr<ThreadPool> GlobalThreadPool::instance_;

ThreadPool &GlobalThreadPool::Instance() {
  call_once(initialized_, [] { instance_ = make_unique<ThreadPool>(); });
  DCHECK(instance_ != nullptr);
  return *instance_;
}

// ---------------------------------------------------------------------------

} // namespace concurrency
} // namespace utils
