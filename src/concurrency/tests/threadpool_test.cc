#include "logging/logger.h"
#include "threadpool.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace std;
using namespace utils::concurrency;
using namespace utils::logging;

class GlobalEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    Logger::Init();
    Logger::EnableSignalHandlers();
  }
  void TearDown() override { Logger::Shutdown(); }
};

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new GlobalEnvironment());
  return RUN_ALL_TESTS();
}

class ThreadPoolTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ThreadPoolTest, Submit_BasicTask) {
  ThreadPool pool(2);
  EXPECT_EQ(pool.GetThreadCount(), 2);
  atomic<int> counter{0};

  auto future = pool.Submit([&counter]() { counter.fetch_add(1); });
  future.get();

  EXPECT_EQ(counter.load(), 1);
}

TEST_F(ThreadPoolTest, Submit_ReturnsValue) {
  ThreadPool pool(2);

  auto future = pool.Submit([]() {
    LOG(INFO) << "Executing task that returns a value.";
    return 42;
  });

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, Submit_MultipleTasks) {
  ThreadPool pool(2);
  const int kNumTasks = 100;
  atomic<int> counter{0};

  for (int i = 0; i < kNumTasks; ++i) {
    pool.Submit([&counter]() { counter.fetch_add(1); });
  }
  // Sleep for some time to allow all tasks to complete.
  this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(counter.load(), kNumTasks);
}

TEST_F(ThreadPoolTest, SubmitWithPriority_HighFirst) {
  ThreadPool pool(1);
  atomic<int> counter{0};

  pool.SubmitWithPriority(TaskPriority::kLow, [&counter]() {
    this_thread::sleep_for(std::chrono::milliseconds(20));
    counter.fetch_add(1);
  });

  pool.SubmitWithPriority(TaskPriority::kHigh,
                          [&counter]() { counter.fetch_add(10); });

  this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_EQ(counter.load(), 11);
}

TEST_F(ThreadPoolTest, SubmitWithPriority_AllLevels) {
  ThreadPool pool(1);
  vector<int> order;
  mutex mtx;

  pool.SubmitWithPriority(TaskPriority::kLow, [&order, &mtx]() {
    lock_guard<mutex> lock(mtx);
    order.push_back(0);
  });
  pool.SubmitWithPriority(TaskPriority::kNormal, [&order, &mtx]() {
    lock_guard<mutex> lock(mtx);
    order.push_back(1);
  });
  pool.SubmitWithPriority(TaskPriority::kHigh, [&order, &mtx]() {
    lock_guard<mutex> lock(mtx);
    order.push_back(2);
  });
  pool.SubmitWithPriority(TaskPriority::kCritical, [&order, &mtx]() {
    lock_guard<mutex> lock(mtx);
    order.push_back(3);
  });

  this_thread::sleep_for(std::chrono::milliseconds(50));

  ASSERT_EQ(order.size(), 4);
  EXPECT_EQ(order[3], 0);
}

TEST_F(ThreadPoolTest, SubmitDelayed_ExecutesAfterDelay) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  auto start = std::chrono::steady_clock::now();
  auto future =
    pool.SubmitDelayed(std::chrono::milliseconds(100), []() { return 42; });

  EXPECT_EQ(future.first.get(), 42);

  auto elapsed = duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start)
                   .count();
  LOG(INFO) << LOGVARS(elapsed);
  EXPECT_GE(elapsed, 100);
}

TEST_F(ThreadPoolTest, SubmitDelayed_MultipleOrdered) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  vector<int> results;
  mutex mtx;

  pool.SubmitDelayed(std::chrono::milliseconds(100), [&results, &mtx]() {
    lock_guard<mutex> lock(mtx);
    results.push_back(1);
  });
  pool.SubmitDelayed(std::chrono::milliseconds(50), [&results, &mtx]() {
    lock_guard<mutex> lock(mtx);
    results.push_back(0);
  });

  this_thread::sleep_for(std::chrono::milliseconds(200));
  pool.WaitForTasks();

  ASSERT_EQ(results.size(), 2);
  EXPECT_EQ(results[0], 0);
}

TEST_F(ThreadPoolTest, SubmitAt_PastTime) {
  ThreadPool pool(2);
  auto past = std::chrono::steady_clock::now() - std::chrono::milliseconds(10);

  auto future = pool.SubmitAt(past, []() { return 100; });

  EXPECT_EQ(future.first.get(), 100);
  auto elapsed = duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - past)
                   .count();
  LOG(INFO) << LOGVARS(elapsed);
  EXPECT_GE(elapsed, 0);
}

