#ifndef UTILS_CONCURRENCY_THREADPOOL_H_
#define UTILS_CONCURRENCY_THREADPOOL_H_

#include "yacul/basic/basic.h"
#include "yacul/logging/logger.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace utils {
namespace concurrency {

/*
 * ThreadPool is a production-ready thread pool with work-stealing, priority
 * queues, delayed / recurring tasks, and auto-scaling.
 *
 * CONFIGURATION:
 *   - min_threads: Minimum worker threads (threads never go below this)
 *   - max_threads: Maximum worker threads (pool auto-expands up to this)
 *   - initial_threads: Threads created at startup
 *   - max_queue_size: Max tasks in queue (0 = unlimited)
 *   - thread_idle_timeout: Idle threads retire after this duration
 *   - enable_work_stealing: Enable per-thread local queues for better cache
 * locality
 *   - enable_priority_queue: Enable priority scheduling (kLow, kNormal, kHigh,
 * kCritical)
 *   - enable_delayed_tasks: Enable delayed / recurring task submission
 *   - thread_name_prefix: Prefix for pthread names (Linux)
 *
 * USAGE EXAMPLES:
 *
 *   1. Simple submit (returns std::future):
 *      auto future = pool.Submit([] { return compute(); });
 *      auto result = future.get();  // blocks until complete
 *
 *   2. Submit with priority:
 *      pool.SubmitWithPriority(TaskPriority::kHigh, [] { process(); });
 *
 *   3. Delayed task (executes after delay):
 *      auto [future, handle] = pool.SubmitDelayed(100ms, [] { do_work(); });
 *      handle.Cancel();  // cancel before it runs
 *
 *   4. Recurring task (runs at fixed interval):
 *      auto handle = pool.SubmitRecurring(1s, [] { heartbeat(); });
 *      // handle.Cancel() to stop
 *
 *   5. Batch submission:
 *      std::vector<std::future<void>> futures =
 *        pool.SubmitBatch(tasks.begin(), tasks.end());
 *
 * IMPORTANT NOTES:
 *   - Tasks that throw are caught and counted in stats_.tasks_failed
 *   - Use CancelTask() with the returned TaskHandle to cancel tasks
 *   - Shutdown(wait_for_tasks = true) blocks until all tasks complete
 *   - ShutdownNow() drops all pending tasks immediately
 *   - Pause / Resume freezes all worker threads (tasks queue up but don't
 * execute)
 *   - GetStats() provides real-time metrics
 *
 * THREAD SAFETY:
 *   - All public methods are thread-safe
 *   - Submit returns a std::future; use .get() to retrieve results
 *   - Exceptions in tasks are caught and logged, not propagated to submitter
 *
 * PERFORMANCE TIPS:
 *   - Enable work_stealing for better throughput under load
 *   - Use priorities sparingly (adds overhead)
 *   - Avoid very long-running tasks (blocks workers)
 *   - Use SubmitBatch for bulk operations (reduces lock contention)
 *
 */

// ---------------------------------------------------------------------------

// TaskPriority
enum class TaskPriority { kLow = 0, kNormal = 1, kHigh = 2, kCritical = 3 };

// ---------------------------------------------------------------------------

// Forward declarations
struct DelayedTask;

// ---------------------------------------------------------------------------

// Returned when submitting delayed / recurring tasks. The caller can use it to
// cancel the task.
class TaskHandle {
public:
  TaskHandle() : task_id_(0), cancelled_(nullptr) {}
  TaskHandle(uint64_t id, std::shared_ptr<std::atomic<bool>> cancelled)
    : task_id_(id), cancelled_(std::move(cancelled)) {}

  void Cancel() {
    if (cancelled_)
      cancelled_->store(true, std::memory_order_relaxed);
  }

  bool IsCancelled() const {
    return cancelled_ ? cancelled_->load(std::memory_order_relaxed) : false;
  }

  uint64_t GetId() const { return task_id_; }

private:
  uint64_t task_id_;
  std::shared_ptr<std::atomic<bool>> cancelled_;
};

// ---------------------------------------------------------------------------

// All counters are atomics so they can be read without locks.
struct ThreadPoolStats {
  std::atomic<uint64_t> tasks_submitted{0};
  std::atomic<uint64_t> tasks_completed{0};
  std::atomic<uint64_t> tasks_failed{0};
  std::atomic<uint64_t> delayed_tasks_submitted{0};
  std::atomic<uint64_t> delayed_tasks_executed{0};
  std::atomic<uint64_t> tasks_cancelled{0};
  std::atomic<uint32_t> active_threads{0};
  // queue_size tracks items in the 'regular' priority queue only.
  std::atomic<uint32_t> queue_size{0};
  std::atomic<uint32_t> delayed_queue_size{0};
  std::chrono::steady_clock::time_point start_time;

