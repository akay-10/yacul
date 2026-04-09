#ifndef UTILS_CONCURRENCY_LOCK_ID_H
#define UTILS_CONCURRENCY_LOCK_ID_H

#include "basic/basic.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace utils {
namespace concurrency {

/*
 * LockId provides locking for shared entities identified by arbitrary
 * identifiers. It supports exclusive and shared (read / write) semantics,
 * try lock with timeout, sharded internal bookkeeping for high concurrency,
 * and RAII guard types that mirror the std::unique_lock / std::shared_lock
 * interface.
 *
 * Usage example:
 *
 * LockId<int> locker;
 *
 * // Exclusive lock (write):
 * {
 *   auto guard = locker.Lock(42);
 *   // ... mutate entity 42 ...
 * } // released on scope exit
 *
 * // Shared lock (read):
 * {
 *   auto guard = locker.LockShared(42);
 *   // ... read entity 42 ...
 * }
 *
 * // Try-lock with timeout:
 * auto guard = locker.TryLockFor(42, std::chrono::milliseconds(100));
 * if (guard) {
 *   // acquired
 * }
 *
 * Thread-safe
 *
 * Template parameters:
 * IdType – The key type. Must be copyable and hashable.
 * Hash – Hash functor (defaults to std::hash<IdType>).
 *
 * Sharding: The internal map of LockEntry objects is split across
 * kDefaultShards independent shards, each guarded by its own std::mutex.
 * This allows up to kDefaultShards concurrent map operations before any
 * contention occurs. The shard for a given ID is determined by hashing.
 */

// ---------------------------------------------------------------------------

// Forward declarations
template <typename IdType, typename Hash = std::hash<IdType>> class LockId;

// ---------------------------------------------------------------------------

namespace lock_id_internal {

// Holds a shared_mutex and a reference count. The reference count tracks how
// many callers currently hold (or are waiting for) a lock on this ID so that
// the entry can be safely removed from the map once no one needs it.
struct LockEntry {
  std::shared_timed_mutex mutex;
  // Number of active holders or waiters. Managed by LockId under the shard
  // guard, so no extra atomic is required here.
  std::size_t ref_count{0};

  LockEntry() = default;