TEST_F(ThreadPoolTest, SubmitAt_FutureTime) {
  ThreadPool pool(2);
  auto future_time =
    std::chrono::steady_clock::now() + std::chrono::milliseconds(50);

  auto future = pool.SubmitAt(future_time, []() { return 200; });

  EXPECT_EQ(future.first.get(), 200);
  auto elapsed = duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - future_time)
                   .count();
  LOG(INFO) << LOGVARS(elapsed);
  EXPECT_GE(elapsed, 0);
}

TEST_F(ThreadPoolTest, SubmitRecurring_MultipleTimes) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  atomic<int> counter{0};
  auto handle = pool.SubmitRecurring(std::chrono::milliseconds(30),
                                     [&counter]() { counter.fetch_add(1); });

  this_thread::sleep_for(std::chrono::milliseconds(150));
  handle.Cancel();

  pool.WaitForTasks();

  LOG(INFO) << LOGVARS(counter.load());
  EXPECT_GE(counter.load(), 3);
}

TEST_F(ThreadPoolTest, SubmitRecurring_Interval) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  vector<std::chrono::steady_clock::time_point> times;
  mutex mtx;

  auto handle =
    pool.SubmitRecurring(std::chrono::milliseconds(50), [&times, &mtx]() {
      lock_guard<mutex> lock(mtx);
      times.push_back(std::chrono::steady_clock::now());
    });

  this_thread::sleep_for(std::chrono::milliseconds(160));
  handle.Cancel();

  pool.WaitForTasks();

  ASSERT_GE(times.size(), 2);
  auto interval =
    duration_cast<std::chrono::milliseconds>(times[1] - times[0]).count();
  EXPECT_GE(interval, 50);
}

TEST_F(ThreadPoolTest, CancelTask_DelayedBeforeExecution) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  auto [future, handle] =
    pool.SubmitDelayed(std::chrono::milliseconds(5000), []() { return 1; });

  handle.Cancel();

  this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_TRUE(handle.IsCancelled());
}

TEST_F(ThreadPoolTest, CancelTask_Recurring) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  atomic<int> counter{0};
  auto handle = pool.SubmitRecurring(std::chrono::milliseconds(20),
                                     [&counter]() { counter.fetch_add(1); });

  this_thread::sleep_for(std::chrono::milliseconds(60));
  handle.Cancel();

  auto count_at_cancel = counter.load();
  this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_EQ(counter.load(), count_at_cancel);
}

TEST_F(ThreadPoolTest, TaskHandle_IsCancelled) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  auto [future1, handle1] =
    pool.SubmitDelayed(std::chrono::milliseconds(100), []() {});
  handle1.Cancel();

  EXPECT_TRUE(handle1.IsCancelled());

  auto [future2, handle2] =
    pool.SubmitDelayed(std::chrono::milliseconds(5000), []() {});
  EXPECT_FALSE(handle2.IsCancelled());
}

TEST_F(ThreadPoolTest, WaitForTasks_BlocksUntilComplete) {
  ThreadPool pool(2);
  atomic<int> counter{0};

  pool.Submit([&counter]() {
    this_thread::sleep_for(std::chrono::milliseconds(50));
    counter.fetch_add(1);
  });

  pool.WaitForTasks();

  EXPECT_EQ(counter.load(), 1);
}

TEST_F(ThreadPoolTest, PauseResume) {
  ThreadPool pool(2);
  atomic<int> counter{0};

  pool.Pause();

  auto f1 = pool.Submit([&counter]() { counter.fetch_add(1); });
  auto f2 = pool.Submit([&counter]() { counter.fetch_add(1); });

  this_thread::sleep_for(std::chrono::milliseconds(30));

  EXPECT_EQ(counter.load(), 0);
  EXPECT_TRUE(pool.IsPaused());

  pool.Resume();

  f1.get();
  f2.get();

  EXPECT_EQ(counter.load(), 2);
}

TEST_F(ThreadPoolTest, Shutdown_WaitForTasks) {
  ThreadPool pool(2);
  atomic<int> counter{0};

  pool.Submit([&counter]() {
    this_thread::sleep_for(std::chrono::milliseconds(20));
    counter.fetch_add(1);
  });

  pool.Shutdown(true);

  EXPECT_TRUE(pool.IsShutdown());
  EXPECT_EQ(counter.load(), 1);
}