  ThreadPoolStats() : start_time(std::chrono::steady_clock::now()) {}

  // Non-copyable because of atomics; but we can still zero them out.
  DISALLOW_COPY_AND_ASSIGN(ThreadPoolStats);

  double GetCompletionRate() const {
    const uint64_t submitted = tasks_submitted.load(std::memory_order_relaxed);
    const uint64_t completed = tasks_completed.load(std::memory_order_relaxed);
    return (submitted > 0)
             ? static_cast<double>(completed) / static_cast<double>(submitted)
             : 0.0;
  }

  std::chrono::milliseconds GetUptime() const {
    const auto now_time = std::chrono::steady_clock::now();
    const auto uptime = now_time - start_time;
    return std::chrono::duration_cast<std::chrono::milliseconds>(uptime);
  }

  uint32_t GetTotalQueueSize() const {
    return queue_size.load(std::memory_order_relaxed) +
           delayed_queue_size.load(std::memory_order_relaxed);
  }
};

// ---------------------------------------------------------------------------

struct ThreadPoolConfig {
  uint32_t min_threads = 1;
  uint32_t max_threads = std::thread::hardware_concurrency();
  uint32_t initial_threads =
    std::max(1u, std::thread::hardware_concurrency() / 2);
  uint32_t max_queue_size = 10000;
  // Idle workers beyond min_threads are culled after this duration.
  std::chrono::milliseconds thread_idle_timeout{30000};
  // Granularity at which the delay-scheduler wakes to promote ready tasks.
  std::chrono::milliseconds delay_scheduler_interval{10};
  bool enable_work_stealing = true;
  bool enable_priority_queue = true;
  bool enable_delayed_tasks = true;
  std::string thread_name_prefix = "ThreadPool";
};

// ---------------------------------------------------------------------------

// Stored in a min-heap ordered by execute_time (earliest first).
struct DelayedTask {
  std::chrono::steady_clock::time_point execute_time;
  TaskPriority priority;
  std::function<void()> task;
  uint64_t task_id;

  DelayedTask(std::chrono::steady_clock::time_point exec_time,
              TaskPriority prio, std::function<void()> t, uint64_t id)
    : execute_time(exec_time), priority(prio), task(std::move(t)), task_id(id) {
  }

  // std::priority_queue is a max-heap (largest element on top). We want
  // earliest time to execute first, so invert comparison.
  bool operator<(const DelayedTask &other) const {
    if (execute_time != other.execute_time) {
      // Return true if this task should come AFTER other (i.e., other has
      // earlier time)
      return execute_time > other.execute_time;
    }
    // For same execution time: higher priority value should execute first.
    // In max-heap, larger value means higher priority (comes first).
    return priority < other.priority;
  }
};

// ---------------------------------------------------------------------------

class ThreadPool {
public:
  explicit ThreadPool(ThreadPoolConfig config = ThreadPoolConfig{});

  // Convenience constructor; fixed-size pool with sensible defaults.
  explicit ThreadPool(uint32_t num_threads);

  ~ThreadPool();

  // Submission

  // Submit a callable with NORMAL priority.
  template <typename F, typename... Args>
  auto Submit(F &&f, Args &&...args)
    -> std::future<std::invoke_result_t<F, Args...>> {

    return SubmitWithPriority(TaskPriority::kNormal, std::forward<F>(f),
                              std::forward<Args>(args)...);
  }

  // Submit a callable with an explicit priority.
  template <typename F, typename... Args>
  auto SubmitWithPriority(TaskPriority priority, F &&f, Args &&...args)
    -> std::future<std::invoke_result_t<F, Args...>> {

    using ReturnType = std::invoke_result_t<F, Args...>;

    auto shared_task = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> result = shared_task->get_future();

    EnqueueTask(priority, [this, shared_task, priority]() mutable {
      stats_.active_threads.fetch_add(1, std::memory_order_relaxed);
      try {
        (*shared_task)();
        stats_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
      } catch (...) {
        stats_.tasks_failed.fetch_add(1, std::memory_order_relaxed);
        LOG(ERROR) << "Worker caught unhandled exception in task";
      }
      stats_.active_threads.fetch_sub(1, std::memory_order_relaxed);
      finish_condition_.notify_all();
    });

    return result;
  }

