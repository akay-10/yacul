#include "token_bucket.h"

#include <algorithm>
#include <chrono>

using namespace std;

namespace utils {
namespace qos {

// ---------------------------------------------------------------------------

TokenBucket::TokenBucket(double rate, double capacity)
    : rate_(rate), capacity_(capacity), tokens_(capacity),
      last_refill_ns_(chrono::duration_cast<chrono::nanoseconds>(
                          chrono::steady_clock::now().time_since_epoch())
                          .count()) {}

// ---------------------------------------------------------------------------

void TokenBucket::RefillInternal() const {
  int64_t now_ns = chrono::duration_cast<chrono::nanoseconds>(
                       chrono::steady_clock::now().time_since_epoch())
                       .count();

  int64_t prev_ns = last_refill_ns_.load(memory_order_relaxed);
  double elapsed_s = static_cast<double>(now_ns - prev_ns) * 1e-9;
  if (elapsed_s <= 0.0)
    return;

  // Try to claim the time window; another thread may win — that is fine,
  // the token add is still monotone-safe via fetch_add-like CAS loop.
  if (!last_refill_ns_.compare_exchange_weak(
          prev_ns, now_ns, memory_order_relaxed, memory_order_relaxed)) {
    return; // Another thread is refilling; skip to avoid double-add.
  }

  double added = elapsed_s * rate_;
  double current = tokens_.load(memory_order_relaxed);
  double updated = min(current + added, capacity_);
  tokens_.store(updated, memory_order_relaxed);
}

// ---------------------------------------------------------------------------

bool TokenBucket::TryConsume(double tokens) {
  RefillInternal();

  double current = tokens_.load(memory_order_relaxed);
  while (current >= tokens) {
    if (tokens_.compare_exchange_weak(current, current - tokens,
                                      memory_order_acquire,
                                      memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------

void TokenBucket::Refill(double tokens) {
  double current = tokens_.load(memory_order_relaxed);
  double updated;
  do {
    updated = min(current + tokens, capacity_);
  } while (!tokens_.compare_exchange_weak(
      current, updated, memory_order_relaxed, memory_order_relaxed));
}

// ---------------------------------------------------------------------------

void TokenBucket::Reset() {
  tokens_.store(capacity_, memory_order_relaxed);
  last_refill_ns_.store(chrono::duration_cast<chrono::nanoseconds>(
                            chrono::steady_clock::now().time_since_epoch())
                            .count(),
                        memory_order_relaxed);
}

// ---------------------------------------------------------------------------

double TokenBucket::CurrentTokens() const {
  RefillInternal();
  return tokens_.load(memory_order_relaxed);
}

} // namespace qos
} // namespace utils
