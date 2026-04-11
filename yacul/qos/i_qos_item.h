#ifndef UTILS_QOS_I_QOS_ITEM_H
#define UTILS_QOS_I_QOS_ITEM_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace utils {
namespace qos {

// Priority levels, higher numeric value means higher scheduling precedence.
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

// QosItemId, a unique 64-bit identifier assigned at submission time.
using QosItemId = uint64_t;

// QosMetadata, a per-item metadata tracked throughout the lifecycle.
struct QosMetadata {
  QosItemId id{0};
  Priority priority{Priority::kNormal};
  std::chrono::steady_clock::time_point enqueue_time{};
  std::chrono::steady_clock::time_point deadline{};
  uint32_t weight{1};
  bool has_deadline{false};
  std::string tag{};
};

// IQosItem, an interface for every item submitted to the QOS. NOTE: Must
// implement.
class IQosItem {
public:
  using Ptr = std::shared_ptr<IQosItem>;
  using ConstPtr = std::shared_ptr<const IQosItem>;

  virtual ~IQosItem() = default;

  // Execute the item. Called by the QOS consumer.
  virtual void Execute() = 0;

  // Description for logging and diagnostics.
  virtual std::string Describe() const = 0;

  // Estimated cost in arbitrary units (bytes, CPU cycles, etc). Used by
  // admission control and rate limiting.
  virtual uint32_t Cost() const { return 1; }

  // True when the item has already expired and should be discarded.
  virtual bool IsExpired() const { return false; }

  // Accessors/Mutators for metadata.
  QosMetadata &metadata() { return metadata_; }
  const QosMetadata &metadata() const { return metadata_; }

protected:
  QosMetadata metadata_;
};

// IQosObserver, an optional sink to receive lifecycle notifications.
class IQosObserver {
public:
  using Ptr = std::shared_ptr<IQosObserver>;
  using PtrConst = std::shared_ptr<const IQosObserver>;

  virtual ~IQosObserver() = default;

  virtual void OnEnqueued(const QosMetadata &metadata) {}
  virtual void OnDequeued(const QosMetadata &metadata) {}
  virtual void OnExecuted(const QosMetadata &metadata,
                          std::chrono::microseconds latency) {}
  virtual void OnDropped(const QosMetadata &metadata) {}
  virtual void OnExpired(const QosMetadata &metadata) {}
};

} // namespace qos
} // namespace utils

#endif // UTILS_QOS_I_QOS_ITEM_H
