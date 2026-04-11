#ifndef UTILS_CONCURRENCY_SPINLOCK_H
#define UTILS_CONCURRENCY_SPINLOCK_H

#include "yacul/basic/basic.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace utils {
namespace concurrency {

/*
 * Spinlock implements a lightweight, non-recursive mutual exclusion primitive
 * using atomic test-and-set with exponential backoff. It satisfies the
 * BasicLockable and Lockable named requirements, making it compatible with
 * std::lock_guard, std::unique_lock, std::scoped_lock, and std::lock().
 *
 * Usage:
 * Spinlock mu;
 * {
 *   std::lock_guard<Spinlock> lk(mu);
 *   // critical section
 * }
 *
 * Thread-safe
 *
 * Performance characteristics:
 *   - Acquire (uncontended): Single CAS.
 *   - Acquire (contended)  : Exponential backoff with yield fallback.
 *   - sizeof(Spinlock)     : 1 byte (cache-line padding NOT included by
 *                            default; align externally when false sharing is
 *                            a concern).
 *
 * Limitations:
 *   - Not recursive / reentrant.
 *   - Not fair (no FIFO ordering guarantee).
 *   - Spinning wastes CPU; prefer std::mutex when critical sections are long
 *     (> ~1 us) or when the number of threads greatly exceeds available cores.
 */

class Spinlock {
public:
  // Maximum number of PAUSE / NOP spin iterations before yielding the thread.
  static constexpr uint32_t kMaxSpinCount = 1024;

  // Number of iterations between each yield call in the yield phase.
  static constexpr uint32_t kYieldInterval = 64;

  Spinlock() = default;
  ~Spinlock() = default;

  // Acquires the lock. Blocks the calling thread using an adaptive spin-then-
  // yield strategy until the lock becomes available.
  void lock();

  // Releases the lock. The calling thread must own the lock.
  void unlock();

  // Attempts to acquire the lock without blocking. Returns true if the lock was
  // acquired, false otherwise.
  bool try_lock();

  // Returns true if the lock is currently held by any thread. This is a
  // best-effort, use only for diagnostics.
  bool is_locked() const;

private:
  // Non-copyable, non-movable (matches std::mutex semantics).
  DISALLOW_COPY_AND_ASSIGN(Spinlock);
  DISALLOW_MOVE_AND_ASSIGN(Spinlock);

  // Emits a CPU-level "pause" hint (PAUSE on x86, YIELD on ARM) to reduce
  // power consumption and improve performance under hyper-threading.
  static void CpuRelax();

  // Internal spin loop body: spins with exponential backoff up to
  // kMaxSpinCount iterations, then falls back to std::this_thread::yield().
  void SpinWait();

  // The lock state. Aligned to a cache line is NOT done here intentionally,
  // callers that need to avoid false sharing should align the enclosing
  // struct / class.
  std::atomic<bool> locked_{false};
};

// Convenience Aliases

// Non-moveable RAII guard.
using SpinGuard = std::lock_guard<Spinlock>;

// Movable RAII guard.
using UniqueSpin = std::unique_lock<Spinlock>;

} // namespace concurrency
} // namespace utils

#endif // UTILS_CONCURRENCY_SPINLOCK_H
