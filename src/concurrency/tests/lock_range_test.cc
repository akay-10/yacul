#include "lock_range.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std;
using namespace chrono_literals;
using namespace utils::concurrency;

class LockRangeEnvironment : public ::testing::Environment {
public:
  void SetUp() override {}

  void TearDown() override {}
};

static ::testing::Environment *const kGlobalEnv =
  ::testing::AddGlobalTestEnvironment(new LockRangeEnvironment);

class LockRangeTest : public ::testing::Test {
protected:
  LockRange lr_;
};

TEST_F(LockRangeTest, ExclusiveLockAcquiredAndReleased) {
  auto g = lr_.Lock(0, 100, LockRange::Mode::kExclusive);

  EXPECT_TRUE(g.Valid());
  EXPECT_EQ(g.offset(), 0u);
  EXPECT_EQ(g.length(), 100u);
  EXPECT_EQ(g.mode(), LockRange::Mode::kExclusive);
  EXPECT_EQ(lr_.ActiveLockCount(), 1u);

  g.Release();

  EXPECT_FALSE(g.Valid());
  EXPECT_EQ(lr_.ActiveLockCount(), 0u);
}

TEST_F(LockRangeTest, SharedLockAcquiredAndReleased) {
  auto g = lr_.Lock(0, 100, LockRange::Mode::kShared);

  EXPECT_TRUE(g.Valid());
  EXPECT_EQ(g.offset(), 0u);
  EXPECT_EQ(g.length(), 100u);
  EXPECT_EQ(g.mode(), LockRange::Mode::kShared);
  EXPECT_EQ(lr_.ActiveLockCount(), 1u);

  g.Release();

  EXPECT_FALSE(g.Valid());
  EXPECT_EQ(lr_.ActiveLockCount(), 0u);
}

TEST_F(LockRangeTest, DefaultConstructedGuardIsInvalid) {
  LockRange::Guard g;
  EXPECT_FALSE(g.Valid());
}

TEST_F(LockRangeTest, ReleaseIsIdempotent) {
  auto g = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  g.Release();
  g.Release(); // must not crash or double-decrement the entry count

  EXPECT_FALSE(g.Valid());
  EXPECT_EQ(lr_.ActiveLockCount(), 0u);
}

TEST_F(LockRangeTest, GuardMoveConstruct) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  LockRange::Guard g2 = move(g1);

  EXPECT_FALSE(g1.Valid());
  EXPECT_TRUE(g2.Valid());
  EXPECT_EQ(lr_.ActiveLockCount(), 1u);
}

TEST_F(LockRangeTest, GuardMoveAssignReleasesDestination) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  auto g2 = lr_.Lock(200, 100, LockRange::Mode::kExclusive);
  EXPECT_EQ(lr_.ActiveLockCount(), 2u);

  // g2 is overwritten; its lock on [200,300) must be released.
  g2 = move(g1);

  EXPECT_FALSE(g1.Valid());
  EXPECT_TRUE(g2.Valid());
  EXPECT_EQ(g2.offset(), 0u);
  EXPECT_EQ(lr_.ActiveLockCount(), 1u);
}

TEST_F(LockRangeTest, DestructorReleasesLock) {
  {
    auto g = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
    EXPECT_EQ(lr_.ActiveLockCount(), 1u);
  }
  EXPECT_EQ(lr_.ActiveLockCount(), 0u);
}

TEST_F(LockRangeTest, MultipleConcurrentSharedLocksOnSameRange) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kShared);
  auto g2 = lr_.Lock(0, 100, LockRange::Mode::kShared);
  auto g3 = lr_.Lock(0, 100, LockRange::Mode::kShared);

  EXPECT_TRUE(g1.Valid());
  EXPECT_TRUE(g2.Valid());
  EXPECT_TRUE(g3.Valid());
  EXPECT_EQ(lr_.ActiveLockCount(), 3u);
}

TEST_F(LockRangeTest, NonOverlappingExclusiveLocksCoexist) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  auto g2 = lr_.Lock(200, 100, LockRange::Mode::kExclusive);
  auto g3 = lr_.Lock(1000, 500, LockRange::Mode::kExclusive);

  EXPECT_TRUE(g1.Valid());
  EXPECT_TRUE(g2.Valid());
  EXPECT_TRUE(g3.Valid());
}

TEST_F(LockRangeTest, AdjacentRangesAreNotOverlapping) {
  // [0, 100) and [100, 200) share a boundary but do not overlap.
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  auto g2 = lr_.Lock(100, 100, LockRange::Mode::kExclusive);

  EXPECT_TRUE(g1.Valid());
  EXPECT_TRUE(g2.Valid());
}

TEST_F(LockRangeTest, IsLockedAnyReturnsFalseWhenEmpty) {
  EXPECT_FALSE(lr_.IsLockedAny(0, 100));
}

