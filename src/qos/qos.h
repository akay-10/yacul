#ifndef UTILS_QOS_QOS_H
#define UTILS_QOS_QOS_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "basic/basic.h"
#include "concurrency/os_lockless_queue.h"
#include "i_qos_item.h"
#include "qos_stats.h"
#include "token_bucket.h"

namespace utils {
namespace qos {

// ---------------------------------------------------------------------------
// QosConfig — all tunables for a QOS instance.
// ---------------------------------------------------------------------------
struct QosConfig {
  // --- Capacity ---
  // Maximum items per priority queue (0 = unlimited).
  size_t max_queue_depth{4096};

  // Total items across all queues (0 = unlimited).
  size_t max_total_depth{0};

  // --- Rate limiting (token bucket per priority, 0 = disabled) ---
  // tokens/second for each priority level (index == Priority numeric value).
  std::array<double, kNumPriorityLevels> rate_limit_per_priority{
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}};

  // Burst capacity multiplier (bucket size = rate × burst_multiplier).
  double burst_multiplier{2.0};

  // Global rate limit across all priorities (0 = disabled).
  double global_rate_limit{0.0};
  double global_burst_multiplier{2.0};

  // --- Scheduling ---
  // Weighted round-robin quanta: how many items to drain per priority level
  // before moving to the next lower level.
  std::array<size_t, kNumPriorityLevels> wrr_quanta{{1, 2, 4, 8, 16, 32}};

  // If true, deadline-aware scheduling is applied when items carry deadlines.
  bool deadline_scheduling{true};

  // --- Consumer threads ---
  size_t num_consumer_threads{1};

  // --- Timeouts ---
  // Items older than this are considered expired (0 = no expiry).
  std::chrono::milliseconds item_ttl{std::chrono::milliseconds{0}};

  // --- Overflow policy ---
  enum class OverflowPolicy {
    kDrop,       // silently drop new items
    kDropOldest, // evict oldest item in the full queue
    kBlock,      // block caller until space is available (use with caution)
  };
  OverflowPolicy overflow_policy{OverflowPolicy::kDrop};

  // --- Back-pressure ---
  // Fraction [0,1] of max_queue_depth at which back-pressure is signalled.
  double backpressure_threshold{0.8};
};

// ---------------------------------------------------------------------------
// QOS — production-grade Quality-of-Service queue manager.
//
// Thread safety: all public methods are safe to call concurrently from
// arbitrary threads.  Internally backed by moodycamel::ConcurrentQueue.
// ---------------------------------------------------------------------------
class QOS {
public:
  // Construct with default configuration.
  QOS();

  // Construct with explicit configuration.
  explicit QOS(QosConfig config);

  // Destructor: stops consumers and drains outstanding items.
  ~QOS();

  // Non-copyable, non-movable (owns threads and atomics).
  DISALLOW_COPY_AND_ASSIGN(QOS);
  DISALLOW_MOVE_AND_ASSIGN(QOS);

  // -------------------------------------------------------------------------
  // Lifecycle
  // -------------------------------------------------------------------------

  // Start background consumer thread(s).  Must be called before Dequeue/Run.
  void Start();

  // Gracefully stop consumers after draining remaining items.
  void Stop();

  // Pause dispatch without stopping threads (items continue to be enqueued).
  void Pause();

  // Resume after Pause().
  void Resume();

  bool IsRunning() const;
  bool IsPaused() const;

  // -------------------------------------------------------------------------
  // Submission
  // -------------------------------------------------------------------------

  // Enqueue an item at its embedded priority.
  // Returns true on success, false if rejected (rate-limited / queue full).
  bool Enqueue(IQosItemPtr item);

  // Enqueue overriding the item's own priority field.
  bool Enqueue(IQosItemPtr item, Priority priority);

  // Enqueue with an explicit deadline.
  bool Enqueue(IQosItemPtr item, Priority priority,
               std::chrono::steady_clock::time_point deadline);

  // Convenience: enqueue a plain callable wrapped in a FunctionItem.
  bool EnqueueTask(std::function<void()> fn,
                   Priority priority = Priority::kNormal, std::string tag = {});

  // -------------------------------------------------------------------------
  // Dequeue (manual consumer mode — consumers must be stopped)
  // -------------------------------------------------------------------------

  // Attempt to dequeue the highest-priority non-expired item.
  // Returns nullptr if all queues are empty.
  IQosItemPtr TryDequeue();

  // Bulk dequeue up to `max_items`; returns actual count.
  size_t TryDequeueBulk(std::vector<IQosItemPtr> &out, size_t max_items);

  // -------------------------------------------------------------------------
  // Observers
  // -------------------------------------------------------------------------
  void AddObserver(std::shared_ptr<IQosObserver> observer);
  void RemoveObserver(std::shared_ptr<IQosObserver> observer);

  // -------------------------------------------------------------------------
  // Introspection
  // -------------------------------------------------------------------------

  // Approximate total items across all queues.
  size_t SizeApprox() const;

  // Approximate size for a specific priority.
  size_t SizeApprox(Priority priority) const;

  // True when queue fill exceeds the configured back-pressure threshold.
  bool IsUnderBackPressure() const;

  // Read-only snapshot of accumulated statistics.
  const QosStats &Stats() const;
  void ResetStats();

  const QosConfig &Config() const;

private:
  // -------------------------------------------------------------------------
  // Internal types
  // -------------------------------------------------------------------------

  // Per-priority sub-queue bundle.
  struct PriorityQueue {
    moodycamel::ConcurrentQueue<IQosItemPtr> queue;
    std::unique_ptr<TokenBucket> rate_limiter; // may be nullptr
    std::atomic<size_t> depth{0};
    size_t wrr_quota{1};

    explicit PriorityQueue(size_t initial_capacity, size_t wrr_quota_);
  };

  // -------------------------------------------------------------------------
  // Internal helpers
  // -------------------------------------------------------------------------

  bool EnqueueInternal(IQosItemPtr item, Priority priority,
                       std::chrono::steady_clock::time_point deadline,
                       bool has_deadline);

  IQosItemPtr DequeueInternal();

  // Worker loop executed by each consumer thread.
  void ConsumerLoop();

  void NotifyEnqueued(const QosMetadata &meta);
  void NotifyDequeued(const QosMetadata &meta);
  void NotifyExecuted(const QosMetadata &meta,
                      std::chrono::microseconds latency);
  void NotifyDropped(const QosMetadata &meta);
  void NotifyExpired(const QosMetadata &meta);

  bool CheckRateLimit(PriorityQueue &pq, double cost);
  bool IsExpired(const IQosItemPtr &item) const;

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------

  QosConfig config_;

  std::array<std::unique_ptr<PriorityQueue>, kNumPriorityLevels> queues_;

  // Global rate limiter (optional).
  std::unique_ptr<TokenBucket> global_rate_limiter_;

  // Monotonically increasing item ID generator.
  std::atomic<QosItemId> next_id_{1};

  // Total items in-flight across all queues.
  std::atomic<size_t> total_depth_{0};

  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> stop_requested_{false};

  std::vector<std::thread> consumer_threads_;

  // Observer list (copy-on-write via snapshot approach).
  mutable std::mutex observers_mu_;
  std::vector<std::shared_ptr<IQosObserver>> observers_;

  QosStats stats_;
};

} // namespace qos
} // namespace utils

#endif // UTILS_QOS_QOS_H
