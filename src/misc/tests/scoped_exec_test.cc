#include "scoped_exec.h"

#include <atomic>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std;
using namespace utils::misc;

class ScopedExecEnvironment : public ::testing::Environment {
public:
  void SetUp() override {}
  void TearDown() override {}
};

template <typename Policy> class ScopedExecPolicyTest : public ::testing::Test {
protected:
  using Exec = ScopedExec<Policy>;

  function<void()> MakeIncrementer(atomic<int> &counter) {
    return [&counter] { ++counter; };
  }

  function<void()> MakeLogger(string &log, string token) {
    return [&log, t = move(token)] { log += t; };
  }
};

using PolicyTypes = ::testing::Types<ThreadSafePolicy, UnsafePolicy>;
TYPED_TEST_SUITE(ScopedExecPolicyTest, PolicyTypes);

TYPED_TEST(ScopedExecPolicyTest, DefaultConstructedIsDisarmed) {
  typename TestFixture::Exec exec;
  EXPECT_FALSE(static_cast<bool>(exec));
}

TYPED_TEST(ScopedExecPolicyTest, ConstructedWithFunctorIsArmed) {
  atomic<int> counter{0};
  {
    typename TestFixture::Exec exec(this->MakeIncrementer(counter));
    EXPECT_TRUE(static_cast<bool>(exec));
  }
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, ConstructedWithNullFunctionIsDisarmed) {
  function<void()> null_fn;
  typename TestFixture::Exec exec(null_fn);
  EXPECT_FALSE(static_cast<bool>(exec));
}

TYPED_TEST(ScopedExecPolicyTest, DestructorRunsFunctorExactlyOnce) {
  atomic<int> counter{0};
  {
    typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  }
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, DestructorDoesNotRunAfterRelease) {
  atomic<int> counter{0};
  {
    typename TestFixture::Exec exec(this->MakeIncrementer(counter));
    exec.release();
  }
  EXPECT_EQ(counter.load(), 0);
}

TYPED_TEST(ScopedExecPolicyTest, DestructorSuppressesExceptions) {
  EXPECT_NO_THROW(
    { typename TestFixture::Exec exec([] { throw runtime_error("boom"); }); });
}

TYPED_TEST(ScopedExecPolicyTest, ReleaseDisarmsExecutor) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  auto fn = exec.release();
  EXPECT_FALSE(static_cast<bool>(exec));
  EXPECT_EQ(counter.load(), 0);
}

TYPED_TEST(ScopedExecPolicyTest, ReleaseReturnsFunctor) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  auto fn = exec.release();
  ASSERT_TRUE(static_cast<bool>(fn));
  fn();
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, ReleaseOnDisarmedReturnsEmptyFunction) {
  typename TestFixture::Exec exec;
  auto fn = exec.release();
  EXPECT_FALSE(static_cast<bool>(fn));
}

TYPED_TEST(ScopedExecPolicyTest, InvokeRunsFunctorAndDisarms) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  exec.invoke();
  EXPECT_EQ(counter.load(), 1);
  EXPECT_FALSE(static_cast<bool>(exec));
}

TYPED_TEST(ScopedExecPolicyTest, InvokeOnDisarmedIsNoop) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  exec.release();
  exec.invoke();
  EXPECT_EQ(counter.load(), 0);
}

TYPED_TEST(ScopedExecPolicyTest, InvokeRunsOnlyOnce) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  exec.invoke();
  exec.invoke();
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, InvokePropagatesException) {
  typename TestFixture::Exec exec([] { throw runtime_error("err"); });
  EXPECT_THROW(exec.invoke(), runtime_error);
  EXPECT_FALSE(static_cast<bool>(exec));
}

TYPED_TEST(ScopedExecPolicyTest, InvokeDisarmsBeforeExecuting) {
  // Even when the functor throws, the executor must be disarmed so the
  // destructor does not fire it a second time.
  atomic<int> counter{0};
  {
    typename TestFixture::Exec exec([&counter] {
      ++counter;
      throw runtime_error("fail");
    });
    EXPECT_THROW(exec.invoke(), runtime_error);
  }
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, ResetReplacesExistingFunctor) {
  string log;
  typename TestFixture::Exec exec(this->MakeLogger(log, "A"));
  exec.reset(this->MakeLogger(log, "B"));
  EXPECT_EQ(log, "A");
  exec.invoke();
  EXPECT_EQ(log, "AB");
}

TYPED_TEST(ScopedExecPolicyTest, ResetOnDisarmedArmsWithNewFunctor) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec;
  exec.reset(this->MakeIncrementer(counter));
  EXPECT_TRUE(static_cast<bool>(exec));
  exec.invoke();
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, ResetWithNullFunctionDisarms) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  exec.reset(function<void()>{nullptr});
  EXPECT_FALSE(static_cast<bool>(exec));
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, ResetNoArgRunsOldFunctorAndDisarms) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec(this->MakeIncrementer(counter));
  exec.reset();
  EXPECT_EQ(counter.load(), 1);
  EXPECT_FALSE(static_cast<bool>(exec));
}

TYPED_TEST(ScopedExecPolicyTest, ResetNoArgOnDisarmedIsNoop) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec;
  exec.reset();
  EXPECT_EQ(counter.load(), 0);
  EXPECT_FALSE(static_cast<bool>(exec));
}

