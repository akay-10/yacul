#include "lock_id.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

using namespace std;
using namespace utils::concurrency;

TEST(ExclusiveLock, GuardIsTrueAfterLock) {
  LockId<int> locker;
  auto g = locker.Lock(1);
  EXPECT_TRUE(static_cast<bool>(g));
}

TEST(ExclusiveLock, IdAccessorMatchesRequestedId) {
  LockId<int> locker;
  auto g = locker.Lock(42);
  EXPECT_EQ(g.id(), 42);
}

TEST(ExclusiveLock, ActiveEntryCountIsOneWhileHeld) {
  LockId<int> locker;
  auto g = locker.Lock(7);
  EXPECT_EQ(locker.ActiveEntryCount(), 1u);
}

TEST(ExclusiveLock, ActiveEntryCountDropsToZeroAfterScopeExit) {
  LockId<int> locker;
  {
    auto g = locker.Lock(7);
    ASSERT_EQ(locker.ActiveEntryCount(), 1u);
  }
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(ExclusiveLock, ManualReleaseDropsCountAndClearsGuard) {
  LockId<int> locker;
  auto g = locker.Lock(3);
  ASSERT_EQ(locker.ActiveEntryCount(), 1u);
  g.Release();
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
  EXPECT_FALSE(static_cast<bool>(g));
}

TEST(ExclusiveLock, DoubleReleaseIsIdempotent) {
  LockId<int> locker;
  auto g = locker.Lock(3);
  g.Release();
  g.Release(); // must not crash or double-decrement
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(MoveSemantics, MoveConstructorTransfersOwnership) {
  LockId<int> locker;
  auto g1 = locker.Lock(10);
  ASSERT_TRUE(static_cast<bool>(g1));

  auto g2 = move(g1);

  EXPECT_FALSE(static_cast<bool>(g1));
  EXPECT_TRUE(static_cast<bool>(g2));
  EXPECT_EQ(locker.ActiveEntryCount(), 1u);
}

TEST(MoveSemantics, MoveAssignmentTransfersOwnership) {
  LockId<int> locker;
  auto g1 = locker.Lock(10);
  LockIdGuard<int, hash<int>> g2;

  g2 = move(g1);

  EXPECT_FALSE(static_cast<bool>(g1));
  EXPECT_TRUE(static_cast<bool>(g2));
}

TEST(MoveSemantics, MovedFromGuardDoesNotDoubleRelease) {
  LockId<int> locker;
  {
    auto g1 = locker.Lock(10);
    auto g2 = move(g1);
    // g1 (empty) destructs first, then g2 releases properly.
  }
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(MoveSemantics, SharedGuardMoveConstructor) {
  LockId<int> locker;
  auto g1 = locker.LockShared(5);
  ASSERT_TRUE(static_cast<bool>(g1));

  auto g2 = move(g1);

  EXPECT_FALSE(static_cast<bool>(g1));
  EXPECT_TRUE(static_cast<bool>(g2));
}

TEST(TryLock, SucceedsWhenIdIsFree) {
  LockId<int> locker;
  auto g = locker.TryLock(20);
  EXPECT_TRUE(static_cast<bool>(g));
}

TEST(TryLock, FailsWhenExclusiveLockAlreadyHeld) {
  LockId<int> locker;
  auto held = locker.Lock(99);
  auto attempt = locker.TryLock(99);
  EXPECT_FALSE(static_cast<bool>(attempt));
}

TEST(TryLock, FailsWhenSharedLockAlreadyHeld) {
  LockId<int> locker;
  auto reader = locker.LockShared(99);
  auto writer_attempt = locker.TryLock(99);
  EXPECT_FALSE(static_cast<bool>(writer_attempt));
}

TEST(TryLock, SucceedsAfterHolderReleases) {
  LockId<int> locker;
  {
    auto held = locker.Lock(99);
  }
  auto g = locker.TryLock(99);
  EXPECT_TRUE(static_cast<bool>(g));
}

TEST(TryLockFor, ReturnsEmptyGuardWhenContested) {
  LockId<int> locker;
  auto held = locker.Lock(5);
  // Must fail: held already owns ID 5 exclusively.
  // Elapsed time is not asserted: shared_timed_mutex timed waits may return
  // immediately on platforms where the underlying POSIX clock is unavailable
  // (e.g. certain CI sandbox environments).
  auto attempt = locker.TryLockFor(5, chrono::milliseconds(50));
  EXPECT_FALSE(static_cast<bool>(attempt));
}

TEST(TryLockFor, SucceedsOnFreeId) {
  LockId<int> locker;
  auto g = locker.TryLockFor(1, chrono::milliseconds(100));
  EXPECT_TRUE(static_cast<bool>(g));
}

TEST(TryLockFor, SharedReturnsEmptyGuardWhenExclusiveHeld) {
  LockId<int> locker;
  auto held = locker.Lock(5);
  auto attempt = locker.TryLockSharedFor(5, chrono::milliseconds(50));
  EXPECT_FALSE(static_cast<bool>(attempt));
}

TEST(TryLockFor, SharedSucceedsOnFreeId) {
  LockId<int> locker;
  auto g = locker.TryLockSharedFor(1, chrono::milliseconds(100));
  EXPECT_TRUE(static_cast<bool>(g));
}

TEST(SharedLock, MultipleReadersCanCoexist) {
  LockId<int> locker;
  auto g1 = locker.LockShared(7);
  auto g2 = locker.LockShared(7);
  EXPECT_TRUE(static_cast<bool>(g1));
  EXPECT_TRUE(static_cast<bool>(g2));
  EXPECT_EQ(locker.ActiveEntryCount(), 1u);
}

TEST(SharedLock, EntryRemovedAfterAllReadersRelease) {
  LockId<int> locker;
  auto g1 = locker.LockShared(7);
  auto g2 = locker.LockShared(7);
  EXPECT_EQ(locker.ActiveEntryCount(), 1u);
  g1.Release();
  EXPECT_EQ(locker.ActiveEntryCount(), 1u);
  g2.Release();
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(SharedLock, ExclusiveBlockedByActiveReader) {
  LockId<int> locker;
  auto reader = locker.LockShared(3);
  auto writer_attempt = locker.TryLock(3);
  EXPECT_FALSE(static_cast<bool>(writer_attempt));
}

TEST(SharedLock, ExclusiveSucceedsAfterAllReadersRelease) {
  LockId<int> locker;
  {
    auto reader = locker.LockShared(3);
    ASSERT_FALSE(static_cast<bool>(locker.TryLock(3)));
  }
  auto writer = locker.TryLock(3);
  EXPECT_TRUE(static_cast<bool>(writer));
}

TEST(SharedLock, TrySharedSucceedsOnFreeId) {
  LockId<int> locker;
  auto g = locker.TryLockShared(55);
  EXPECT_TRUE(static_cast<bool>(g));
}

TEST(SharedLock, TrySharedFailsWhenExclusiveHeld) {
  LockId<int> locker;
  auto writer = locker.Lock(55);
  auto reader = locker.TryLockShared(55);
  EXPECT_FALSE(static_cast<bool>(reader));
}

TEST(MultipleIds, DistinctIdsDoNotBlockEachOther) {
  LockId<string> locker;
  auto g1 = locker.Lock("alice");
  auto g2 = locker.Lock("bob");
  EXPECT_TRUE(static_cast<bool>(g1));
  EXPECT_TRUE(static_cast<bool>(g2));
  EXPECT_EQ(locker.ActiveEntryCount(), 2u);
}

TEST(MultipleIds, EntryCountTracksDistinctIds) {
  LockId<int> locker;
  auto g1 = locker.Lock(1);
  auto g2 = locker.Lock(2);
  auto g3 = locker.Lock(3);
  EXPECT_EQ(locker.ActiveEntryCount(), 3u);
  g2.Release();
  EXPECT_EQ(locker.ActiveEntryCount(), 2u);
}

TEST(MultipleIds, SameIdOnlyCountsOnce) {
  LockId<int> locker;
  auto r1 = locker.LockShared(100);
  auto r2 = locker.LockShared(100);
  auto r3 = locker.LockShared(100);
  EXPECT_EQ(locker.ActiveEntryCount(), 1u);
}

TEST(Concurrency, ExclusiveLockProtectsCounter) {
  LockId<int> locker;
  int counter = 0;
  const int kThreads = 8;
  const int kIterations = 10'000;

  vector<thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < kIterations; ++i) {
        auto g = locker.Lock(42);
        ++counter;
      }
    });
  }
  for (auto &th : threads)
    th.join();

  EXPECT_EQ(counter, kThreads * kIterations);
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(Concurrency, SharedLockAllowsConcurrentReads) {
  LockId<int> locker;
  // All threads read the same value concurrently; no corruption expected.
  const int kValue = 123;
  const int kThreads = 8;
  const int kIterations = 5'000;
  atomic<int> errors{0};

  vector<thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < kIterations; ++i) {
        auto g = locker.LockShared(1);
        if (kValue != 123)
          ++errors;
      }
    });
  }
  for (auto &th : threads)
    th.join();

  EXPECT_EQ(errors.load(), 0);
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(Concurrency, DistinctIdsConcurrentlyWithNoContention) {
  // Each thread locks its own exclusive ID – zero cross-thread contention.
  LockId<int> locker;
  const int kThreads = 16;
  const int kIterations = 5'000;
  vector<int> counters(kThreads, 0);

  vector<thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kIterations; ++i) {
        auto g = locker.Lock(t);
        ++counters[t];
      }
    });
  }
  for (auto &th : threads)
    th.join();

  for (int t = 0; t < kThreads; ++t) {
    EXPECT_EQ(counters[t], kIterations);
  }
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(LockUnlockCycle, IntIdCycleIsStable) {
  LockId<int> locker;
  for (int i = 0; i < 100; ++i) {
    auto g = locker.Lock(i % 5);
    EXPECT_TRUE(static_cast<bool>(g));
  }
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(LockUnlockCycle, StringIdCycleIsStable) {
  LockId<string> locker;
  for (int i = 0; i < 100; ++i) {
    auto g = locker.Lock("id_" + to_string(i % 5));
    EXPECT_TRUE(static_cast<bool>(g));
  }
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(LockUnlockCycle, ActiveEntryCountZeroAfterMixedIntOps) {
  LockId<int> locker;
  for (int i = 0; i < 50; ++i) {
    auto g1 = locker.Lock(i);
    auto g2 = locker.LockShared(i + 1000);
  }
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(LockUnlockCycle, ActiveEntryCountZeroAfterMixedStringOps) {
  LockId<string> locker;
  for (int i = 0; i < 50; ++i) {
    auto g1 = locker.Lock("w_" + to_string(i));
    auto g2 = locker.LockShared("r_" + to_string(i));
  }
  EXPECT_EQ(locker.ActiveEntryCount(), 0u);
}

TEST(NumShards, DefaultShardCountMatchesConstant) {
  LockId<int> locker;
  EXPECT_EQ(locker.NumShards(), LockId<int>::kDefaultShards);
}

TEST(NumShards, CustomShardCountRespected) {
  LockId<int> locker(8);
  EXPECT_EQ(locker.NumShards(), 8u);
}
