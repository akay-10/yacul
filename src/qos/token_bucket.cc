#include "token_bucket.h"

#include <algorithm>
#include <chrono>

using namespace std;
using namespace utils::concurrency;

namespace utils {
namespace qos {

// ---------------------------------------------------------------------------

static int64_t NowNs() {
  return chrono::duration_cast<chrono::nanoseconds>(
           chrono::steady_clock::now().time_since_epoch())
    .count();
}

// ---------------------------------------------------------------------------

TokenBucket::TokenBucket(double rate, double capacity)
  : rate_(rate), capacity_(capacity), tokens_(capacity),
    last_refill_ns_(NowNs()) {}

// ---------------------------------------------------------------------------

void TokenBucket::RefillLocked() {
  int64_t now_ns = NowNs();

  double elapsed_s = static_cast<double>(now_ns - last_refill_ns_) * 1e-9;

  // Guard against clock anomalies (NTP steps, monotonic hiccups).
  if (elapsed_s <= 0.0)
    return;

  // Cap elapsed time to at most one full bucket to prevent a huge credit
  // accumulation when the bucket has been idle for a long time.
  elapsed_s = min(elapsed_s, capacity_ / rate_);

  tokens_ = min(tokens_ + elapsed_s * rate_, capacity_);
  last_refill_ns_ = now_ns;
}

// ---------------------------------------------------------------------------

bool TokenBucket::TryConsume(double tokens) {
  if (tokens <= 0.0)
    return true;

  lock_guard<Spinlock> lk(mu_);
  RefillLocked();

  if (tokens_ < tokens)
    return false;

  tokens_ -= tokens;
  return true;
}

// ---------------------------------------------------------------------------

void TokenBucket::Refill(double tokens) {
  if (tokens <= 0.0)
    return;

  lock_guard<Spinlock> lk(mu_);
  tokens_ = min(tokens_ + tokens, capacity_);
}

// ---------------------------------------------------------------------------

void TokenBucket::Reset() {
  lock_guard<Spinlock> lk(mu_);
  tokens_ = capacity_;
  last_refill_ns_ = NowNs();
}

// ---------------------------------------------------------------------------

double TokenBucket::CurrentTokens() {
  lock_guard<Spinlock> lk(mu_);
  RefillLocked();
  return tokens_;
}

// ---------------------------------------------------------------------------

} // namespace qos
} // namespace utils