TYPED_TEST(ScopedExecPolicyTest, MoveConstructorTransfersOwnership) {
  atomic<int> counter{0};
  typename TestFixture::Exec src(this->MakeIncrementer(counter));
  typename TestFixture::Exec dst(move(src));
  EXPECT_FALSE(static_cast<bool>(src)); // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(static_cast<bool>(dst));
  dst.invoke();
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest,
           MoveConstructedSourceDoesNotFireOnDestruction) {
  atomic<int> counter{0};
  {
    typename TestFixture::Exec src(this->MakeIncrementer(counter));
    typename TestFixture::Exec dst(move(src));
    dst.release();
  }
  EXPECT_EQ(counter.load(), 0);
}

TYPED_TEST(ScopedExecPolicyTest, MoveAssignmentRunsOldFunctorAndTransfers) {
  string log;
  typename TestFixture::Exec src(this->MakeLogger(log, "SRC"));
  typename TestFixture::Exec dst(this->MakeLogger(log, "DST"));
  dst = move(src);
  EXPECT_EQ(log, "DST");
  EXPECT_FALSE(static_cast<bool>(src)); // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(static_cast<bool>(dst));
  dst.invoke();
  EXPECT_EQ(log, "DSTSRC");
}

TYPED_TEST(ScopedExecPolicyTest, MoveAssignDisarmedOntoArmed) {
  atomic<int> counter{0};
  typename TestFixture::Exec src;
  typename TestFixture::Exec dst(this->MakeIncrementer(counter));
  dst = move(src);
  EXPECT_EQ(counter.load(), 1);
  EXPECT_FALSE(static_cast<bool>(dst));
}

TYPED_TEST(ScopedExecPolicyTest, AcceptsStdFunction) {
  atomic<int> counter{0};
  function<void()> fn = [&counter] { ++counter; };
  {
    typename TestFixture::Exec exec(fn);
  }
  EXPECT_EQ(counter.load(), 1);
}

TYPED_TEST(ScopedExecPolicyTest, AcceptsSharedPtrCapture) {
  auto ptr = make_shared<int>(42);
  int captured = 0;
  {
    typename TestFixture::Exec exec(
      [p = move(ptr), &captured] { captured = *p; });
  }
  EXPECT_EQ(captured, 42);
}

TYPED_TEST(ScopedExecPolicyTest, FunctorCanReArmViaResetWithoutDeadlock) {
  atomic<int> counter{0};
  typename TestFixture::Exec exec;
  auto arm = [&](auto &self_ref) -> void {
    exec.reset([&counter, &exec, &self_ref] {
      ++counter;
      if (counter.load() < 3)
        self_ref(self_ref);
    });
  };
  arm(arm);
  exec.invoke();
  exec.invoke();
  exec.invoke();
  EXPECT_EQ(counter.load(), 3);
  EXPECT_FALSE(static_cast<bool>(exec));
}

TEST(ScopedExecSizeTest, UnsafePolicySmallerThanThreadSafePolicy) {
  EXPECT_LT(sizeof(ScopedExecUnsafe), sizeof(ScopedExecTS));
}

TEST(ScopedExecSizeTest, UnsafePolicyHasNoMutexOverhead) {
  // The mutex-free variant should be at most the size of fn_ + armed_ +
  // padding. We just verify it is strictly smaller, not an exact value.
  EXPECT_LT(sizeof(ScopedExecUnsafe), sizeof(ScopedExecTS));
}

class ScopedExecTSTest : public ::testing::Test {};

TEST_F(ScopedExecTSTest, ConcurrentInvokeFiresExactlyOnce) {
  constexpr int kThreads = 32;
  atomic<int> counter{0};
  ScopedExecTS exec([&counter] { ++counter; });
  vector<thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&exec] { exec.invoke(); });
  }
  for (auto &t : threads)
    t.join();
  EXPECT_EQ(counter.load(), 1);
}

TEST_F(ScopedExecTSTest, ConcurrentReleaseAndInvokeFiresAtMostOnce) {
  constexpr int kRounds = 1000;
  for (int r = 0; r < kRounds; ++r) {
    atomic<int> counter{0};
    ScopedExecTS exec([&counter] { ++counter; });
    thread t1([&exec] { exec.invoke(); });
    thread t2([&exec] { exec.release(); });
    t1.join();
    t2.join();
    EXPECT_LE(counter.load(), 1);
  }
}

TEST_F(ScopedExecTSTest, DestructorRacesInvokeFiresExactlyOnce) {
  constexpr int kRounds = 1000;
  for (int r = 0; r < kRounds; ++r) {
    atomic<int> counter{0};
    atomic<bool> go{false};
    {
      ScopedExecTS exec([&counter] { ++counter; });
      thread t([&exec, &go] {
        while (!go.load(memory_order_acquire)) {
        }
        exec.invoke();
      });
      go.store(true, memory_order_release);
      exec.invoke();
      t.join();
    }
    EXPECT_EQ(counter.load(), 1);
  }
}

TEST_F(ScopedExecTSTest, ConcurrentResetFiresOriginalFunctorExactlyOnce) {
  constexpr int kRounds = 500;
  for (int r = 0; r < kRounds; ++r) {
    atomic<int> orig_fires{0};
    atomic<int> repl_fires{0};
    ScopedExecTS exec([&orig_fires] { ++orig_fires; });
    thread t1(
      [&exec, &repl_fires] { exec.reset([&repl_fires] { ++repl_fires; }); });
    thread t2(
      [&exec, &repl_fires] { exec.reset([&repl_fires] { ++repl_fires; }); });
    t1.join();
    t2.join();
    exec.release();
    EXPECT_EQ(orig_fires.load(), 1);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new ScopedExecEnvironment);
  return RUN_ALL_TESTS();
}