  // Submit a callable to run after 'delay'.
  template <typename F, typename... Args>
  auto SubmitDelayed(std::chrono::milliseconds delay, F &&f, Args &&...args)
    -> std::pair<std::future<std::invoke_result_t<F, Args...>>, TaskHandle> {

    return SubmitDelayedWithPriority(delay, TaskPriority::kNormal,
                                     std::forward<F>(f),
                                     std::forward<Args>(args)...);
  }

  // Submit a callable to run after 'delay' with 'priority'.
  template <typename F, typename... Args>
  auto SubmitDelayedWithPriority(std::chrono::milliseconds delay,
                                 TaskPriority priority, F &&f, Args &&...args)
    -> std::pair<std::future<std::invoke_result_t<F, Args...>>, TaskHandle> {

    using ReturnType = std::invoke_result_t<F, Args...>;

    CHECK(config_.enable_delayed_tasks)
      << "Delayed tasks are disabled in this ThreadPool";

    auto shared_task = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> result = shared_task->get_future();

    const uint64_t task_id =
      next_task_id_.fetch_add(1, std::memory_order_relaxed);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    auto execute_time = std::chrono::steady_clock::now() + delay;

    auto wrapped = [this, shared_task, cancelled, task_id]() mutable {
      if (cancelled->load(std::memory_order_relaxed)) {
        stats_.tasks_cancelled.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      stats_.active_threads.fetch_add(1, std::memory_order_relaxed);
      try {
        (*shared_task)();
        stats_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
        stats_.delayed_tasks_executed.fetch_add(1, std::memory_order_relaxed);
      } catch (...) {
        stats_.tasks_failed.fetch_add(1, std::memory_order_relaxed);
        LOG(ERROR) << "Worker caught unhandled exception in delayed task; "
                   << LOGVARS(task_id);
      }
      stats_.active_threads.fetch_sub(1, std::memory_order_relaxed);
      finish_condition_.notify_all();
    };

    {
      std::lock_guard<std::mutex> lock(delayed_queue_mutex_);
      CHECK(!shutdown_.load(std::memory_order_relaxed))
        << "Cannot submit tasks to a shutdown ThreadPool";
      delayed_tasks_.emplace(execute_time, priority, std::move(wrapped),
                             task_id);
      cancelled_tasks_[task_id] = cancelled;
      stats_.delayed_tasks_submitted.fetch_add(1, std::memory_order_relaxed);
      stats_.delayed_queue_size.fetch_add(1, std::memory_order_relaxed);
    }
    delayed_condition_.notify_one();

    return {std::move(result), TaskHandle(task_id, cancelled)};
  }

  // Submit at an absolute time-point.
  template <typename F, typename... Args>
  auto SubmitAt(std::chrono::steady_clock::time_point when, F &&f,
                Args &&...args)
    -> std::pair<std::future<std::invoke_result_t<F, Args...>>, TaskHandle> {

    return SubmitAtWithPriority(when, TaskPriority::kNormal, std::forward<F>(f),
                                std::forward<Args>(args)...);
  }

  // Submit at an absolute time-point with 'priority'.
  template <typename F, typename... Args>
  auto SubmitAtWithPriority(std::chrono::steady_clock::time_point when,
                            TaskPriority priority, F &&f, Args &&...args)
    -> std::pair<std::future<std::invoke_result_t<F, Args...>>, TaskHandle> {

    const auto now = std::chrono::steady_clock::now();
    if (when <= now) {
      // Execute immediately on the regular queue.
      auto fut = SubmitWithPriority(priority, std::forward<F>(f),
                                    std::forward<Args>(args)...);
      return {std::move(fut), TaskHandle()};
    }
    const auto delay =
      std::chrono::duration_cast<std::chrono::milliseconds>(when - now);
    return SubmitDelayedWithPriority(delay, priority, std::forward<F>(f),
                                     std::forward<Args>(args)...);
  }

  // Submit a recurring task with an 'interval'.
  template <typename F, typename... Args>
  TaskHandle SubmitRecurring(std::chrono::milliseconds interval, F &&f,
                             Args &&...args) {

    return SubmitRecurringWithPriority(interval, TaskPriority::kNormal,
                                       std::forward<F>(f),
                                       std::forward<Args>(args)...);
  }

  // Submit a recurring task with an 'interval' and 'priority'.
  template <typename F, typename... Args>
  TaskHandle SubmitRecurringWithPriority(std::chrono::milliseconds interval,
                                         TaskPriority priority, F &&f,
                                         Args &&...args) {

    CHECK(config_.enable_delayed_tasks)
      << "Delayed tasks require enable_delayed_tasks = true";

    const uint64_t task_id =
      next_task_id_.fetch_add(1, std::memory_order_relaxed);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);

    // Wrap user callable + args into a shared_ptr<function> so the lambda
    // that reschedules itself can safely capture it.
    auto user_fn = std::make_shared<std::function<void()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    // shared_ptr to the recurring body itself – filled after construction.
    auto body = std::make_shared<std::function<void()>>();

    *body = [this, interval, priority, cancelled, task_id, user_fn,
             body]() mutable {
      if (cancelled->load(std::memory_order_relaxed) ||
          shutdown_.load(std::memory_order_relaxed)) {
        return;
      }
      try {
        (*user_fn)();
        stats_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
      } catch (...) {
        stats_.tasks_failed.fetch_add(1, std::memory_order_relaxed);
        LOG(ERROR) << "Recurring task " << LOGVARS(task_id)
                   << " threw an exception.";
      }
      // Reschedule unless cancelled/shutdown.
      if (!cancelled->load(std::memory_order_relaxed) &&
          !shutdown_.load(std::memory_order_relaxed)) {
        // Copy body into a plain std::function for submission.
        std::function<void()> next_body = *body;
        auto [future, handle] =
          SubmitDelayedWithPriority(interval, priority, std::move(next_body));
        (void)future;
        (void)handle;
      }
    };

    // Register the cancellation token before first submission.
    {
      std::lock_guard<std::mutex> lock(delayed_queue_mutex_);
      cancelled_tasks_[task_id] = cancelled;
    }

    // Kick off the first execution immediately (delay = 0).
    std::function<void()> first = *body;
    auto [future, handle] = SubmitDelayedWithPriority(
      std::chrono::milliseconds(0), priority, std::move(first));
    (void)future;

    return TaskHandle(task_id, cancelled);
  }

