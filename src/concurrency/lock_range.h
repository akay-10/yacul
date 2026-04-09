#ifndef UTILS_CONCURRENCY_LOCK_RANGE_H
#define UTILS_CONCURRENCY_LOCK_RANGE_H

#include "basic/basic.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace utils {
namespace concurrency {

/*
 * LockRange is a range locking facility.
 *
 * Allows multiple threads to lock arbitrary [offset, offset + length) ranges
 * with shared (read) or exclusive (write) semantics. Overlapping range
 * requests are detected and serialized; non-overlapping ranges proceed
 * concurrently.
 *
 * Design:
 *   - O(log n) overlap detection via a sorted map of active LockEntry objects.
 *   - Reader/writer semantics (shared vs. exclusive) per overlapping region.
 *   - Writer preference policy prevents writer starvation.
 *   - Blocking, non-blocking, and timed acquisition variants.
 *   - RAII guard (LockRange::Guard) for exception safe release.
 *
 * Thread-safe
 *
 * Usage example:
 * LockRange locker;
 *
 * // Exclusive lock, blocks until acquired.
 * auto g = locker.Lock(offset, length, LockRange::Mode::kExclusive);
 *
 * // Shared lock with a 100 ms timeout.
 * LockRange::Status s;
 * auto g = locker.TryLockFor(offset, length, LockRange::Mode::kShared,
 *                            std::chrono::milliseconds(100), &s);
 * if (!g.Valid()) {
 *   // timed out
 * }
 *
 * // Upgrade from shared to exclusive (may block).
 * g.Upgrade();
 *
 */

class LockRange {
public:
  // Lock acquisition mode.
  enum class Mode : uint8_t {
    kShared = 0,    // Multiple concurrent holders permitted.
    kExclusive = 1, // Sole holder; no concurrent shared or exclusive.
  };

  // Status codes returned by non-blocking / timed variants.
  enum class Status : uint8_t {
    kAcquired = 0,  // Lock was successfully taken.
    kTimeout = 1,   // Deadline elapsed before the lock could be acquired.
    kCancelled = 2, // Acquisition cancelled.
    kInvalid = 3,   // Invalid request.
  };

  // RAII wrapper that releases the locked range on destruction.
  class Guard {
  public:
    Guard() = default;
    ~Guard();

    // Move-only; non-copyable.
    DISALLOW_COPY_AND_ASSIGN(Guard);

    Guard(Guard &&other);
    Guard &operator=(Guard &&other);

    // True iff this guard holds a valid (acquired) lock.
    bool Valid() const { return owner_ != nullptr; }

    // Upgrade a kShared lock to kExclusive. Blocks indefinitely. Aborts if the
    // guard is not valid or already exclusive. After a failed upgrade the guard
    // is invalidated; the caller no longer holds any lock on the range.
    void Upgrade();

    // Timed upgrade variant.
    template <typename Rep, typename Period>
    Status UpgradeFor(std::chrono::duration<Rep, Period> timeout) {
      return UpgradeUntil(std::chrono::steady_clock::now() + timeout);
    }

    // Deadline based upgrade variant. Returns kAcquired on success, kTimeout on
    // deadline expiry. On kTimeout the guard is invalidated.
    Status UpgradeUntil(std::chrono::steady_clock::time_point deadline);

    // Explicit release before destruction. Idempotent.
    void Release();

    // Accessors, only meaningful when Valid() is true.
    uint64_t offset() const { return offset_; }
    uint64_t length() const { return length_; }
    Mode mode() const { return mode_; }

  private:
    // Allow friends to touch privates.
    friend class LockRange;

    Guard(LockRange *owner, uint64_t offset, uint64_t length, Mode mode)
      : owner_(owner), offset_(offset), length_(length), mode_(mode) {}

    LockRange *owner_ = nullptr;
    uint64_t offset_ = 0;
    uint64_t length_ = 0;
    Mode mode_ = Mode::kShared;
  };

  LockRange() = default;
  ~LockRange();

  // Non-copyable, non-movable (guards hold a raw pointer back to this).
  DISALLOW_COPY_AND_ASSIGN(LockRange);
  DISALLOW_MOVE_AND_ASSIGN(LockRange);

  // Blocking acquisition

