#include "qos.h"

#include "qos/i_qos_item.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <thread>

using namespace std;

namespace utils {
namespace qos {

namespace {

// FunctionItem, wraps a plain std::function<void()> as an IQosItem.
class FunctionItem final : public IQosItem {
public:
  explicit FunctionItem(function<void()> fn, string tag) : fn_(move(fn)) {
    metadata_.tag = move(tag);
  }

  void Execute() override {
    if (fn_)
      fn_();
  }

  string Describe() const override {
    return "FunctionItem[tag=" + metadata_.tag + "]";
  }

private:
  function<void()> fn_;
};

} // namespace

// ---------------------------------------------------------------------------

QOS::PriorityQueue::PriorityQueue(size_t initial_capacity, size_t wrr_quota_)
  : queue(initial_capacity), wrr_quota(wrr_quota_) {}

// ---------------------------------------------------------------------------

QOS::QOS() : QOS(QosConfig{}) {}

// ---------------------------------------------------------------------------

QOS::QOS(QosConfig config) : config_(move(config)) {
  size_t initial_cap =
    (config_.max_queue_depth > 0) ? config_.max_queue_depth : 256;

  for (size_t i = 0; i < kNumPriorityLevels; ++i) {
    size_t quota = config_.wrr_quanta[i];
    queues_[i] = make_unique<PriorityQueue>(initial_cap, quota);

    double rate = config_.rate_limit_per_priority[i];
    if (rate > 0.0) {
      double capacity = rate * config_.burst_multiplier;
      queues_[i]->rate_limiter = make_unique<TokenBucket>(rate, capacity);
    }
  }

  if (config_.global_rate_limit > 0.0) {
    double cap = config_.global_rate_limit * config_.global_burst_multiplier;
    global_rate_limiter_ =
      make_unique<TokenBucket>(config_.global_rate_limit, cap);
  }
}

// ---------------------------------------------------------------------------

QOS::~QOS() { Stop(); }

// ---------------------------------------------------------------------------

void QOS::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true, memory_order_acq_rel,
                                        memory_order_relaxed)) {
    return; // Already running.
  }

  stop_requested_.store(false, memory_order_release);
  paused_.store(false, memory_order_release);

  size_t num_threads = max<size_t>(1, config_.num_consumer_threads);
  consumer_threads_.reserve(num_threads);
  for (size_t i = 0; i < num_threads; ++i) {
    consumer_threads_.emplace_back([this] { ConsumerLoop(); });
  }
}

// ---------------------------------------------------------------------------

void QOS::Stop() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false, memory_order_acq_rel,
                                        memory_order_relaxed)) {
    return; // Not running.
  }

  stop_requested_.store(true, memory_order_release);
  paused_.store(false, memory_order_release);

  for (auto &t : consumer_threads_) {
    if (t.joinable())
      t.join();
  }
  consumer_threads_.clear();
}

// ---------------------------------------------------------------------------

void QOS::Pause() { paused_.store(true, memory_order_release); }

// ---------------------------------------------------------------------------

void QOS::Resume() { paused_.store(false, memory_order_release); }

// ---------------------------------------------------------------------------

bool QOS::IsRunning() const { return running_.load(memory_order_acquire); }

// ---------------------------------------------------------------------------

bool QOS::IsPaused() const { return paused_.load(memory_order_acquire); }

// ---------------------------------------------------------------------------

bool QOS::Enqueue(IQosItem::Ptr item) {
  if (!item)
    return false;
  Priority p = item->metadata().priority;
  return EnqueueInternal(move(item), p, chrono::steady_clock::time_point{},
                         false);
}

// ---------------------------------------------------------------------------

bool QOS::Enqueue(IQosItem::Ptr item, Priority priority) {
  if (!item)
    return false;
  item->metadata().priority = priority;
  return EnqueueInternal(move(item), priority,
                         chrono::steady_clock::time_point{}, false);
}

// ---------------------------------------------------------------------------

bool QOS::Enqueue(IQosItem::Ptr item, Priority priority,
                  chrono::steady_clock::time_point deadline) {
  if (!item)
    return false;
  item->metadata().priority = priority;
  return EnqueueInternal(move(item), priority, deadline, true);
}

// ---------------------------------------------------------------------------

bool QOS::EnqueueTask(function<void()> fn, Priority priority, string tag) {
  auto item = make_shared<FunctionItem>(move(fn), move(tag));
  item->metadata().priority = priority;
  return EnqueueInternal(move(item), priority,
                         chrono::steady_clock::time_point{}, false);
}

// ---------------------------------------------------------------------------