  // Submit a batch of tasks.
  template <typename Iterator>
  auto SubmitBatch(Iterator first, Iterator last)
    -> std::vector<std::future<std::invoke_result_t<
      typename std::iterator_traits<Iterator>::value_type>>> {

    using TaskType = typename std::iterator_traits<Iterator>::value_type;
    using RetType = std::invoke_result_t<TaskType>;

    std::vector<std::future<RetType>> futures;
    for (auto it = first; it != last; ++it) {
      futures.emplace_back(Submit(*it));
    }
    return futures;
  }

  // Cancellation

  bool CancelTask(const TaskHandle &handle);
  bool CancelTask(uint64_t task_id);

  // Lifecycle

  // Block until all queued and in-flight regular tasks complete.
  void WaitForTasks();
  void Pause();
  void Resume();
  bool IsPaused() const { return paused_.load(std::memory_order_relaxed); }

  // Graceful shutdown; optionally waiting for queued tasks.
  void Shutdown(bool wait_for_tasks = true);

  // Immediate shutdown; drops all pending tasks.
  void ShutdownNow();

  bool IsShutdown() const { return shutdown_.load(std::memory_order_relaxed); }

  // Pool management

  // Resize pool to [min_threads, max_threads].
  void Resize(uint32_t new_size);
  uint32_t GetThreadCount() const;
  uint32_t GetActiveThreadCount() const {
    return stats_.active_threads.load(std::memory_order_relaxed);
  }
  uint32_t GetQueueSize() const;
  uint32_t GetDelayedTasksCount() const {
    return stats_.delayed_queue_size.load(std::memory_order_relaxed);
  }

  // Statistics

  const ThreadPoolStats &GetStats() const { return stats_; }
  void ResetStats();

  // Configuration

  const ThreadPoolConfig &GetConfig() const { return config_; }
  void SetMaxQueueSize(uint32_t max_size);

private:
  DISALLOW_COPY_AND_ASSIGN(ThreadPool);

  // Enqueue a pre-wrapped task onto the appropriate regular queue. Must be
  // called WITHOUT queue_mutex_ held.
  void EnqueueTask(TaskPriority priority, std::function<void()> task);

  // Worker loop for a single thread.
  void WorkerLoop(uint32_t thread_id);
  void DelaySchedulerLoop();
  void ProcessDelayedTasks();

  // Add a new worker thread to the pool.
  void AddWorkerThread();

  // Expand if the queue depth has grown beyond 2× the current pool size and
  // all threads appear busy.
  void TryExpandPool();

  // Attempt to steal one task from a peer thread-local queue. Returns nullptr
  // if nothing is available.
  std::function<void()> StealTask(uint32_t thief_id);