  // Acquires [offset, offset + length) in the requested mode. Blocks
  // indefinitely until the lock can be granted.
  Guard Lock(uint64_t offset, uint64_t length, Mode mode);

  // Timed acquisition

  // Attempts acquisition for at most 'timeout'. The returned Guard may be
  // invalid when kTimeout; check Guard::Valid() or inspect *status_out.
  template <typename Rep, typename Period>
  Guard TryLockFor(uint64_t offset, uint64_t length, Mode mode,
                   std::chrono::duration<Rep, Period> timeout,
                   Status *status_out = nullptr) {
    return TryLockUntil(offset, length, mode,
                        std::chrono::steady_clock::now() + timeout, status_out);
  }

  Guard TryLockUntil(uint64_t offset, uint64_t length, Mode mode,
                     std::chrono::steady_clock::time_point deadline,
                     Status *status_out = nullptr);

  // Non-blocking acquisition

  // Returns immediately with an invalid guard if the range cannot be locked
  // without any blocking.
  Guard TryLock(uint64_t offset, uint64_t length, Mode mode,
                Status *status_out = nullptr);

  // Diagnostics

  // Returns the number of active lock entries.
  size_t ActiveLockCount() const;

  // Returns true if [offset, offset + length) overlaps any active lock entry.
  bool IsLockedAny(uint64_t offset, uint64_t length) const;

private:
  // A live lock entry stored in the sorted map.
  struct LockEntry {
    uint64_t offset = 0;
    // exclusive end = offset + length, i.e [offset, end) is the locked range.
    uint64_t end = 0;
    Mode mode = Mode::kShared;
    // Number of concurrent shared holders.
    uint32_t share_count = 1;
  };

  // A pending waiter descriptor. Stack-allocated by the waiting thread; a
  // pointer is stored in waiters_.
  struct Waiter {
    uint64_t offset;
    uint64_t end;
    Mode mode;
  };

  // Compound key so multiple entries can have the same offset.
  struct EntryKey {
    uint64_t offset;
    uint64_t id;

    bool operator<(const EntryKey &o) const {
      if (offset != o.offset)
        return offset < o.offset;
      return id < o.id;
    }
  };

  // True if [a_off, a_end) and [b_off, b_end) overlap.
  static bool Overlaps(uint64_t a_off, uint64_t a_end, uint64_t b_off,
                       uint64_t b_end);

  // Release the specified range and wake waiters.
  void DoRelease(uint64_t offset, uint64_t length, Mode mode);

  // Internal helpers (all require mu_ to be held by the caller)

  // True if granting [offset, end) in 'mode' conflicts with any existing
  // entry (optionally excluding the entry at 'exclude_key').
  bool HasConflict(uint64_t offset, uint64_t end, Mode mode,
                   const EntryKey *exclude_key = nullptr) const;

  // True if any exclusive waiter is queued for an overlapping range.
  bool ExclusiveWaiterExists(uint64_t offset, uint64_t end) const;

  // True if the range can be granted immediately. This is will happen if the
  // request has no conflict and there is no block due to writer priority.
  bool CanGrant(uint64_t offset, uint64_t end, Mode mode,
                const EntryKey *exclude_key = nullptr) const;

  // Insert a new entry and return its key.
  EntryKey InsertEntry(uint64_t offset, uint64_t end, Mode mode);

  // Remove the entry identified by 'key' and broadcast to all waiters.
  void RemoveEntry(const EntryKey &key);

  // Core wait loop. 'exclude_key' is forwarded to CanGrant to allow the upgrade
  // path to ignore the caller's own existing entry.
  Status WaitLoop(std::unique_lock<std::mutex> &lk, uint64_t offset,
                  uint64_t end, Mode mode,
                  std::chrono::steady_clock::time_point deadline, bool no_wait,
                  const EntryKey *exclude_key = nullptr);

  mutable std::mutex mu_;
  std::condition_variable cv_;

  uint64_t next_id_ = 0;
  std::map<EntryKey, LockEntry> entries_;

  // Pointers to stack-allocated Waiter descriptors of all blocked threads.
  std::vector<Waiter *> waiters_;
};

} // namespace concurrency
} // namespace utils

#endif // UTILS_CONCURRENCY_LOCK_RANGE_H
