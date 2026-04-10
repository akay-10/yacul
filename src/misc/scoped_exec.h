#ifndef UTILS_MISC_SCOPED_EXEC_H
#define UTILS_MISC_SCOPED_EXEC_H

#include "basic/basic.h"

#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

namespace utils {
namespace misc {
/*
 * Concurrency policies
 *
 * Pass one of these as the template argument to ScopedExec<Policy> to select
 * the desired thread safety behaviour at compile time. There is zero runtime
 * overhead for UnsafePolicy — the mutex member is not present and every lock
 * call compiles away entirely via empty base optimisation and if constexpr.
 * All public methods on a single instance are safe to call concurrently from
 * multiple threads. The functor is guaranteed to execute at most once
 * regardless of how many threads race on invoke(), release(), reset(), or
 * the destructor simultaneously.
 */
struct ThreadSafePolicy {};

/*
 * No locking is performed. The instance must not be shared across threads
 * without external synchronisation. Offers maximum performance for
 * single threaded or already synchronised use cases.
 */
struct UnsafePolicy {};

// ---------------------------------------------------------------------------

namespace scoped_exec_internal {

// MutexBase<ThreadSafePolicy> owns a std::mutex.
// MutexBase<UnsafePolicy> is an empty struct; zero size overhead (EBO).
template <typename Policy> struct MutexBase;

template <> struct MutexBase<ThreadSafePolicy> {
  mutable std::mutex mu_;
};

template <> struct MutexBase<UnsafePolicy> {};

// NullLockGuard satisfies the LockGuard concept with a no-op body. Used by
// UnsafePolicy so that all locking call-sites in ScopedExec compile to nothing
// without requiring any source-level conditionals at those sites.
struct NullLockGuard {
  explicit NullLockGuard(const NullLockGuard &) = delete;
  explicit NullLockGuard() = default;
};

// LockGuard<Policy, T> resolves to the appropriate guard type.
template <typename Policy> struct LockGuardSelector;

template <> struct LockGuardSelector<ThreadSafePolicy> {
  using Type = std::lock_guard<std::mutex>;
};

template <> struct LockGuardSelector<UnsafePolicy> {
  using Type = NullLockGuard;
};

} // namespace scoped_exec_internal

// ---------------------------------------------------------------------------

/*
 * ScopedExec<Policy>
 *
 * A RAII guard that executes a registered functor exactly once when the object
 * is destroyed (or explicitly invoked).  The executor can be disarmed via
 * release() so the functor is never called.
 *
 * Select concurrency behaviour via the Policy template parameter:
 *
 *   ScopedExec<ThreadSafePolicy>  — fully thread-safe (see below)
 *   ScopedExec<UnsafePolicy>      — no locking; single-thread / external sync
 *
 * Or use the convenience aliases at the bottom of this file:
 *
 *   ScopedExecTS      — thread-safe variant
 *   ScopedExecUnsafe  — unsafe (no-lock) variant
 *
 * ---------------------------------------------------------------------------
 * ThreadSafePolicy guarantees
 * ---------------------------------------------------------------------------
 * All public methods are safe to call concurrently from multiple threads.
 * The functor is guaranteed to execute at most once regardless of races on
 * invoke(), release(), reset(), or the destructor.
 *
 * Functor execution is performed 'outside' the internal mutex to prevent
 * deadlock when the functor itself calls back into the same ScopedExec
 * instance (e.g. to re-arm it via reset()). As a consequence, operator bool()
 * will already return false before the functor has finished running — callers
 * must not use armed-state as a proxy for "functor has completed".
 *
 * Move construction locks only the source (this is not yet visible).
 * Move assignment uses std::lock() on both mutexes to prevent deadlock.
 *
 * ---------------------------------------------------------------------------
 * UnsafePolicy behaviour
 * ---------------------------------------------------------------------------
 * Identical API and semantics; all mutex operations compile to nothing.
 * The mutex member is absent (EBO), so sizeof(ScopedExec<UnsafePolicy>) is
 * strictly smaller than sizeof(ScopedExec<ThreadSafePolicy>).
 *
 * ---------------------------------------------------------------------------
 * Note on std::function
 * ---------------------------------------------------------------------------
 * std::function requires its stored target to be copy-constructible.
 * Lambdas capturing move-only types (e.g. std::unique_ptr) are incompatible.
 * Wrap such data in std::shared_ptr to retain shared ownership inside the
 * callable.
 *
 */
template <typename Policy = UnsafePolicy>
class ScopedExec : private scoped_exec_internal::MutexBase<Policy> {
  static_assert(std::is_same_v<Policy, ThreadSafePolicy> ||
                  std::is_same_v<Policy, UnsafePolicy>,
                "Policy must be ThreadSafePolicy or UnsafePolicy");

