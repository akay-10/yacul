#include "lock_range.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

using namespace std;

namespace utils {
namespace concurrency {
// ===========================================================================
// Anonymous-namespace helpers
// ===========================================================================

namespace {

// A sentinel representing "no deadline" (block indefinitely).
constexpr chrono::steady_clock::time_point kForever =
    chrono::steady_clock::time_point::max();

// Validate that offset + length does not overflow uint64_t and that length > 0.
void ValidateRange(uint64_t offset, uint64_t length, const char *caller) {
  if (length == 0) {
    throw invalid_argument(string(caller) + ": length must be > 0");
  }
  if (offset > numeric_limits<uint64_t>::max() - length) {
    throw overflow_error(string(caller) +
                         ": offset + length overflows uint64_t");
  }
}

} // namespace

// ===========================================================================
// LockRange::Guard
// ===========================================================================

LockRange::Guard::~Guard() { Release(); }

//------------------------------------------------------------------------

LockRange::Guard::Guard(Guard &&other)
    : owner_(other.owner_), offset_(other.offset_), length_(other.length_),
      mode_(other.mode_) {
  other.owner_ = nullptr;
}

//------------------------------------------------------------------------

LockRange::Guard &LockRange::Guard::operator=(Guard &&other) {
  if (this != &other) {
    Release();
    owner_ = other.owner_;
    offset_ = other.offset_;
    length_ = other.length_;
    mode_ = other.mode_;
    other.owner_ = nullptr;
  }
  return *this;
}

//------------------------------------------------------------------------

void LockRange::Guard::Release() {
  if (owner_ == nullptr)
    return;
  LockRange *owner = owner_;
  owner_ = nullptr; // clear first so ~Guard() re-entrance is harmless
  owner->DoRelease(offset_, length_, mode_);
}

//------------------------------------------------------------------------

void LockRange::Guard::Upgrade() {
  Status s = UpgradeUntil(kForever);
  (void)s; // kForever guarantees kAcquired (or throws on logic errors).
}

//------------------------------------------------------------------------

LockRange::Status
LockRange::Guard::UpgradeUntil(chrono::steady_clock::time_point deadline) {
  if (owner_ == nullptr) {
    throw logic_error("LockRange::Guard::UpgradeUntil: guard is not valid");
  }
  if (mode_ == Mode::kExclusive) {
    throw logic_error(
        "LockRange::Guard::UpgradeUntil: guard is already exclusive");
  }

  // Strategy:
  //   1. While holding mu_, locate our shared entry and decrement its
  //      share_count (removing it entirely if this is the last holder).
  //   2. Register as an exclusive waiter and wait until no conflicting
  //      entries remain (WaitLoop).
  //   3. On success, insert the exclusive entry and update mode_.
  //   4. On timeout the guard is invalidated (no lock held).

  LockRange *owner = owner_;
  uint64_t off = offset_;
  uint64_t end = off + length_;

  unique_lock<mutex> lk(owner->mu_);

  // Step 1: release (or decrement) our current shared entry.
  // We need to find the specific entry that corresponds to this guard.
  // Since multiple threads can hold a shared lock on the same range, we
  // identify "our" entry by matching (offset, end, mode==kShared) and
  // decrementing exactly once.
  bool released = false;
  for (auto it = owner->entries_.begin(); it != owner->entries_.end(); ++it) {
    const EntryKey &key = it->first;
    LockEntry &e = it->second;

    if (key.offset > off)
      break; // sorted; no match possible beyond this
    if (e.offset != off || e.end != end || e.mode != Mode::kShared)
      continue;

    if (e.share_count > 1) {
      --e.share_count;
    } else {
      owner->entries_.erase(it);
      owner->cv_.notify_all();
    }
    released = true;
    break;
  }

  if (!released) {
    // Entry not found.  The guard is already in an inconsistent state.
    owner_ = nullptr;
    throw logic_error(
        "LockRange::Guard::UpgradeUntil: could not find own lock entry");
  }

  // Step 2–3: wait for exclusive acquisition.
  Status s = owner->WaitLoop(lk, off, end, Mode::kExclusive, deadline,
                             /*no_wait=*/false, /*exclude_key=*/nullptr);

  lk.unlock();

  if (s == Status::kAcquired) {
    mode_ = Mode::kExclusive;
  } else {
    // Timeout: guard is now invalid (we released the shared lock but could
    // not re-acquire as exclusive).
    owner_ = nullptr;
  }
  return s;
}

// ===========================================================================
// LockRange — constructor / destructor
// ===========================================================================

LockRange::LockRange() = default;

//------------------------------------------------------------------------

LockRange::~LockRange() {
  // All guards must have been destroyed before the LockRange itself.
  assert(entries_.empty() && "LockRange destroyed while locks are still held");
}

// ===========================================================================
// LockRange — public acquisition API
// ===========================================================================

LockRange::Guard LockRange::Lock(uint64_t offset, uint64_t length, Mode mode) {
  ValidateRange(offset, length, "LockRange::Lock");

  unique_lock<mutex> lk(mu_);
  Status s = WaitLoop(lk, offset, offset + length, mode, kForever,
                      /*no_wait=*/false);
  (void)s; // kForever always yields kAcquired.
  return Guard(this, offset, length, mode);
}

//------------------------------------------------------------------------

LockRange::Guard
LockRange::TryLockUntil(uint64_t offset, uint64_t length, Mode mode,
                        chrono::steady_clock::time_point deadline,
                        Status *status_out) {
  ValidateRange(offset, length, "LockRange::TryLockUntil");

  unique_lock<mutex> lk(mu_);
  Status s = WaitLoop(lk, offset, offset + length, mode, deadline,
                      /*no_wait=*/false);

  if (status_out)
    *status_out = s;

  if (s == Status::kAcquired)
    return Guard(this, offset, length, mode);
  return Guard{};
}

//------------------------------------------------------------------------

LockRange::Guard LockRange::TryLock(uint64_t offset, uint64_t length, Mode mode,
                                    Status *status_out) {
  ValidateRange(offset, length, "LockRange::TryLock");

  unique_lock<mutex> lk(mu_);
  Status s = WaitLoop(lk, offset, offset + length, mode,
                      /*deadline=*/chrono::steady_clock::time_point{},
                      /*no_wait=*/true);

  if (status_out)
    *status_out = s;

  if (s == Status::kAcquired)
    return Guard(this, offset, length, mode);
  return Guard{};
}

// ===========================================================================
// LockRange — diagnostic API
// ===========================================================================

size_t LockRange::ActiveLockCount() const {
  unique_lock<mutex> lk(mu_);
  return entries_.size();
}

//------------------------------------------------------------------------

bool LockRange::IsLockedAny(uint64_t offset, uint64_t length) const {
  if (length == 0)
    return false;
  uint64_t end = offset + length;

  unique_lock<mutex> lk(mu_);
  for (const auto &[key, entry] : entries_) {
    // Entries are sorted by offset; once key.offset >= end there can be
    // no further overlap.
    if (key.offset >= end)
      break;
    if (Overlaps(entry.offset, entry.end, offset, end))
      return true;
  }
  return false;
}

// ===========================================================================
// LockRange — private helpers
// ===========================================================================

/* static */
bool LockRange::Overlaps(uint64_t a_off, uint64_t a_end, uint64_t b_off,
                         uint64_t b_end) {
  return a_off < b_end && b_off < a_end;
}

//------------------------------------------------------------------------

bool LockRange::HasConflict(uint64_t offset, uint64_t end, Mode mode,
                            const EntryKey *exclude_key) const {
  // Scan entries that could overlap [offset, end).
  // Stop as soon as the entry's starting offset is at or beyond `end`.
  for (const auto &[key, entry] : entries_) {
    if (key.offset >= end)
      break;
    if (exclude_key && key.offset == exclude_key->offset &&
        key.id == exclude_key->id) {
      continue;
    }
    if (!Overlaps(entry.offset, entry.end, offset, end))
      continue;

    // Conflict matrix:
    //   request\existing  Shared    Exclusive
    //   Shared            no        YES
    //   Exclusive         YES       YES
    if (mode == Mode::kExclusive || entry.mode == Mode::kExclusive) {
      return true;
    }
  }
  return false;
}

//------------------------------------------------------------------------

bool LockRange::ExclusiveWaiterExists(uint64_t offset, uint64_t end) const {
  for (const Waiter *w : waiters_) {
    if (w->mode == Mode::kExclusive &&
        Overlaps(w->offset, w->end, offset, end)) {
      return true;
    }
  }
  return false;
}

//------------------------------------------------------------------------

bool LockRange::CanGrant(uint64_t offset, uint64_t end, Mode mode,
                         const EntryKey *exclude_key) const {
  // Writer-preference: if we are a shared-lock request and an exclusive
  // waiter is already queued for an overlapping range, we yield to avoid
  // starving writers.
  if (mode == Mode::kShared && ExclusiveWaiterExists(offset, end)) {
    return false;
  }
  return !HasConflict(offset, end, mode, exclude_key);
}

//------------------------------------------------------------------------

LockRange::EntryKey LockRange::InsertEntry(uint64_t offset, uint64_t end,
                                           Mode mode) {
  EntryKey key{offset, next_id_++};
  LockEntry &e = entries_[key];
  e.offset = offset;
  e.end = end;
  e.mode = mode;
  e.share_count = 1;
  return key;
}

//------------------------------------------------------------------------

void LockRange::RemoveEntry(const EntryKey &key) {
  entries_.erase(key);
  cv_.notify_all();
}

//------------------------------------------------------------------------

LockRange::Status LockRange::WaitLoop(unique_lock<mutex> &lk, uint64_t offset,
                                      uint64_t end, Mode mode,
                                      chrono::steady_clock::time_point deadline,
                                      bool no_wait,
                                      const EntryKey *exclude_key) {
  // Caller must hold `lk` on entry; `lk` is still held on return.

  if (CanGrant(offset, end, mode, exclude_key)) {
    InsertEntry(offset, end, mode);
    return Status::kAcquired;
  }

  if (no_wait) {
    return Status::kTimeout;
  }

  // Register a waiter descriptor so writer-preference logic can see us.
  Waiter w{offset, end, mode};
  waiters_.push_back(&w);

  Status result = Status::kTimeout;

  while (!CanGrant(offset, end, mode, exclude_key)) {
    if (deadline == kForever) {
      cv_.wait(lk);
    } else {
      cv_status cvs = cv_.wait_until(lk, deadline);
      if (cvs == cv_status::timeout) {
        // Re-check under the lock before giving up (spurious wakeup guard).
        if (!CanGrant(offset, end, mode, exclude_key))
          break;
      }
    }
  }

  // Unregister.
  waiters_.erase(find(waiters_.begin(), waiters_.end(), &w));

  if (CanGrant(offset, end, mode, exclude_key)) {
    InsertEntry(offset, end, mode);
    result = Status::kAcquired;
  }

  return result;
}

//------------------------------------------------------------------------

void LockRange::DoRelease(uint64_t offset, uint64_t length, Mode mode) {
  uint64_t end = offset + length;

  unique_lock<mutex> lk(mu_);

  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    const EntryKey &key = it->first;
    LockEntry &e = it->second;

    if (key.offset > end)
      break; // past any possible match
    if (e.offset != offset || e.end != end || e.mode != mode)
      continue;

    if (mode == Mode::kShared && e.share_count > 1) {
      // Other shared holders remain; just decrement.
      --e.share_count;
      return;
    }

    // Full removal.
    entries_.erase(it);
    cv_.notify_all();
    return;
  }

  // Entry not found.  This is a benign race (e.g., the guard was moved from
  // and the source was default-initialized).  We silently ignore it.
}

} // namespace concurrency
} // namespace utils