TEST_F(LockRangeTest, IsLockedAnyDetectsExactRange) {
  auto g = lr_.Lock(50, 50, LockRange::Mode::kExclusive); // [50, 100)
  EXPECT_TRUE(lr_.IsLockedAny(50, 50));
}

TEST_F(LockRangeTest, IsLockedAnyDetectsPartialOverlap) {
  auto g = lr_.Lock(50, 50, LockRange::Mode::kExclusive); // [50, 100)
  EXPECT_TRUE(lr_.IsLockedAny(0, 100)); // [0,  100) — left-overlapping
  EXPECT_TRUE(lr_.IsLockedAny(75, 50)); // [75, 125) — right-overlapping
  EXPECT_TRUE(lr_.IsLockedAny(60, 10)); // [60,  70) — contained within
}

TEST_F(LockRangeTest, IsLockedAnyMissesAdjacentAndDisjointRanges) {
  auto g = lr_.Lock(50, 50, LockRange::Mode::kExclusive); // [50, 100)
  EXPECT_FALSE(lr_.IsLockedAny(0, 50));    // [0,   50) — adjacent left
  EXPECT_FALSE(lr_.IsLockedAny(100, 100)); // [100, 200) — adjacent right
  EXPECT_FALSE(lr_.IsLockedAny(200, 100)); // far right
}

TEST_F(LockRangeTest, IsLockedAnyReturnsFalseAfterRelease) {
  auto g = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  EXPECT_TRUE(lr_.IsLockedAny(0, 100));
  g.Release();
  EXPECT_FALSE(lr_.IsLockedAny(0, 100));
}

TEST_F(LockRangeTest, TryLockSucceedsWhenFree) {
  LockRange::Status s = LockRange::Status::kTimeout;
  auto g = lr_.TryLock(0, 100, LockRange::Mode::kExclusive, &s);

  EXPECT_TRUE(g.Valid());
  EXPECT_EQ(s, LockRange::Status::kAcquired);
}

TEST_F(LockRangeTest, TryLockExclusiveFailsUnderExclusiveConflict) {
  auto holder = lr_.Lock(0, 100, LockRange::Mode::kExclusive);

  LockRange::Status s = LockRange::Status::kAcquired;
  auto g = lr_.TryLock(50, 50, LockRange::Mode::kExclusive, &s);

  EXPECT_FALSE(g.Valid());
  EXPECT_EQ(s, LockRange::Status::kTimeout);
}

TEST_F(LockRangeTest, TryLockSharedFailsUnderExclusiveConflict) {
  auto holder = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  LockRange::Status s = LockRange::Status::kAcquired;
  auto g = lr_.TryLock(0, 100, LockRange::Mode::kShared, &s);

  EXPECT_FALSE(g.Valid());
  EXPECT_EQ(s, LockRange::Status::kTimeout);
}

TEST_F(LockRangeTest, TryLockSharedSucceedsAlongsideExistingShared) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kShared);

  LockRange::Status s = LockRange::Status::kTimeout;
  auto g2 = lr_.TryLock(0, 100, LockRange::Mode::kShared, &s);

  EXPECT_TRUE(g2.Valid());
  EXPECT_EQ(s, LockRange::Status::kAcquired);
}

TEST_F(LockRangeTest, TryLockExclusiveFailsAlongsideShared) {
  auto shared = lr_.Lock(0, 100, LockRange::Mode::kShared);

  LockRange::Status s = LockRange::Status::kAcquired;
  auto g = lr_.TryLock(0, 100, LockRange::Mode::kExclusive, &s);

  EXPECT_FALSE(g.Valid());
  EXPECT_EQ(s, LockRange::Status::kTimeout);
}

TEST_F(LockRangeTest, TryLockForSucceedsWhenFree) {
  LockRange::Status s = LockRange::Status::kTimeout;
  auto g = lr_.TryLockFor(0, 100, LockRange::Mode::kExclusive, 200ms, &s);

  EXPECT_TRUE(g.Valid());
  EXPECT_EQ(s, LockRange::Status::kAcquired);
}

TEST_F(LockRangeTest, TryLockForTimesOutUnderContention) {
  auto holder = lr_.Lock(0, 1000, LockRange::Mode::kExclusive);

  LockRange::Status s = LockRange::Status::kAcquired;
  auto start = chrono::steady_clock::now();
  auto g = lr_.TryLockFor(500, 100, LockRange::Mode::kExclusive, 50ms, &s);
  auto elapsed = chrono::steady_clock::now() - start;

  EXPECT_FALSE(g.Valid());
  EXPECT_EQ(s, LockRange::Status::kTimeout);
  // Verify we blocked for approximately the requested duration.
  // Allow generous slack (40 ms lower bound) for CI scheduling jitter.
  EXPECT_GE(elapsed, 40ms);
}