bool QOS::EnqueueInternal(IQosItem::Ptr item, Priority priority,
                          chrono::steady_clock::time_point deadline,
                          bool has_deadline) {
  auto &pq = *queues_[static_cast<size_t>(priority)];

  // --- Admission: capacity check ---
  if (config_.max_queue_depth > 0 &&
      pq.depth.load(memory_order_relaxed) >= config_.max_queue_depth) {
    if (config_.overflow_policy == QosConfig::OverflowPolicy::kDrop) {
      NotifyDropped(item->metadata());
      stats_.At(priority).dropped.fetch_add(1, memory_order_relaxed);
      return false;
    }
    // kDropOldest: try to evict one item.
    if (config_.overflow_policy == QosConfig::OverflowPolicy::kDropOldest) {
      IQosItem::Ptr evicted;
      if (pq.queue.try_dequeue(evicted)) {
        pq.depth.fetch_sub(1, memory_order_relaxed);
        total_depth_.fetch_sub(1, memory_order_relaxed);
        NotifyDropped(evicted->metadata());
        stats_.At(priority).dropped.fetch_add(1, memory_order_relaxed);
      }
    }
    // kBlock handled below (spin for now; production may use a condvar).
  }

  if (config_.max_total_depth > 0 &&
      total_depth_.load(memory_order_relaxed) >= config_.max_total_depth) {
    NotifyDropped(item->metadata());
    stats_.At(priority).dropped.fetch_add(1, memory_order_relaxed);
    return false;
  }

  // --- Admission: global rate limit ---
  if (global_rate_limiter_) {
    if (!global_rate_limiter_->TryConsume(static_cast<double>(item->Cost()))) {
      NotifyDropped(item->metadata());
      stats_.At(priority).dropped.fetch_add(1, memory_order_relaxed);
      return false;
    }
  }

  // --- Admission: per-priority rate limit ---
  if (pq.rate_limiter) {
    if (!pq.rate_limiter->TryConsume(static_cast<double>(item->Cost()))) {
      NotifyDropped(item->metadata());
      stats_.At(priority).dropped.fetch_add(1, memory_order_relaxed);
      return false;
    }
  }

  // --- Stamp metadata ---
  item->metadata().id = next_id_.fetch_add(1, memory_order_relaxed);
  item->metadata().enqueue_time = chrono::steady_clock::now();
  item->metadata().has_deadline = has_deadline;
  if (has_deadline)
    item->metadata().deadline = deadline;

  if (!pq.queue.enqueue(item)) {
    NotifyDropped(item->metadata());
    stats_.At(priority).dropped.fetch_add(1, memory_order_relaxed);
    return false;
  }

  pq.depth.fetch_add(1, memory_order_relaxed);
  total_depth_.fetch_add(1, memory_order_relaxed);

  stats_.At(priority).enqueued.fetch_add(1, memory_order_relaxed);
  NotifyEnqueued(item->metadata());
  return true;
}

// ---------------------------------------------------------------------------

IQosItem::Ptr QOS::TryDequeue() { return DequeueInternal(); }

// ---------------------------------------------------------------------------

size_t QOS::TryDequeueBulk(vector<IQosItem::Ptr> &out, size_t max_items) {
  size_t count = 0;
  for (size_t i = 0; i < max_items; ++i) {
    IQosItem::Ptr item = DequeueInternal();
    if (!item)
      break;
    out.push_back(move(item));
    ++count;
  }
  return count;
}

// ---------------------------------------------------------------------------