  using LockGuard =
    typename scoped_exec_internal::LockGuardSelector<Policy>::Type;

public:
  // Constructs a disarmed (no-op) executor.
  ScopedExec() = default;

  // Constructs an armed executor from any callable (lambda, function pointer,
  // std::function, functor). The executor is disarmed if fn is null/empty.
  template <typename Fn, typename = std::enable_if_t<
                           std::is_invocable_r_v<void, std::decay_t<Fn>>>>
  explicit ScopedExec(Fn &&fn)
    : fn_(std::forward<Fn>(fn)), armed_(fn_ != nullptr) {}

  // Move constructor: for ThreadSafePolicy, locks the source to guard against
  // concurrent access. For UnsafePolicy, performs a direct move.
  ScopedExec(ScopedExec &&other) { MoveConstruct(std::move(other)); }

  // Move assignment: for ThreadSafePolicy, locks both instances (deadlock-safe
  // via std::lock) and runs any currently armed functor on *this before taking
  // ownership. For UnsafePolicy, runs the current functor then moves directly.
  ScopedExec &operator=(ScopedExec &&other);

  // Deleted copy operations; ownership of the functor is unique.
  DISALLOW_COPY_AND_ASSIGN(ScopedExec);

  // Destructor: invokes the functor if still armed. Exceptions thrown by the
  // functor are suppressed to satisfy the no-throw destructor contract.  Use
  // invoke() when exception propagation is required.
  ~ScopedExec();

  // Disarm / re-arm

  // Disarms the executor without executing the functor. Returns the previously
  // stored functor (may be empty).
  std::function<void()> release();

  // Replaces the stored functor. If the executor is currently armed, the *old*
  // functor is executed (outside the lock for ThreadSafePolicy) before the
  // replacement is installed. Pass nullptr/empty to install a new functor
  // without firing the old one is not the intent here; use reset() for that.
  template <typename Fn, typename = std::enable_if_t<
                           std::is_invocable_r_v<void, std::decay_t<Fn>>>>
  void reset(Fn &&fn) {
    reset(std::function<void()>(std::forward<Fn>(fn)));
  }

  // Overload that accepts a pre-wrapped std::function (avoids double-wrap).
  void reset(std::function<void()> fn);

  // Runs the current functor (if armed) then leaves the executor disarmed.
  void reset();

  // Manual execution

  // Executes the functor immediately and disarms the executor. If the executor
  // is already disarmed this is a no-op. Exceptions thrown by the functor
  // propagate to the caller. For ThreadSafePolicy: safe to call concurrently;
  // only one thread fires.
  void invoke();

  // Observers

  // Returns true if the executor is currently armed. For ThreadSafePolicy: the
  // return value is a snapshot that may become stale immediately after this
  // call returns.
  explicit operator bool() const;

private:
  // Returns a LockGuard over mu_ (ThreadSafePolicy) or a NullLockGuard
  // (UnsafePolicy). Callers hold the returned guard for the critical section.
  LockGuard AcquireLock() const {
    if constexpr (std::is_same_v<Policy, ThreadSafePolicy>) {
      return LockGuard{this->mu_};
    } else {
      return LockGuard{};
    }
  }

  // Acquires the lock, clears (fn_, armed_), and returns the old functor.
  // Returns an empty function if the executor was not armed. The caller is
  // responsible for executing the returned functor.
  std::function<void()> ExtractIfArmed();

  // Implementation of move construction, templated on a dummy to share code.
  void MoveConstruct(ScopedExec &&other);