TEST_F(LockRangeTest, TryLockForSucceedsWhenHolderReleasesInTime) {
  auto holder = lr_.Lock(0, 100, LockRange::Mode::kExclusive);

  thread releaser([&] {
    this_thread::sleep_for(20ms);
    holder.Release();
  });

  LockRange::Status s = LockRange::Status::kTimeout;
  auto g = lr_.TryLockFor(0, 100, LockRange::Mode::kExclusive, 500ms, &s);
  releaser.join();

  EXPECT_TRUE(g.Valid());
  EXPECT_EQ(s, LockRange::Status::kAcquired);
}

TEST_F(LockRangeTest, ExclusiveBlocksSharedUntilReleased) {
  atomic<bool> shared_acquired{false};

  auto excl = lr_.Lock(0, 1000, LockRange::Mode::kExclusive);

  thread t([&] {
    auto g = lr_.Lock(0, 100, LockRange::Mode::kShared);
    shared_acquired.store(true, memory_order_release);
  });

  this_thread::sleep_for(30ms);
  EXPECT_FALSE(shared_acquired.load(memory_order_acquire));

  excl.Release();
  t.join();
  EXPECT_TRUE(shared_acquired.load(memory_order_acquire));
}

TEST_F(LockRangeTest, AllSharedsMustReleaseBeforeExclusiveGranted) {
  atomic<bool> excl_acquired{false};

  auto s1 = lr_.Lock(0, 1000, LockRange::Mode::kShared);
  auto s2 = lr_.Lock(0, 1000, LockRange::Mode::kShared);

  thread t([&] {
    auto g = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
    excl_acquired.store(true, memory_order_release);
  });

  this_thread::sleep_for(30ms);
  EXPECT_FALSE(excl_acquired.load(memory_order_acquire));

  s1.Release();
  this_thread::sleep_for(10ms);
  EXPECT_FALSE(excl_acquired.load(memory_order_acquire)); // s2 still held

  s2.Release();
  t.join();
  EXPECT_TRUE(excl_acquired.load(memory_order_acquire));
}

TEST_F(LockRangeTest, ConcurrentNonOverlappingExclusiveLocks) {
  constexpr int kThreads = 8;
  atomic<int> ready{0};
  atomic<int> done{0};
  vector<thread> threads;

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i] {
      uint64_t offset = static_cast<uint64_t>(i) * 1000;
      auto g = lr_.Lock(offset, 500, LockRange::Mode::kExclusive);
      ready.fetch_add(1, memory_order_acq_rel);
      // Spin until every thread has acquired its lock to prove
      // non-overlapping ranges truly don't block each other.
      while (ready.load(memory_order_acquire) < kThreads)
        this_thread::yield();
      done.fetch_add(1, memory_order_acq_rel);
    });
  }

  for (auto &t : threads)
    t.join();
  EXPECT_EQ(done.load(), kThreads);
}

TEST_F(LockRangeTest, HighConcurrencyExclusiveContentionNeverOverlaps) {
  // Many threads competing for the same exclusive range.
  // The invariant: at most one holder at a time (concurrent count never > 1).
  constexpr int kThreads = 16;
  constexpr int kIterations = 20;
  atomic<int> concurrent{0};
  atomic<bool> violation{false};

  vector<thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < kIterations; ++j) {
        auto g = lr_.Lock(0, 1000, LockRange::Mode::kExclusive);
        int prev = concurrent.fetch_add(1, memory_order_acq_rel);
        if (prev != 0)
          violation.store(true, memory_order_release);
        this_thread::yield();
        concurrent.fetch_sub(1, memory_order_acq_rel);
      }
    });
  }

  for (auto &t : threads)
    t.join();

  EXPECT_FALSE(violation.load());
  EXPECT_EQ(lr_.ActiveLockCount(), 0u);
}

TEST_F(LockRangeTest, WriterPreventsFurtherReadersFromJumpingAhead) {
  // Sequence:
  //   1. Exclusive lock held — blocks everything.
  //   2. reader1 waits.
  //   3. writer  waits (queued after reader1).
  //   4. reader2 waits (queued after writer).
  //   5. Initial lock released.
  // With writer-preference reader2 may not acquire before the writer does.
  // We verify all three eventually run (total == 111).

  atomic<int> acquisitions{0};

  auto first = lr_.Lock(0, 100, LockRange::Mode::kExclusive);

  thread reader1([&] {
    auto g = lr_.Lock(0, 100, LockRange::Mode::kShared);
    acquisitions.fetch_add(1, memory_order_acq_rel);
  });
  this_thread::sleep_for(10ms);

  thread writer([&] {
    auto g = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
    acquisitions.fetch_add(10, memory_order_acq_rel);
  });
  this_thread::sleep_for(10ms);

  thread reader2([&] {
    auto g = lr_.Lock(0, 100, LockRange::Mode::kShared);
    acquisitions.fetch_add(100, memory_order_acq_rel);
  });
  this_thread::sleep_for(10ms);

  first.Release();

  reader1.join();
  writer.join();
  reader2.join();

  EXPECT_EQ(acquisitions.load(), 111);
}

