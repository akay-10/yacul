#ifndef UTILS_QOS_TOKEN_BUCKET_H
#define UTILS_QOS_TOKEN_BUCKET_H

#include <atomic>
#include <chrono>
#include <cstdint>

namespace utils {
namespace qos {

// ---------------------------------------------------------------------------
// TokenBucket — thread-safe token-bucket rate limiter.
//
// Tokens accumulate at `rate` tokens/second up to `capacity`.
// Callers consume tokens via TryConsume(); if insufficient tokens are
// available the call returns false immediately (non-blocking).
// ---------------------------------------------------------------------------
class TokenBucket {
public:
  // rate     — tokens added per second (sustained throughput).
  // capacity — maximum burst size (peak tokens).
  TokenBucket(double rate, double capacity);

  // Attempt to consume `tokens` from the bucket.
  // Returns true and debits the bucket on success; false otherwise.
  bool TryConsume(double tokens = 1.0);

  // Force-add tokens (e.g. to model a credit).
  void Refill(double tokens);

  // Reset to full capacity.
  void Reset();

  // Current fill level (approximate, for monitoring).
  double CurrentTokens() const;

  double rate() const { return rate_; }
  double capacity() const { return capacity_; }

private:
  void RefillInternal() const;

  double rate_;
  double capacity_;
  mutable std::atomic<double> tokens_;
  mutable std::atomic<int64_t> last_refill_ns_;
};

} // namespace qos
} // namespace utils

#endif // UTILS_QOS_TOKEN_BUCKET_H
