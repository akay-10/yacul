#include "yacul/concurrency/spinlock.h"

#include <thread>

using namespace std;

namespace utils {
namespace concurrency {

// ---------------------------------------------------------------------------

void Spinlock::CpuRelax() {
#if defined(__x86_64__) || defined(__i386__)
  // PAUSE instruction: reduces power, avoids memory order violations on HT.
  __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
  // YIELD hint for ARM; falls back to a compiler barrier if unavailable.
  __asm__ volatile("yield" ::: "memory");
#else
  // Generic compiler barrier, prevents hoisting of the load out of the loop.
  atomic_thread_fence(memory_order_seq_cst);
#endif
}

// ---------------------------------------------------------------------------

void Spinlock::SpinWait() {
  uint32_t spin_count = 1;

  // Phase 1: Adaptive spin with exponential back-off.
  // Each iteration doubles the number of PAUSE hints emitted, capped at
  // kMaxSpinCount. This keeps the fast path extremely cheap while reducing
  // bus traffic as contention grows.
  while (spin_count <= kMaxSpinCount) {
    for (uint32_t i = 0; i < spin_count; ++i) {
      CpuRelax();
    }

    // Relaxed load: we only need to observe the flag change; the subsequent
    // exchange (in lock()) provides the necessary acquire barrier.
    if (!locked_.load(memory_order_relaxed)) {
      return; // Likely unlocked — let the caller retry the CAS.
    }

    spin_count <<= 1; // Exponential back-off.
  }

  // Phase 2: Yield — give up remaining quantum to avoid starving other threads
  // when the core count is low or contention is very high.
  uint32_t yield_iter = 0;
  while (locked_.load(memory_order_relaxed)) {
    if ((++yield_iter % kYieldInterval) == 0) {
      this_thread::sleep_for(chrono::nanoseconds(1));
    } else {
      this_thread::yield();
    }
  }
}

// ---------------------------------------------------------------------------

void Spinlock::lock() {
  // Fast path: single CAS; succeeds immediately in the uncontended case.
  // acquire semantics: all subsequent memory ops happen-after the lock.
  if (!locked_.exchange(true, memory_order_acquire)) {
    return;
  }

  // Slow path: spin until the lock appears free, then retry acquisition.
  do {
    SpinWait();
  } while (locked_.exchange(true, memory_order_acquire));
}

// ---------------------------------------------------------------------------

void Spinlock::unlock() {
  // release semantics: all prior memory ops happen-before the unlock.
  locked_.store(false, memory_order_release);
}

// ---------------------------------------------------------------------------

bool Spinlock::try_lock() {
  // acquire semantics on success; relaxed on failure (no ordering required
  // when we don't take the lock).
  return !locked_.load(memory_order_relaxed) &&
         !locked_.exchange(true, memory_order_acquire);
}

// ---------------------------------------------------------------------------

bool Spinlock::is_locked() const { return locked_.load(memory_order_relaxed); }

// ---------------------------------------------------------------------------

} // namespace concurrency
} // namespace utils
