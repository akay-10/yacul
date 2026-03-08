#include "qos_stats.h"

#include <numeric>
#include <sstream>

using namespace std;

namespace utils {
namespace qos {

// ---------------------------------------------------------------------------

void PerPriorityStats::Reset() {
  enqueued.store(0, memory_order_relaxed);
  dequeued.store(0, memory_order_relaxed);
  executed.store(0, memory_order_relaxed);
  dropped.store(0, memory_order_relaxed);
  expired.store(0, memory_order_relaxed);
  total_latency_us.store(0, memory_order_relaxed);
}

// ---------------------------------------------------------------------------

PerPriorityStats &QosStats::At(Priority p) {
  return stats_[static_cast<size_t>(p)];
}

// ---------------------------------------------------------------------------

const PerPriorityStats &QosStats::At(Priority p) const {
  return stats_[static_cast<size_t>(p)];
}

// ---------------------------------------------------------------------------

uint64_t QosStats::TotalEnqueued() const {
  uint64_t total = 0;
  for (const auto &s : stats_) {
    total += s.enqueued.load(memory_order_relaxed);
  }
  return total;
}

// ---------------------------------------------------------------------------

uint64_t QosStats::TotalDequeued() const {
  uint64_t total = 0;
  for (const auto &s : stats_) {
    total += s.dequeued.load(memory_order_relaxed);
  }
  return total;
}

// ---------------------------------------------------------------------------

uint64_t QosStats::TotalExecuted() const {
  uint64_t total = 0;
  for (const auto &s : stats_) {
    total += s.executed.load(memory_order_relaxed);
  }
  return total;
}

// ---------------------------------------------------------------------------

uint64_t QosStats::TotalDropped() const {
  uint64_t total = 0;
  for (const auto &s : stats_) {
    total += s.dropped.load(memory_order_relaxed);
  }
  return total;
}

// ---------------------------------------------------------------------------

uint64_t QosStats::TotalExpired() const {
  uint64_t total = 0;
  for (const auto &s : stats_) {
    total += s.expired.load(memory_order_relaxed);
  }
  return total;
}

// ---------------------------------------------------------------------------

double QosStats::AverageLatencyUs() const {
  uint64_t exec = TotalExecuted();
  if (exec == 0)
    return 0.0;

  uint64_t total_lat = 0;
  for (const auto &s : stats_) {
    total_lat += s.total_latency_us.load(memory_order_relaxed);
  }
  return static_cast<double>(total_lat) / static_cast<double>(exec);
}

// ---------------------------------------------------------------------------

void QosStats::Reset() {
  for (auto &s : stats_)
    s.Reset();
}

// ---------------------------------------------------------------------------

string QosStats::ToString() const {
  static const char *kNames[] = {"Background", "Low",      "Normal",
                                 "High",       "Critical", "Realtime"};

  ostringstream oss;
  oss << "QosStats {\n";
  for (size_t i = 0; i < kNumPriorityLevels; ++i) {
    const auto &s = stats_[i];
    oss << "  [" << kNames[i] << "]"
        << "  enqueued=" << s.enqueued.load(memory_order_relaxed)
        << "  dequeued=" << s.dequeued.load(memory_order_relaxed)
        << "  executed=" << s.executed.load(memory_order_relaxed)
        << "  dropped=" << s.dropped.load(memory_order_relaxed)
        << "  expired=" << s.expired.load(memory_order_relaxed) << "\n";
  }
  oss << "  avg_latency_us=" << AverageLatencyUs() << "\n";
  oss << "}";
  return oss.str();
}

} // namespace qos
} // namespace utils
