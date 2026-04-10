#ifndef UTILS_QOS_TOKEN_BUCKET_H_
#define UTILS_QOS_TOKEN_BUCKET_H_

#include "concurrency/spinlock.h"

#include <cstdint>

namespace utils {
namespace qos {

/*
 * Thread-safe token-bucket rate limiter. Tokens accumulate at 'rate'
 * tokens/second up to 'capacity'.
 *
 * Thread-safety: All public methods are thread-safe. A Spinlock guards all
 * mutable state, keeping the implementation straightforward and correct at the
 * cost of a short critical section on every call. Prefer this over the
 * lockless variant when simplicity and auditability matter more than the
 * last nanosecond of throughput.
 */
class TokenBucket {
public:
  // rate - tokens added per second (sustained throughput). Must be > 0.
  // capacity - maximum burst size (peak tokens). Must be > 0.
  TokenBucket(double rate, double capacity);

  // Attempt to consume 'tokens' from the bucket. Returns true and debits the
  // bucket on success; returns false without modifying state on failure.
  bool TryConsume(double tokens = 1.0);

  // Force-add tokens to model an external credit. Clamped to capacity.
  void Refill(double tokens);

  // Reset the bucket to full capacity and update the refill timestamp.
  void Reset();

  // Returns the current fill level (approximate, for monitoring/metrics).
  double CurrentTokens();

  double rate() const { return rate_; }
  double capacity() const { return capacity_; }

private:
  // Must be called with mu_ held.
  void RefillLocked();

  // Immutable after construction — no synchronisation needed.
  const double rate_;
  const double capacity_;

  // All fields below are guarded by mu_.
  mutable utils::concurrency::Spinlock mu_;
  // Current token count; guarded by mu_.
  double tokens_;
  // Steady-clock timestamp of last refill; guarded by mu_.
  int64_t last_refill_ns_;
};

} // namespace qos
} // namespace utils

#endif // UTILS_QOS_TOKEN_BUCKET_H_