class LockRangeUpgradeTest : public ::testing::Test {
protected:
  LockRange lr_;
};

TEST_F(LockRangeUpgradeTest, SoleSharedUpgradesToExclusive) {
  auto g = lr_.Lock(0, 100, LockRange::Mode::kShared);
  ASSERT_EQ(g.mode(), LockRange::Mode::kShared);

  g.Upgrade();

  EXPECT_TRUE(g.Valid());
  EXPECT_EQ(g.mode(), LockRange::Mode::kExclusive);
  EXPECT_EQ(lr_.ActiveLockCount(), 1u);
}

TEST_F(LockRangeUpgradeTest, UpgradeBlocksWhileOtherSharedOutstanding) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kShared);
  auto g2 = lr_.Lock(0, 100, LockRange::Mode::kShared);

  atomic<bool> upgraded{false};
  thread t([&] {
    g1.Upgrade();
    upgraded.store(true, memory_order_release);
  });

  this_thread::sleep_for(30ms);
  EXPECT_FALSE(upgraded.load(memory_order_acquire));

  g2.Release();
  t.join();

  EXPECT_TRUE(upgraded.load(memory_order_acquire));
  EXPECT_EQ(g1.mode(), LockRange::Mode::kExclusive);
}

TEST_F(LockRangeUpgradeTest, UpgradeForSucceedsWhenPeerReleasesInTime) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kShared);
  auto g2 = lr_.Lock(0, 100, LockRange::Mode::kShared);

  thread t([&] {
    this_thread::sleep_for(20ms);
    g2.Release();
  });

  LockRange::Status s = g1.UpgradeFor(500ms);
  t.join();

  EXPECT_EQ(s, LockRange::Status::kAcquired);
  EXPECT_TRUE(g1.Valid());
  EXPECT_EQ(g1.mode(), LockRange::Mode::kExclusive);
}

TEST_F(LockRangeUpgradeTest, UpgradeForTimesOutAndInvalidatesGuard) {
  auto g1 = lr_.Lock(0, 100, LockRange::Mode::kShared);
  auto g2 = lr_.Lock(0, 100, LockRange::Mode::kShared);

  LockRange::Status s = g1.UpgradeFor(50ms);

  EXPECT_EQ(s, LockRange::Status::kTimeout);
  // Guard is invalidated on timeout — the caller holds no lock.
  EXPECT_FALSE(g1.Valid());
  // Peer guard is unaffected.
  EXPECT_TRUE(g2.Valid());
}

TEST_F(LockRangeUpgradeTest, UpgradeOnInvalidGuardThrows) {
  LockRange::Guard g;
  EXPECT_DEATH(g.Upgrade(), "Guard is not valid");
}

TEST_F(LockRangeUpgradeTest, UpgradeOnAlreadyExclusiveGuardThrows) {
  auto g = lr_.Lock(0, 100, LockRange::Mode::kExclusive);
  EXPECT_DEATH(g.Upgrade(), "Guard is already exclusive");
}

class LockRangeValidationTest : public ::testing::Test {
protected:
  LockRange lr_;
};

TEST_F(LockRangeValidationTest, ZeroLengthLockThrowsInvalidArgument) {
  EXPECT_DEATH(lr_.Lock(0, 0, LockRange::Mode::kExclusive),
               "length must be > 0");
}

TEST_F(LockRangeValidationTest, ZeroLengthTryLockThrowsInvalidArgument) {
  EXPECT_DEATH(lr_.TryLock(0, 0, LockRange::Mode::kExclusive),
               "length must be > 0");
}

TEST_F(LockRangeValidationTest, ZeroLengthTryLockForThrowsInvalidArgument) {
  EXPECT_DEATH(lr_.TryLockFor(0, 0, LockRange::Mode::kExclusive, 10ms),
               "length must be > 0");
}

TEST_F(LockRangeValidationTest, OverflowingRangeThrowsOverflowError) {
  constexpr uint64_t kMax = numeric_limits<uint64_t>::max();
  EXPECT_DEATH(lr_.Lock(kMax, 1, LockRange::Mode::kExclusive),
               "overflows uint64_t");
  EXPECT_DEATH(lr_.Lock(1, kMax, LockRange::Mode::kExclusive),
               "overflows uint64_t");
}