  // Non-copyable, non-movable.
  DISALLOW_COPY_AND_ASSIGN(LockEntry);
  DISALLOW_MOVE_AND_ASSIGN(LockEntry);
};

} // namespace lock_id_internal

// ---------------------------------------------------------------------------

// Returned by LockId::Lock().
template <typename IdType, typename Hash> class LockIdGuard {
public:
  LockIdGuard(LockId<IdType, Hash> *owner, IdType id,
              std::unique_lock<std::shared_timed_mutex> lock)
    : owner_(owner), id_(std::move(id)), lock_(std::move(lock)) {}

  // Empty state denoting unlocked (e.g. after a failed TryLock).
  LockIdGuard() = default;

  // Releases the per ID lock on destruction.
  ~LockIdGuard() { Release(); }

  LockIdGuard(LockIdGuard &&other)
    : owner_(other.owner_), id_(std::move(other.id_)),
      lock_(std::move(other.lock_)) {
    other.owner_ = nullptr;
  }

  LockIdGuard &operator=(LockIdGuard &&other) {
    if (this != &other) {
      Release();
      owner_ = other.owner_;
      id_ = std::move(other.id_);
      lock_ = std::move(other.lock_);
      other.owner_ = nullptr;
    }
    return *this;
  }

  // Non-copyable; movable.
  DISALLOW_COPY_AND_ASSIGN(LockIdGuard);

  // Returns true if this guard owns a lock.
  explicit operator bool() const { return lock_.owns_lock(); }

  // Manually release the lock before the guard goes out of scope.
  void Release();

  // Expose the locked ID.
  const IdType &id() const { return id_; }

private:
  LockId<IdType, Hash> *owner_{nullptr};
  IdType id_{};
  std::unique_lock<std::shared_timed_mutex> lock_;
};

// ---------------------------------------------------------------------------

// Returned by LockId::LockShared().
template <typename IdType, typename Hash> class LockIdSharedGuard {
public:
  LockIdSharedGuard(LockId<IdType, Hash> *owner, IdType id,
                    std::shared_lock<std::shared_timed_mutex> lock)
    : owner_(owner), id_(std::move(id)), lock_(std::move(lock)) {}

  // Empty state denoting unlocked (e.g. after a failed TryLock).
  LockIdSharedGuard() = default;

  // Releases the per ID lock on destruction.
  ~LockIdSharedGuard() { Release(); }

  LockIdSharedGuard(LockIdSharedGuard &&other)
    : owner_(other.owner_), id_(std::move(other.id_)),
      lock_(std::move(other.lock_)) {
    other.owner_ = nullptr;
  }

  LockIdSharedGuard &operator=(LockIdSharedGuard &&other) {
    if (this != &other) {
      Release();
      owner_ = other.owner_;
      id_ = std::move(other.id_);
      lock_ = std::move(other.lock_);
      other.owner_ = nullptr;
    }
    return *this;
  }

  // Non-copyable; movable.
  DISALLOW_COPY_AND_ASSIGN(LockIdSharedGuard);

  // Returns true if this guard owns a lock.
  explicit operator bool() const { return lock_.owns_lock(); }

  // Manually release the lock before the guard goes out of scope.
  void Release();

  // Expose the locked ID.
  const IdType &id() const { return id_; }

private:
  LockId<IdType, Hash> *owner_{nullptr};
  IdType id_{};
  std::shared_lock<std::shared_timed_mutex> lock_;
};

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash> class LockId {
public:
  // Number of shards for the entry map. Must be a power of two.
  static constexpr std::size_t kDefaultShards = 64;

  explicit LockId(std::size_t num_shards = kDefaultShards);

  ~LockId() = default;

  // Non-copyable, non-movable (entries hold raw back-pointers).
  DISALLOW_COPY_AND_ASSIGN(LockId);
  DISALLOW_MOVE_AND_ASSIGN(LockId);

  // Exclusive (write) locking

  // Blocks until an exclusive lock for 'id' is acquired.
  LockIdGuard<IdType, Hash> Lock(const IdType &id);
  LockIdGuard<IdType, Hash> Lock(IdType &&id);

  // Attempts to acquire an exclusive lock without blocking. Returns an empty
  // (falsy) guard on failure.
  LockIdGuard<IdType, Hash> TryLock(const IdType &id);
  LockIdGuard<IdType, Hash> TryLock(IdType &&id);

  // Attempts to acquire an exclusive lock, waiting up to 'duration'. Returns an
  // empty (falsy) guard on timeout.
  template <typename Rep, typename Period>
  LockIdGuard<IdType, Hash>
  TryLockFor(const IdType &id,
             const std::chrono::duration<Rep, Period> &duration);
  template <typename Rep, typename Period>
  LockIdGuard<IdType, Hash>
  TryLockFor(IdType &&id, const std::chrono::duration<Rep, Period> &duration);

  // Shared (read) locking

  // Blocks until a shared lock for 'id' is acquired.
  LockIdSharedGuard<IdType, Hash> LockShared(const IdType &id);
  LockIdSharedGuard<IdType, Hash> LockShared(IdType &&id);

  // Attempts to acquire a shared lock without blocking.
  LockIdSharedGuard<IdType, Hash> TryLockShared(const IdType &id);
  LockIdSharedGuard<IdType, Hash> TryLockShared(IdType &&id);

  // Attempts to acquire a shared lock, waiting up to 'duration'.
  template <typename Rep, typename Period>
  LockIdSharedGuard<IdType, Hash>
  TryLockSharedFor(const IdType &id,
                   const std::chrono::duration<Rep, Period> &duration);
  template <typename Rep, typename Period>
  LockIdSharedGuard<IdType, Hash>
  TryLockSharedFor(IdType &&id,
                   const std::chrono::duration<Rep, Period> &duration);

  // Diagnostics

  // Returns the number of IDs that currently have at least one active holder
  // or waiter across all shards.
  std::size_t ActiveEntryCount() const;

  // Returns the number of shards configured for this instance.
  std::size_t NumShards() const { return num_shards_; }

private:
  // Allow friends to touch the privates.
  friend class LockIdGuard<IdType, Hash>;
  friend class LockIdSharedGuard<IdType, Hash>;

  // Internal helpers

  // Computes the shard index for a given ID.
  std::size_t ShardIndex(const IdType &id) const;

  // Acquires a reference to the entry for 'id', creating it if necessary.
  // The caller must release the reference via ReleaseEntry() or implicitly
  // through a guard destruction.
  lock_id_internal::LockEntry *AcquireEntry(const IdType &id);

  // Decrements the ref count of the entry for 'id' and erases it if zero.
  void ReleaseEntry(const IdType &id);

  // Called by LockIdGuard destructor.
  void ReleaseExclusive(const IdType &id,
                        std::unique_lock<std::shared_timed_mutex> &lock);

  // Called by LockIdSharedGuard destructor.
  void ReleaseShared(const IdType &id,
                     std::shared_lock<std::shared_timed_mutex> &lock);

  // Shard bookkeeping.
  using EntryMap =
    std::unordered_map<IdType, std::unique_ptr<lock_id_internal::LockEntry>,
                       Hash>;

  struct Shard {
    mutable std::mutex map_mutex;
    EntryMap entries;

    // Padding to avoid false sharing between shards.
    char padding[64 - sizeof(std::mutex) > 0 ? 64 - sizeof(std::mutex) : 1];
  };

  const std::size_t num_shards_;
  std::unique_ptr<Shard[]> shards_;
  Hash hasher_;
};

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
void LockIdGuard<IdType, Hash>::Release() {

  if (owner_ && lock_.owns_lock()) {
    owner_->ReleaseExclusive(id_, lock_);
    owner_ = nullptr;
  }
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
void LockIdSharedGuard<IdType, Hash>::Release() {

  if (owner_ && lock_.owns_lock()) {
    owner_->ReleaseShared(id_, lock_);
    owner_ = nullptr;
  }
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockId<IdType, Hash>::LockId(std::size_t num_shards)
  : num_shards_(num_shards > 0 ? num_shards : kDefaultShards),
    shards_(std::make_unique<Shard[]>(num_shards > 0 ? num_shards
                                                     : kDefaultShards)) {}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
std::size_t LockId<IdType, Hash>::ShardIndex(const IdType &id) const {
  // Note: num_shards_ is always a power of two, so we can use bitwise AND for
  // modulo instead of the more expensive remainder operator.
  return hasher_(id) & (num_shards_ - 1);
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
lock_id_internal::LockEntry *
LockId<IdType, Hash>::AcquireEntry(const IdType &id) {

  Shard &shard = shards_[ShardIndex(id)];
  std::lock_guard<std::mutex> shard_guard(shard.map_mutex);

  auto &entry_ptr = shard.entries[id];
  if (!entry_ptr) {
    entry_ptr = std::make_unique<lock_id_internal::LockEntry>();
  }
  ++entry_ptr->ref_count;
  return entry_ptr.get();
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
void LockId<IdType, Hash>::ReleaseEntry(const IdType &id) {

  Shard &shard = shards_[ShardIndex(id)];
  std::lock_guard<std::mutex> shard_guard(shard.map_mutex);

  auto it = shard.entries.find(id);
  if (it != shard.entries.end()) {
    if (--it->second->ref_count == 0) {
      shard.entries.erase(it);
    }
  }
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
void LockId<IdType, Hash>::ReleaseExclusive(
  const IdType &id, std::unique_lock<std::shared_timed_mutex> &lock) {

  lock.unlock();
  ReleaseEntry(id);
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
void LockId<IdType, Hash>::ReleaseShared(
  const IdType &id, std::shared_lock<std::shared_timed_mutex> &lock) {

  lock.unlock();
  ReleaseEntry(id);
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdGuard<IdType, Hash> LockId<IdType, Hash>::Lock(const IdType &id) {

  lock_id_internal::LockEntry *entry = AcquireEntry(id);
  std::unique_lock<std::shared_timed_mutex> lk(entry->mutex);
  return LockIdGuard<IdType, Hash>(this, id, std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdGuard<IdType, Hash> LockId<IdType, Hash>::Lock(IdType &&id) {

  IdType id_copy = id;
  lock_id_internal::LockEntry *entry = AcquireEntry(id_copy);
  std::unique_lock<std::shared_timed_mutex> lk(entry->mutex);
  return LockIdGuard<IdType, Hash>(this, std::move(id_copy), std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdGuard<IdType, Hash> LockId<IdType, Hash>::TryLock(const IdType &id) {

  lock_id_internal::LockEntry *entry = AcquireEntry(id);
  std::unique_lock<std::shared_timed_mutex> lk(entry->mutex, std::try_to_lock);
  if (!lk.owns_lock()) {
    ReleaseEntry(id);
    return LockIdGuard<IdType, Hash>{};
  }
  return LockIdGuard<IdType, Hash>(this, id, std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdGuard<IdType, Hash> LockId<IdType, Hash>::TryLock(IdType &&id) {

  IdType id_copy = id;
  lock_id_internal::LockEntry *entry = AcquireEntry(id_copy);
  std::unique_lock<std::shared_timed_mutex> lk(entry->mutex, std::try_to_lock);
  if (!lk.owns_lock()) {
    ReleaseEntry(id_copy);
    return LockIdGuard<IdType, Hash>{};
  }
  return LockIdGuard<IdType, Hash>(this, std::move(id_copy), std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
template <typename Rep, typename Period>
LockIdGuard<IdType, Hash> LockId<IdType, Hash>::TryLockFor(
  const IdType &id, const std::chrono::duration<Rep, Period> &duration) {

  lock_id_internal::LockEntry *entry = AcquireEntry(id);
  auto deadline = std::chrono::steady_clock::now() + duration;
  std::unique_lock<std::shared_timed_mutex> lk(entry->mutex, std::defer_lock);
  if (!lk.mutex()->try_lock_until(deadline)) {
    ReleaseEntry(id);
    return LockIdGuard<IdType, Hash>{};
  }
  lk.release(); // ownership transferred; reconstruct an owning unique_lock
  std::unique_lock<std::shared_timed_mutex> owned(entry->mutex,
                                                  std::adopt_lock);
  return LockIdGuard<IdType, Hash>(this, id, std::move(owned));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
template <typename Rep, typename Period>
LockIdGuard<IdType, Hash> LockId<IdType, Hash>::TryLockFor(
  IdType &&id, const std::chrono::duration<Rep, Period> &duration) {

  IdType id_copy = id;
  lock_id_internal::LockEntry *entry = AcquireEntry(id_copy);
  auto deadline = std::chrono::steady_clock::now() + duration;
  std::unique_lock<std::shared_timed_mutex> lk(entry->mutex, std::defer_lock);
  if (!lk.mutex()->try_lock_until(deadline)) {
    ReleaseEntry(id_copy);
    return LockIdGuard<IdType, Hash>{};
  }
  lk.release();
  std::unique_lock<std::shared_timed_mutex> owned(entry->mutex,
                                                  std::adopt_lock);
  return LockIdGuard<IdType, Hash>(this, std::move(id_copy), std::move(owned));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdSharedGuard<IdType, Hash>
LockId<IdType, Hash>::LockShared(const IdType &id) {

  lock_id_internal::LockEntry *entry = AcquireEntry(id);
  std::shared_lock<std::shared_timed_mutex> lk(entry->mutex);
  return LockIdSharedGuard<IdType, Hash>(this, id, std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdSharedGuard<IdType, Hash> LockId<IdType, Hash>::LockShared(IdType &&id) {

  IdType id_copy = id;
  lock_id_internal::LockEntry *entry = AcquireEntry(id_copy);
  std::shared_lock<std::shared_timed_mutex> lk(entry->mutex);
  return LockIdSharedGuard<IdType, Hash>(this, std::move(id_copy),
                                         std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdSharedGuard<IdType, Hash>
LockId<IdType, Hash>::TryLockShared(const IdType &id) {

  lock_id_internal::LockEntry *entry = AcquireEntry(id);
  std::shared_lock<std::shared_timed_mutex> lk(entry->mutex, std::try_to_lock);
  if (!lk.owns_lock()) {
    ReleaseEntry(id);
    return LockIdSharedGuard<IdType, Hash>{};
  }
  return LockIdSharedGuard<IdType, Hash>(this, id, std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
LockIdSharedGuard<IdType, Hash>
LockId<IdType, Hash>::TryLockShared(IdType &&id) {

  IdType id_copy = id;
  lock_id_internal::LockEntry *entry = AcquireEntry(id_copy);
  std::shared_lock<std::shared_timed_mutex> lk(entry->mutex, std::try_to_lock);
  if (!lk.owns_lock()) {
    ReleaseEntry(id_copy);
    return LockIdSharedGuard<IdType, Hash>{};
  }
  return LockIdSharedGuard<IdType, Hash>(this, std::move(id_copy),
                                         std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
template <typename Rep, typename Period>
LockIdSharedGuard<IdType, Hash> LockId<IdType, Hash>::TryLockSharedFor(
  const IdType &id, const std::chrono::duration<Rep, Period> &duration) {

  lock_id_internal::LockEntry *entry = AcquireEntry(id);
  auto deadline = std::chrono::steady_clock::now() + duration;
  if (!entry->mutex.try_lock_shared_until(deadline)) {
    ReleaseEntry(id);
    return LockIdSharedGuard<IdType, Hash>{};
  }
  std::shared_lock<std::shared_timed_mutex> lk(entry->mutex, std::adopt_lock);
  return LockIdSharedGuard<IdType, Hash>(this, id, std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
template <typename Rep, typename Period>
LockIdSharedGuard<IdType, Hash> LockId<IdType, Hash>::TryLockSharedFor(
  IdType &&id, const std::chrono::duration<Rep, Period> &duration) {

  IdType id_copy = id;
  lock_id_internal::LockEntry *entry = AcquireEntry(id_copy);
  auto deadline = std::chrono::steady_clock::now() + duration;
  if (!entry->mutex.try_lock_shared_until(deadline)) {
    ReleaseEntry(id_copy);
    return LockIdSharedGuard<IdType, Hash>{};
  }
  std::shared_lock<std::shared_timed_mutex> lk(entry->mutex, std::adopt_lock);
  return LockIdSharedGuard<IdType, Hash>(this, std::move(id_copy),
                                         std::move(lk));
}

// ---------------------------------------------------------------------------

template <typename IdType, typename Hash>
std::size_t LockId<IdType, Hash>::ActiveEntryCount() const {

  std::size_t total = 0;
  for (std::size_t i = 0; i < num_shards_; ++i) {
    std::lock_guard<std::mutex> shard_guard(shards_[i].map_mutex);
    total += shards_[i].entries.size();
  }
  return total;
}

// ---------------------------------------------------------------------------

} // namespace concurrency
} // namespace utils

#endif // UTILS_CONCURRENCY_LOCK_ID_H