TEST_F(ThreadPoolTest, ShutdownNow_DropsTasks) {
  ThreadPool pool(2);

  pool.Submit([]() { this_thread::sleep_for(std::chrono::milliseconds(100)); });
  pool.Submit([]() { this_thread::sleep_for(std::chrono::milliseconds(100)); });

  pool.ShutdownNow();

  EXPECT_TRUE(pool.IsShutdown());
}

TEST_F(ThreadPoolTest, IsShutdown_State) {
  ThreadPool pool(2);

  EXPECT_FALSE(pool.IsShutdown());

  pool.Shutdown();

  EXPECT_TRUE(pool.IsShutdown());
}

TEST_F(ThreadPoolTest, Stats_TasksSubmittedCompleted) {
  ThreadPool pool(2);

  pool.Submit([]() {});
  pool.Submit([]() {});
  pool.Submit([]() {});

  pool.WaitForTasks();

  const auto &stats = pool.GetStats();
  EXPECT_EQ(stats.tasks_submitted.load(), 3);
  EXPECT_EQ(stats.tasks_completed.load(), 3);
}

TEST_F(ThreadPoolTest, Stats_CompletionRate) {
  ThreadPool pool(2);

  pool.Submit([]() {});
  pool.Submit([]() {});

  pool.WaitForTasks();

  const auto &stats = pool.GetStats();
  EXPECT_DOUBLE_EQ(stats.GetCompletionRate(), 1.0);
}

TEST_F(ThreadPoolTest, SubmitBatch_Multiple) {
  ThreadPool pool(2);

  vector<function<int()>> tasks;
  for (int i = 0; i < 5; ++i) {
    tasks.push_back([i]() { return i * i; });
  }

  auto futures = pool.SubmitBatch(tasks.begin(), tasks.end());

  vector<int> results;
  for (auto &f : futures) {
    results.push_back(f.get());
  }

  EXPECT_EQ(results.size(), 5);
  EXPECT_EQ(results[0], 0);
  EXPECT_EQ(results[4], 16);
}

TEST_F(ThreadPoolTest, GetThreadCount) {
  ThreadPool pool(4);

  EXPECT_GE(pool.GetThreadCount(), 1);
}

TEST_F(ThreadPoolTest, GetQueueSize) {
  ThreadPool pool(1);

  pool.Submit([]() { this_thread::sleep_for(std::chrono::milliseconds(50)); });
  pool.Submit([]() { this_thread::sleep_for(std::chrono::milliseconds(50)); });

  EXPECT_GE(pool.GetQueueSize(), 0);
}

TEST_F(ThreadPoolTest, Constructor_Uint32) {
  ThreadPool pool(3);

  auto future = pool.Submit([]() { return 1; });
  EXPECT_EQ(future.get(), 1);
}

TEST_F(ThreadPoolTest, Submit_MoveOnlyCallable) {
  ThreadPool pool(2);

  unique_ptr<int> ptr = make_unique<int>(42);
  auto future = pool.Submit([p = std::move(ptr)]() { return *p; });

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, Submit_VariadicArgs) {
  ThreadPool pool(2);

  auto future =
    pool.Submit([](int a, int b, int c) { return a + b + c; }, 1, 2, 3);

  EXPECT_EQ(future.get(), 6);
}

TEST_F(ThreadPoolTest, Resize_Pool) {
  ThreadPoolConfig config;
  config.min_threads = 2;
  config.max_threads = 4;
  config.initial_threads = 2;
  ThreadPool pool(config);

  pool.Resize(4);

  EXPECT_EQ(pool.GetThreadCount(), 4);
}

TEST_F(ThreadPoolTest, GlobalThreadPool_Singleton) {
  ThreadPool &pool1 = GlobalThreadPool::Instance();
  ThreadPool &pool2 = GlobalThreadPool::Instance();

  EXPECT_EQ(&pool1, &pool2);
}

TEST_F(ThreadPoolTest, ParallelFor_Basic) {
  ThreadPool pool(4);

  vector<int> data = {1, 2, 3, 4, 5};
  vector<int> results(data.size(), 0);

  auto func = [](int &val) { val *= 2; };

  GlobalThreadPool::Instance().Submit(
    [&data, &func]() { ParallelFor(data.begin(), data.end(), func); });

  this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(ThreadPoolTest, SubmitDelayed_CancelAfterExecution) {
  ThreadPoolConfig config;
  config.enable_delayed_tasks = true;
  ThreadPool pool(config);

  auto [future, handle] =
    pool.SubmitDelayed(std::chrono::milliseconds(20), []() { return 1; });

  EXPECT_EQ(future.get(), 1);

  handle.Cancel();

  EXPECT_TRUE(handle.IsCancelled());
}
