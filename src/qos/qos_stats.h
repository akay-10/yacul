#ifndef UTILS_QOS_QOS_STATS_H
#define UTILS_QOS_QOS_STATS_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "i_qos_item.h"

namespace utils {
namespace qos {

// ---------------------------------------------------------------------------
// PerPriorityStats — counters for a single priority level.
// ---------------------------------------------------------------------------
struct PerPriorityStats {
  std::atomic<uint64_t> enqueued{0};
  std::atomic<uint64_t> dequeued{0};
  std::atomic<uint64_t> executed{0};
  std::atomic<uint64_t> dropped{0};
  std::atomic<uint64_t> expired{0};
  std::atomic<uint64_t> total_latency_us{0}; // sum for mean computation

  // Non-copyable; explicit reset.
  void Reset();
};

// ---------------------------------------------------------------------------
// QosStats — aggregate statistics across all priority levels.
// ---------------------------------------------------------------------------
class QosStats {
public:
  PerPriorityStats &At(Priority p);
  const PerPriorityStats &At(Priority p) const;

  // Aggregate across all levels.
  uint64_t TotalEnqueued() const;
  uint64_t TotalDequeued() const;
  uint64_t TotalExecuted() const;
  uint64_t TotalDropped() const;
  uint64_t TotalExpired() const;

  // Average end-to-end latency in microseconds (0 if none executed).
  double AverageLatencyUs() const;

  void Reset();

  std::string ToString() const;

private:
  std::array<PerPriorityStats, kNumPriorityLevels> stats_;
};

} // namespace qos
} // namespace utils

#endif // UTILS_QOS_QOS_STATS_H
