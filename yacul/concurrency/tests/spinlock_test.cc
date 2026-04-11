#include "yacul/concurrency/spinlock.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace std;
using namespace chrono_literals;
using namespace utils::concurrency;

class SpinlockEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    hardware_concurrency_ = thread::hardware_concurrency();
    if (hardware_concurrency_ == 0)
      hardware_concurrency_ = 2;
  }

  void TearDown() override {}

  static unsigned int hardware_concurrency() { return hardware_concurrency_; }

private:
  static unsigned int hardware_concurrency_;
};

unsigned int SpinlockEnvironment::hardware_concurrency_ = 1;

class SpinlockTest : public ::testing::Test {
protected:
  void SetUp() override { shared_counter_ = 0; }

  // Launches 'num_threads' threads, each executing 'fn', then joins all.
  void RunThreads(unsigned int num_threads, function<void()> fn) {
    vector<thread> threads;
    threads.reserve(num_threads);
    for (unsigned int i = 0; i < num_threads; ++i) {
      threads.emplace_back(fn);
    }
    for (auto &t : threads)
      t.join();
  }

  Spinlock mu_;
  int shared_counter_ = 0;
};

TEST_F(SpinlockTest, InitialStateIsUnlocked) { EXPECT_FALSE(mu_.is_locked()); }

TEST_F(SpinlockTest, LockSetsLockedState) {
  mu_.lock();
  EXPECT_TRUE(mu_.is_locked());
  mu_.unlock();
}

TEST_F(SpinlockTest, UnlockClearsLockedState) {
  mu_.lock();
  mu_.unlock();
  EXPECT_FALSE(mu_.is_locked());
}

TEST_F(SpinlockTest, TryLockSucceedsWhenUnlocked) {
  EXPECT_TRUE(mu_.try_lock());
  mu_.unlock();
}

TEST_F(SpinlockTest, TryLockFailsWhenLocked) {
  mu_.lock();
  EXPECT_FALSE(mu_.try_lock());
  mu_.unlock();
}

TEST_F(SpinlockTest, RepeatedLockUnlock) {
  for (int i = 0; i < 1000; ++i) {
    mu_.lock();
    mu_.unlock();
  }
  EXPECT_FALSE(mu_.is_locked());
}

TEST_F(SpinlockTest, LockGuardCompatibility) {
  {
    lock_guard<Spinlock> lk(mu_);
    EXPECT_TRUE(mu_.is_locked());
  }
  EXPECT_FALSE(mu_.is_locked());
}

TEST_F(SpinlockTest, UniqueLockCompatibility) {
  {
    unique_lock<Spinlock> lk(mu_);
    EXPECT_TRUE(mu_.is_locked());
    lk.unlock();
    EXPECT_FALSE(mu_.is_locked());
    lk.lock();
    EXPECT_TRUE(mu_.is_locked());
  }
  EXPECT_FALSE(mu_.is_locked());
}

TEST_F(SpinlockTest, UniqueLockDeferLock) {
  unique_lock<Spinlock> lk(mu_, defer_lock);
  EXPECT_FALSE(mu_.is_locked());
  lk.lock();
  EXPECT_TRUE(mu_.is_locked());
}

TEST_F(SpinlockTest, UniqueLockTryToLock) {
  unique_lock<Spinlock> lk(mu_, try_to_lock);
  EXPECT_TRUE(lk.owns_lock());
}

TEST_F(SpinlockTest, ScopedLockCompatibility) {
  {
    scoped_lock lk(mu_);
    EXPECT_TRUE(mu_.is_locked());
  }
  EXPECT_FALSE(mu_.is_locked());
}

TEST_F(SpinlockTest, StdLockCompatibility) {
  Spinlock mu2;
  {
    // std::lock acquires both without deadlock.
    lock(mu_, mu2);
    lock_guard<Spinlock> lk1(mu_, adopt_lock);
    lock_guard<Spinlock> lk2(mu2, adopt_lock);
    EXPECT_TRUE(mu_.is_locked());
    EXPECT_TRUE(mu2.is_locked());
  }
  EXPECT_FALSE(mu_.is_locked());
  EXPECT_FALSE(mu2.is_locked());
}

TEST_F(SpinlockTest, MutualExclusionTwoThreads) {
  const int kIterations = 100'000;
  int counter = 0;

  auto worker = [&]() {
    for (int i = 0; i < kIterations; ++i) {
      lock_guard<Spinlock> lk(mu_);
      ++counter;
    }
  };

  RunThreads(2, worker);
  EXPECT_EQ(counter, 2 * kIterations);
}

TEST_F(SpinlockTest, MutualExclusionManyThreads) {
  const unsigned int num_threads =
    min(SpinlockEnvironment::hardware_concurrency() * 2u, 16u);
  const int kIterations = 50'000;
  int counter = 0;

  auto worker = [&]() {
    for (int i = 0; i < kIterations; ++i) {
      lock_guard<Spinlock> lk(mu_);
      ++counter;
    }
  };

  RunThreads(num_threads, worker);
  EXPECT_EQ(counter, static_cast<int>(num_threads) * kIterations);
}

TEST_F(SpinlockTest, TryLockConcurrently) {
  // Only one thread should acquire the lock at a time.
  atomic<int> concurrent_holders{0};
  atomic<int> max_concurrent{0};
  const int kIterations = 10'000;

  auto worker = [&]() {
    for (int i = 0; i < kIterations; ++i) {
      while (!mu_.try_lock()) {
        this_thread::yield();
      }
      // Inside critical section.
      int val = ++concurrent_holders;
      // Atomically track maximum concurrent holders.
      int expected = max_concurrent.load(memory_order_relaxed);
      while (val > expected && !max_concurrent.compare_exchange_weak(
                                 expected, val, memory_order_relaxed)) {
      }
      --concurrent_holders;
      mu_.unlock();
    }
  };

  RunThreads(4, worker);
  EXPECT_EQ(max_concurrent.load(), 1);
}

TEST_F(SpinlockTest, LockReleasedOnException) {
  try {
    lock_guard<Spinlock> lk(mu_);
    throw runtime_error("simulated");
  } catch (...) {
  }
  EXPECT_FALSE(mu_.is_locked());
  // Verify re-acquisition works.
  EXPECT_TRUE(mu_.try_lock());
  mu_.unlock();
}

TEST_F(SpinlockTest, PerformanceSmokeTest) {
  const int kIterations = 1'000'000;
  auto start = chrono::steady_clock::now();

  for (int i = 0; i < kIterations; ++i) {
    mu_.lock();
    ++shared_counter_;
    mu_.unlock();
  }

  auto elapsed = chrono::steady_clock::now() - start;
  auto ns = chrono::duration_cast<chrono::nanoseconds>(elapsed).count();

  EXPECT_EQ(shared_counter_, kIterations);

  // Expect less than 50 ns per uncontended lock/unlock cycle on any modern
  // machine. This is a loose bound to avoid flakiness in CI.
  double ns_per_op = static_cast<double>(ns) / kIterations;
  EXPECT_LT(ns_per_op, 50.0)
    << "Uncontended lock/unlock took " << ns_per_op << " ns/op";
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new SpinlockEnvironment());
  return RUN_ALL_TESTS();
}