  std::function<void()> fn_;
  bool armed_ = false;
};

// Convenience aliases

// Thread-safe variant: all methods safe to call concurrently.
using ScopedExecTS = ScopedExec<ThreadSafePolicy>;

// Unsafe variant: no locking, for single-threaded or externally-synchronised
// use. Zero size and runtime overhead compared to ScopedExecTS.
using ScopedExecUnsafe = ScopedExec<UnsafePolicy>;

// ---------------------------------------------------------------------------

template <typename Policy> ScopedExec<Policy>::~ScopedExec() {
  try {
    auto fn = ExtractIfArmed();
    if (fn)
      fn();
  } catch (...) {
    // Suppress — destructors must not propagate.
  }
}

// ---------------------------------------------------------------------------

template <typename Policy>
void ScopedExec<Policy>::MoveConstruct(ScopedExec &&other) {
  if constexpr (std::is_same_v<Policy, ThreadSafePolicy>) {
    // Lock only the source; *this is not yet visible to other threads.
    [[maybe_unused]] auto lk = other.AcquireLock();
    fn_ = std::move(other.fn_);
    armed_ = other.armed_;
    other.armed_ = false;
  } else {
    fn_ = std::move(other.fn_);
    armed_ = other.armed_;
    other.armed_ = false;
  }
}

// ---------------------------------------------------------------------------

template <typename Policy>
ScopedExec<Policy> &ScopedExec<Policy>::operator=(ScopedExec &&other) {
  if (this == &other)
    return *this;

  if constexpr (std::is_same_v<Policy, ThreadSafePolicy>) {
    // Lock both mutexes deadlock-safely, extract *this's old functor, transfer
    // state, then run old functor after both locks are released.
    std::function<void()> old_fn;
    {
      std::unique_lock<std::mutex> lk_this(this->mu_, std::defer_lock);
      std::unique_lock<std::mutex> lk_other(other.mu_, std::defer_lock);
      std::lock(lk_this, lk_other);

      if (armed_) {
        armed_ = false;
        old_fn = std::move(fn_);
      }
      fn_ = std::move(other.fn_);
      armed_ = other.armed_;
      other.armed_ = false;
    }
    if (old_fn)
      old_fn();
  } else {
    // UnsafePolicy: run current functor (if any) then transfer directly.
    auto old_fn = ExtractIfArmed();
    fn_ = std::move(other.fn_);
    armed_ = other.armed_;
    other.armed_ = false;
    if (old_fn)
      old_fn();
  }

  return *this;
}

// ---------------------------------------------------------------------------

template <typename Policy> std::function<void()> ScopedExec<Policy>::release() {
  [[maybe_unused]] auto lk = AcquireLock();
  armed_ = false;
  return std::move(fn_);
}

// ---------------------------------------------------------------------------

template <typename Policy>
void ScopedExec<Policy>::reset(std::function<void()> fn) {
  std::function<void()> old_fn;
  {
    [[maybe_unused]] auto lk = AcquireLock();
    if (armed_) {
      armed_ = false;
      old_fn = std::move(fn_);
    }
    fn_ = std::move(fn);
    armed_ = (fn_ != nullptr);
  }
  if (old_fn)
    old_fn();
}

// ---------------------------------------------------------------------------

template <typename Policy> void ScopedExec<Policy>::reset() {
  std::function<void()> old_fn;
  {
    [[maybe_unused]] auto lk = AcquireLock();
    if (armed_) {
      armed_ = false;
      old_fn = std::move(fn_);
    }
    fn_ = nullptr;
  }
  if (old_fn)
    old_fn();
}

// ---------------------------------------------------------------------------

template <typename Policy> void ScopedExec<Policy>::invoke() {
  auto fn = ExtractIfArmed();
  if (fn)
    fn();
}

// ---------------------------------------------------------------------------

template <typename Policy> ScopedExec<Policy>::operator bool() const {
  [[maybe_unused]] auto lk = AcquireLock();
  return armed_;
}

// ---------------------------------------------------------------------------

template <typename Policy>
std::function<void()> ScopedExec<Policy>::ExtractIfArmed() {
  [[maybe_unused]] auto lk = AcquireLock();
  if (!armed_)
    return nullptr;
  armed_ = false;
  return std::move(fn_);
}

// ---------------------------------------------------------------------------

} // namespace misc
} // namespace utils

#endif // UTILS_MISC_SCOPED_EXEC_H