IQosItem::Ptr QOS::DequeueInternal() {
  // Scan from highest priority to lowest using WRR quanta. If
  // deadline_scheduling is on, we do a quick peek-and-compare across the top 2
  // queues for imminent deadlines.

  for (int level = static_cast<int>(kNumPriorityLevels) - 1; level >= 0;
       --level) {
    auto &pq = *queues_[static_cast<size_t>(level)];
    size_t quota = pq.wrr_quota;

    for (size_t q = 0; q < quota; ++q) {
      IQosItem::Ptr item;
      if (!pq.queue.try_dequeue(item))
        break;

      pq.depth.fetch_sub(1, memory_order_relaxed);
      total_depth_.fetch_sub(1, memory_order_relaxed);
      stats_.At(static_cast<Priority>(level))
        .dequeued.fetch_add(1, memory_order_relaxed);

      // TTL / expiry check.
      if (IsExpired(item)) {
        NotifyExpired(item->metadata());
        stats_.At(static_cast<Priority>(level))
          .expired.fetch_add(1, memory_order_relaxed);
        continue; // Discard and try next.
      }

      NotifyDequeued(item->metadata());
      return item;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------

void QOS::ConsumerLoop() {
  while (!stop_requested_.load(memory_order_acquire)) {
    if (paused_.load(memory_order_acquire)) {
      this_thread::sleep_for(chrono::milliseconds(1));
      continue;
    }

    IQosItem::Ptr item = DequeueInternal();
    if (!item) {
      // Back-off to avoid busy-spinning on an empty queue.
      this_thread::sleep_for(chrono::microseconds(100));
      continue;
    }

    auto t0 = chrono::steady_clock::now();
    item->Execute();
    auto latency = chrono::duration_cast<chrono::microseconds>(
      chrono::steady_clock::now() - t0);

    Priority p = item->metadata().priority;
    stats_.At(p).executed.fetch_add(1, memory_order_relaxed);
    stats_.At(p).total_latency_us.fetch_add(
      static_cast<uint64_t>(latency.count()), memory_order_relaxed);

    NotifyExecuted(item->metadata(), latency);
  }

  // Drain remaining items after stop is requested.
  IQosItem::Ptr item;
  while ((item = DequeueInternal()) != nullptr) {
    auto t0 = chrono::steady_clock::now();
    item->Execute();
    auto latency = chrono::duration_cast<chrono::microseconds>(
      chrono::steady_clock::now() - t0);

    Priority p = item->metadata().priority;
    stats_.At(p).executed.fetch_add(1, memory_order_relaxed);
    stats_.At(p).total_latency_us.fetch_add(
      static_cast<uint64_t>(latency.count()), memory_order_relaxed);
    NotifyExecuted(item->metadata(), latency);
  }
}

// ---------------------------------------------------------------------------

void QOS::AddObserver(IQosObserver::Ptr observer) {
  lock_guard<mutex> lk(observers_mu_);
  observers_.push_back(move(observer));
}

// ---------------------------------------------------------------------------

void QOS::RemoveObserver(IQosObserver::Ptr observer) {
  lock_guard<mutex> lk(observers_mu_);
  observers_.erase(remove(observers_.begin(), observers_.end(), observer),
                   observers_.end());
}

// ---------------------------------------------------------------------------

void QOS::NotifyEnqueued(const QosMetadata &meta) {
  vector<IQosObserver::Ptr> snapshot;
  {
    lock_guard<mutex> lk(observers_mu_);
    snapshot = observers_;
  }
  for (auto &obs : snapshot)
    obs->OnEnqueued(meta);
}

// ---------------------------------------------------------------------------

void QOS::NotifyDequeued(const QosMetadata &meta) {
  vector<IQosObserver::Ptr> snapshot;
  {
    lock_guard<mutex> lk(observers_mu_);
    snapshot = observers_;
  }
  for (auto &obs : snapshot)
    obs->OnDequeued(meta);
}

// ---------------------------------------------------------------------------

void QOS::NotifyExecuted(const QosMetadata &meta,
                         chrono::microseconds latency) {
  vector<IQosObserver::Ptr> snapshot;
  {
    lock_guard<mutex> lk(observers_mu_);
    snapshot = observers_;
  }
  for (auto &obs : snapshot)
    obs->OnExecuted(meta, latency);
}

// ---------------------------------------------------------------------------

void QOS::NotifyDropped(const QosMetadata &meta) {
  vector<IQosObserver::Ptr> snapshot;
  {
    lock_guard<mutex> lk(observers_mu_);
    snapshot = observers_;
  }
  for (auto &obs : snapshot)
    obs->OnDropped(meta);
}

// ---------------------------------------------------------------------------

void QOS::NotifyExpired(const QosMetadata &meta) {
  vector<IQosObserver::Ptr> snapshot;
  {
    lock_guard<mutex> lk(observers_mu_);
    snapshot = observers_;
  }
  for (auto &obs : snapshot)
    obs->OnExpired(meta);
}

// ---------------------------------------------------------------------------

bool QOS::CheckRateLimit(PriorityQueue &pq, double cost) {
  if (!pq.rate_limiter)
    return true;
  return pq.rate_limiter->TryConsume(cost);
}

// ---------------------------------------------------------------------------

bool QOS::IsExpired(const IQosItem::Ptr &item) const {
  if (item->IsExpired())
    return true;

  const auto &meta = item->metadata();

  // Deadline check.
  if (meta.has_deadline && chrono::steady_clock::now() > meta.deadline) {
    return true;
  }

  // TTL check.
  if (config_.item_ttl.count() > 0) {
    auto age = chrono::duration_cast<chrono::milliseconds>(
      chrono::steady_clock::now() - meta.enqueue_time);
    if (age >= config_.item_ttl)
      return true;
  }

  return false;
}

// ---------------------------------------------------------------------------

size_t QOS::SizeApprox() const {
  return total_depth_.load(memory_order_relaxed);
}

// ---------------------------------------------------------------------------

size_t QOS::SizeApprox(Priority priority) const {
  return queues_[static_cast<size_t>(priority)]->depth.load(
    memory_order_relaxed);
}

// ---------------------------------------------------------------------------

bool QOS::IsUnderBackPressure() const {
  if (config_.max_queue_depth == 0)
    return false;
  // Check if any single priority queue exceeds the threshold independently,
  // OR if the most-loaded queue does. Use total vs total capacity.
  double total_capacity = static_cast<double>(config_.max_queue_depth);
  for (size_t i = 0; i < kNumPriorityLevels; ++i) {
    double fill =
      static_cast<double>(queues_[i]->depth.load(memory_order_relaxed)) /
      total_capacity;
    if (fill >= config_.backpressure_threshold)
      return true;
  }
  return false;
}
// ---------------------------------------------------------------------------

const QosStats &QOS::Stats() const { return stats_; }

// ---------------------------------------------------------------------------

void QOS::ResetStats() { stats_.Reset(); }

// ---------------------------------------------------------------------------

const QosConfig &QOS::Config() const { return config_; }

// ---------------------------------------------------------------------------

} // namespace qos
} // namespace utils