  // Dequeue the next task from the shared priority / FIFO queue. Returns false
  // if no task is available. Must be called with queue_mutex_.
  bool DequeueTask(std::function<void()> &out_task);

  static void SetThreadName(const std::string &name);

  // Internal types

  struct PriorityTask {
    int priority;
    std::function<void()> task;

    PriorityTask(int p, std::function<void()> t)
      : priority(p), task(std::move(t)) {}

    // std::priority_queue is a max-heap (largest element on top). Higher
    // priority should execute first.
    bool operator<(const PriorityTask &other) const {
      return priority < other.priority;
    }
  };

  // State

  ThreadPoolConfig config_;

  std::atomic<bool> shutdown_{false};
  std::atomic<bool> paused_{false};
  std::atomic<uint64_t> next_task_id_{1};

  // Worker threads.
  mutable std::mutex threads_mutex_;
  std::vector<std::thread> threads_;

  // Per-thread queues for work-stealing. Sized to max_threads at construction;
  // index == thread_id % max_threads.
  std::vector<std::unique_ptr<std::queue<std::function<void()>>>>
    thread_local_queues_;

  // Shared regular task queues; only one is used depending on config.
  std::queue<std::function<void()>> tasks_;
  std::priority_queue<PriorityTask> priority_tasks_;

  // Delayed task components.
  std::priority_queue<DelayedTask> delayed_tasks_;
  std::map<uint64_t, std::shared_ptr<std::atomic<bool>>> cancelled_tasks_;
  std::thread delay_scheduler_thread_;

  // Synchronisation.
  mutable std::mutex queue_mutex_;
  mutable std::mutex delayed_queue_mutex_;
  std::condition_variable condition_;         // regular queue non-empty
  std::condition_variable delayed_condition_; // delayed scheduler wakeup
  std::condition_variable pause_condition_;   // resume after pause
  std::condition_variable finish_condition_;  // WaitForTasks

  ThreadPoolStats stats_;
  std::atomic<uint32_t> next_thread_id_{0};
};

// ---------------------------------------------------------------------------

// Singleton wrapper
class GlobalThreadPool {
public:
  static ThreadPool &Instance();

  template <typename F, typename... Args>
  static auto Submit(F &&f, Args &&...args)
    -> decltype(Instance().Submit(std::forward<F>(f),
                                  std::forward<Args>(args)...)) {
    return Instance().Submit(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  static auto SubmitDelayed(std::chrono::milliseconds delay, F &&f,
                            Args &&...args)
    -> decltype(Instance().SubmitDelayed(delay, std::forward<F>(f),
                                         std::forward<Args>(args)...)) {
    return Instance().SubmitDelayed(delay, std::forward<F>(f),
                                    std::forward<Args>(args)...);
  }

private:
  GlobalThreadPool() = default;
  DISALLOW_COPY_AND_ASSIGN(GlobalThreadPool);
  static std::once_flag initialized_;
  static std::unique_ptr<ThreadPool> instance_;
};

// ---------------------------------------------------------------------------

// Parallel utilities

// Apply 'func' to every element in [first, last) in parallel.
template <typename Iterator, typename Function>
void ParallelFor(Iterator first, Iterator last, Function func) {
  auto &pool = GlobalThreadPool::Instance();
  std::vector<std::future<void>> futures;
  futures.reserve(std::distance(first, last));
  for (auto it = first; it != last; ++it) {
    futures.emplace_back(pool.Submit([&func, it]() { func(*it); }));
  }
  for (auto &f : futures) {
    f.wait();
  }
}

// ---------------------------------------------------------------------------

// Transform every element of 'input' using 'func', returning a new vector.
template <typename Container, typename Function>
auto ParallelTransform(const Container &input, Function func) -> std::vector<
  std::invoke_result_t<Function, typename Container::value_type>> {
  using RetType =
    std::invoke_result_t<Function, typename Container::value_type>;

  auto &pool = GlobalThreadPool::Instance();
  std::vector<std::future<RetType>> futures;
  futures.reserve(input.size());

  for (const auto &item : input) {
    futures.emplace_back(pool.Submit([func, item]() { return func(item); }));
  }

  std::vector<RetType> results;
  results.reserve(futures.size());
  for (auto &f : futures) {
    results.emplace_back(f.get());
  }
  return results;
}

// ---------------------------------------------------------------------------

} // namespace concurrency
} // namespace utils

#endif // UTILS_CONCURRENCY_THREADPOOL_H_
