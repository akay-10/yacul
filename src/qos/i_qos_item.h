#ifndef UTILS_QOS_I_QOS_ITEM_H
#define UTILS_QOS_I_QOS_ITEM_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace utils {
namespace qos {

// ---------------------------------------------------------------------------
// Priority levels — higher numeric value = higher scheduling precedence.
// ---------------------------------------------------------------------------
enum class Priority : uint8_t {
  kBackground = 0,
  kLow = 1,
  kNormal = 2,
  kHigh = 3,
  kCritical = 4,
  kRealtime = 5,
  kNumLevels = 6,
};

constexpr size_t kNumPriorityLevels = static_cast<size_t>(Priority::kNumLevels);

// ---------------------------------------------------------------------------
// QosItemId — unique 64-bit identifier assigned at submission time.
// ---------------------------------------------------------------------------
using QosItemId = uint64_t;

// ---------------------------------------------------------------------------
// QosMetadata — per-item metadata tracked throughout the lifecycle.
// ---------------------------------------------------------------------------
struct QosMetadata {
  QosItemId id{0};
  Priority priority{Priority::kNormal};
  std::chrono::steady_clock::time_point enqueue_time{};
  std::chrono::steady_clock::time_point deadline{};
  uint32_t weight{1};
  bool has_deadline{false};
  std::string tag{}; // optional label/category
};

// ---------------------------------------------------------------------------
// IQosItem — interface every item submitted to the QOS must implement.
// Concrete items (network packet, callable task, event, …) inherit this.
// ---------------------------------------------------------------------------
class IQosItem {
public:
  virtual ~IQosItem() = default;

  // Execute/dispatch the item.  Called by the QOS consumer.
  virtual void Execute() = 0;

  // Human-readable description (for logging / diagnostics).
  virtual std::string Describe() const = 0;

  // Estimated cost in arbitrary units (bytes, CPU cycles, …).
  // Used by admission control and rate limiting.
  virtual uint32_t Cost() const { return 1; }

  // True when the item has already expired and should be discarded.
  virtual bool IsExpired() const { return false; }

  // Mutable access to scheduling metadata.
  QosMetadata &metadata() { return metadata_; }
  const QosMetadata &metadata() const { return metadata_; }

protected:
  QosMetadata metadata_;
};

using IQosItemPtr = std::shared_ptr<IQosItem>;

// ---------------------------------------------------------------------------
// IQosObserver — optional sink to receive lifecycle notifications.
// ---------------------------------------------------------------------------
class IQosObserver {
public:
  virtual ~IQosObserver() = default;

  virtual void OnEnqueued(const QosMetadata & /*meta*/) {}
  virtual void OnDequeued(const QosMetadata & /*meta*/) {}
  virtual void OnExecuted(const QosMetadata & /*meta*/,
                          std::chrono::microseconds /*latency*/) {}
  virtual void OnDropped(const QosMetadata & /*meta*/) {}
  virtual void OnExpired(const QosMetadata & /*meta*/) {}
};

} // namespace qos
} // namespace utils

#endif // UTILS_QOS_I_QOS_ITEM_H
