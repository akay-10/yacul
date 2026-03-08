#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "i_qos_item.h"
#include "qos.h"
#include "qos_stats.h"
#include "token_bucket.h"

using namespace utils::qos;
using namespace std::chrono_literals;

// ===========================================================================
// Test helpers
// ===========================================================================

namespace {

// ---------------------------------------------------------------------------
// CountingItem — records execution and expiry; useful for sequencing tests.
// ---------------------------------------------------------------------------
class CountingItem final : public IQosItem {
public:
  explicit CountingItem(std::atomic<int> &counter, std::string tag = {})
      : counter_(counter) {
    metadata_.tag = std::move(tag);
  }

  void Execute() override { counter_.fetch_add(1, std::memory_order_relaxed); }

  std::string Describe() const override {
    return "CountingItem[tag=" + metadata_.tag + "]";
  }

private:
  std::atomic<int> &counter_;
};

// ---------------------------------------------------------------------------
// ExpiredItem — always reports itself as expired.
// ---------------------------------------------------------------------------
class ExpiredItem final : public IQosItem {
public:
  explicit ExpiredItem(std::atomic<int> &counter) : counter_(counter) {}

  void Execute() override { counter_.fetch_add(1, std::memory_order_relaxed); }
  bool IsExpired() const override { return true; }
  std::string Describe() const override { return "ExpiredItem"; }

private:
  std::atomic<int> &counter_;
};

// ---------------------------------------------------------------------------
// CostlyItem — returns a configurable cost.
// ---------------------------------------------------------------------------
class CostlyItem final : public IQosItem {
public:
  CostlyItem(uint32_t cost, std::atomic<int> &counter)
      : cost_(cost), counter_(counter) {}

  void Execute() override { counter_.fetch_add(1, std::memory_order_relaxed); }
  uint32_t Cost() const override { return cost_; }
  std::string Describe() const override { return "CostlyItem"; }

private:
  uint32_t cost_;
  std::atomic<int> &counter_;
};

// ---------------------------------------------------------------------------
// RecordingObserver — captures lifecycle events for assertions.
// ---------------------------------------------------------------------------
class RecordingObserver final : public IQosObserver {
public:
  void OnEnqueued(const QosMetadata &) override {
    enqueued.fetch_add(1, std::memory_order_relaxed);
  }
  void OnDequeued(const QosMetadata &) override {
    dequeued.fetch_add(1, std::memory_order_relaxed);
  }
  void OnExecuted(const QosMetadata &, std::chrono::microseconds) override {
    executed.fetch_add(1, std::memory_order_relaxed);
  }
  void OnDropped(const QosMetadata &) override {
    dropped.fetch_add(1, std::memory_order_relaxed);
  }
  void OnExpired(const QosMetadata &) override {
    expired.fetch_add(1, std::memory_order_relaxed);
  }

  std::atomic<int> enqueued{0};
  std::atomic<int> dequeued{0};
  std::atomic<int> executed{0};
  std::atomic<int> dropped{0};
  std::atomic<int> expired{0};
};

// ---------------------------------------------------------------------------
// Utility: spin-wait up to `timeout` for `cond()` to become true.
// ---------------------------------------------------------------------------
template <typename Pred>
bool WaitFor(Pred cond, std::chrono::milliseconds timeout = 2000ms) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!cond()) {
    if (std::chrono::steady_clock::now() >= deadline)
      return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

} // namespace

// ===========================================================================
// Global test environment — prints a banner.
// ===========================================================================

class QosTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { std::cout << "=== QOS test suite starting ===\n"; }
  void TearDown() override { std::cout << "=== QOS test suite finished ===\n"; }
};

// ===========================================================================
// Fixture: basic QOS with default config, manual consumer mode.
// ===========================================================================

class QosBasicTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.max_queue_depth = 64;
    cfg.num_consumer_threads = 0; // manual dequeue
    qos_ = std::make_unique<QOS>(cfg);
  }

  std::unique_ptr<QOS> qos_;
  std::atomic<int> counter_{0};
};

// ---------------------------------------------------------------------------

TEST_F(QosBasicTest, EnqueueAndManualDequeue) {
  auto item = std::make_shared<CountingItem>(counter_);
  ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));

  EXPECT_EQ(qos_->SizeApprox(), 1u);

  auto dequeued = qos_->TryDequeue();
  ASSERT_NE(dequeued, nullptr);
  dequeued->Execute();

  EXPECT_EQ(counter_.load(), 1);
  EXPECT_EQ(qos_->SizeApprox(), 0u);
}

// ---------------------------------------------------------------------------

TEST_F(QosBasicTest, PriorityOrdering) {
  // Enqueue one item at each priority; dequeue should give highest first.
  std::vector<Priority> order;

  for (int i = 0; i < static_cast<int>(kNumPriorityLevels); ++i) {
    auto p = static_cast<Priority>(i);
    std::atomic<int> dummy{0};
    auto item = std::make_shared<CountingItem>(dummy);
    ASSERT_TRUE(qos_->Enqueue(item, p));
  }

  while (auto item = qos_->TryDequeue()) {
    order.push_back(item->metadata().priority);
  }

  // Highest priority should come first.
  for (size_t i = 1; i < order.size(); ++i) {
    EXPECT_GE(static_cast<int>(order[i - 1]), static_cast<int>(order[i]));
  }
}

// ---------------------------------------------------------------------------

TEST_F(QosBasicTest, QueueDepthTracking) {
  const int kItems = 10;
  for (int i = 0; i < kItems; ++i) {
    auto item = std::make_shared<CountingItem>(counter_);
    ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));
  }
  EXPECT_EQ(qos_->SizeApprox(), static_cast<size_t>(kItems));

  for (int i = 0; i < kItems; ++i) {
    ASSERT_NE(qos_->TryDequeue(), nullptr);
  }
  EXPECT_EQ(qos_->SizeApprox(), 0u);
}

// ---------------------------------------------------------------------------

TEST_F(QosBasicTest, DequeueEmptyReturnsNull) {
  EXPECT_EQ(qos_->TryDequeue(), nullptr);
}

// ---------------------------------------------------------------------------

TEST_F(QosBasicTest, BulkDequeue) {
  const int kItems = 20;
  for (int i = 0; i < kItems; ++i) {
    auto item = std::make_shared<CountingItem>(counter_);
    ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));
  }

  std::vector<IQosItemPtr> out;
  size_t n = qos_->TryDequeueBulk(out, 10);
  EXPECT_EQ(n, 10u);
  EXPECT_EQ(qos_->SizeApprox(), 10u);
}

// ---------------------------------------------------------------------------

TEST_F(QosBasicTest, MetadataIdIsUnique) {
  std::vector<QosItemId> ids;
  for (int i = 0; i < 50; ++i) {
    auto item = std::make_shared<CountingItem>(counter_);
    ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));
    ids.push_back(item->metadata().id);
  }
  std::sort(ids.begin(), ids.end());
  auto it = std::unique(ids.begin(), ids.end());
  EXPECT_EQ(it, ids.end()); // no duplicates
}

// ===========================================================================
// Fixture: overflow / admission tests
// ===========================================================================

class QosOverflowTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.max_queue_depth = 4;
    cfg.overflow_policy = QosConfig::OverflowPolicy::kDrop;
    cfg.num_consumer_threads = 0;
    qos_ = std::make_unique<QOS>(cfg);
  }

  std::unique_ptr<QOS> qos_;
  std::atomic<int> counter_{0};
};

// ---------------------------------------------------------------------------

TEST_F(QosOverflowTest, DropsWhenFull) {
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(qos_->Enqueue(std::make_shared<CountingItem>(counter_),
                              Priority::kNormal));
  }
  // 5th item should be dropped.
  bool ok = qos_->Enqueue(std::make_shared<CountingItem>(counter_),
                          Priority::kNormal);
  EXPECT_FALSE(ok);
  EXPECT_EQ(qos_->Stats().TotalDropped(), 1u);
}

// ---------------------------------------------------------------------------

TEST_F(QosOverflowTest, DropOldestPolicy) {
  QosConfig cfg;
  cfg.max_queue_depth = 3;
  cfg.overflow_policy = QosConfig::OverflowPolicy::kDropOldest;
  cfg.num_consumer_threads = 0;
  qos_ = std::make_unique<QOS>(cfg);

  for (int i = 0; i < 4; ++i) {
    qos_->Enqueue(std::make_shared<CountingItem>(counter_), Priority::kNormal);
  }
  // Queue should still be size 3 (oldest evicted).
  EXPECT_EQ(qos_->SizeApprox(Priority::kNormal), 3u);
  EXPECT_EQ(qos_->Stats().TotalDropped(), 1u);
}

// ---------------------------------------------------------------------------

// AFTER
TEST_F(QosOverflowTest, BackPressureSignal) {
  QosConfig cfg;
  cfg.max_queue_depth = 10;
  cfg.backpressure_threshold = 0.5;
  cfg.overflow_policy = QosConfig::OverflowPolicy::kDropOldest;
  cfg.num_consumer_threads = 0;
  qos_ = std::make_unique<QOS>(cfg);

  EXPECT_FALSE(qos_->IsUnderBackPressure());

  // Fill the kNormal queue past the 50% threshold (need > 5 of 10 slots).
  for (int i = 0; i < 8; ++i) {
    qos_->Enqueue(std::make_shared<CountingItem>(counter_), Priority::kNormal);
  }
  EXPECT_TRUE(qos_->IsUnderBackPressure());
}
// ===========================================================================
// Fixture: TTL / expiry tests
// ===========================================================================

class QosTtlTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.item_ttl = 50ms;
    cfg.num_consumer_threads = 0;
    qos_ = std::make_unique<QOS>(cfg);
  }

  std::unique_ptr<QOS> qos_;
  std::atomic<int> counter_{0};
};

// ---------------------------------------------------------------------------

TEST_F(QosTtlTest, ExpiredItemIsDiscarded) {
  auto item = std::make_shared<CountingItem>(counter_);
  ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));

  std::this_thread::sleep_for(100ms); // exceed TTL

  auto dequeued = qos_->TryDequeue();
  EXPECT_EQ(dequeued, nullptr);
  EXPECT_EQ(counter_.load(), 0);
  EXPECT_EQ(qos_->Stats().TotalExpired(), 1u);
}

// ---------------------------------------------------------------------------

TEST_F(QosTtlTest, NonExpiredItemDequeued) {
  auto item = std::make_shared<CountingItem>(counter_);
  ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));

  auto dequeued = qos_->TryDequeue();
  ASSERT_NE(dequeued, nullptr);
  dequeued->Execute();
  EXPECT_EQ(counter_.load(), 1);
}

// ---------------------------------------------------------------------------

TEST_F(QosTtlTest, DeadlineExpiry) {
  QosConfig cfg;
  cfg.deadline_scheduling = true;
  cfg.num_consumer_threads = 0;
  qos_ = std::make_unique<QOS>(cfg);

  auto past_deadline = std::chrono::steady_clock::now() - 1ms;
  auto item = std::make_shared<CountingItem>(counter_);
  ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal, past_deadline));

  auto dequeued = qos_->TryDequeue();
  EXPECT_EQ(dequeued, nullptr);
  EXPECT_EQ(qos_->Stats().TotalExpired(), 1u);
}

// ---------------------------------------------------------------------------

TEST_F(QosTtlTest, IQosItemIsExpiredOverride) {
  auto item = std::make_shared<ExpiredItem>(counter_);
  ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));

  auto dequeued = qos_->TryDequeue();
  EXPECT_EQ(dequeued, nullptr);
  EXPECT_EQ(counter_.load(), 0);
}

// ===========================================================================
// Fixture: observer tests
// ===========================================================================

class QosObserverTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.max_queue_depth = 8;
    cfg.overflow_policy = QosConfig::OverflowPolicy::kDrop;
    cfg.num_consumer_threads = 0;
    qos_ = std::make_unique<QOS>(cfg);
    observer_ = std::make_shared<RecordingObserver>();
    qos_->AddObserver(observer_);
  }

  std::unique_ptr<QOS> qos_;
  std::shared_ptr<RecordingObserver> observer_;
  std::atomic<int> counter_{0};
};

// ---------------------------------------------------------------------------

TEST_F(QosObserverTest, EnqueuedCallbackFired) {
  auto item = std::make_shared<CountingItem>(counter_);
  qos_->Enqueue(item, Priority::kNormal);
  EXPECT_EQ(observer_->enqueued.load(), 1);
}

// ---------------------------------------------------------------------------

TEST_F(QosObserverTest, DequeuedCallbackFired) {
  auto item = std::make_shared<CountingItem>(counter_);
  qos_->Enqueue(item, Priority::kNormal);
  qos_->TryDequeue();
  EXPECT_EQ(observer_->dequeued.load(), 1);
}

// ---------------------------------------------------------------------------

TEST_F(QosObserverTest, DroppedCallbackFired) {
  for (int i = 0; i < 9; ++i) {
    qos_->Enqueue(std::make_shared<CountingItem>(counter_), Priority::kNormal);
  }
  EXPECT_GE(observer_->dropped.load(), 1);
}

// ---------------------------------------------------------------------------

TEST_F(QosObserverTest, ObserverRemoval) {
  qos_->RemoveObserver(observer_);
  auto item = std::make_shared<CountingItem>(counter_);
  qos_->Enqueue(item, Priority::kNormal);
  EXPECT_EQ(observer_->enqueued.load(), 0);
}

// ===========================================================================
// Fixture: consumer thread tests
// ===========================================================================

class QosConsumerTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.num_consumer_threads = 2;
    qos_ = std::make_unique<QOS>(cfg);
  }

  void TearDown() override { qos_->Stop(); }

  std::unique_ptr<QOS> qos_;
  std::atomic<int> counter_{0};
};

// ---------------------------------------------------------------------------

TEST_F(QosConsumerTest, ItemsExecutedByConsumers) {
  qos_->Start();

  const int kItems = 100;
  for (int i = 0; i < kItems; ++i) {
    auto item = std::make_shared<CountingItem>(counter_);
    ASSERT_TRUE(qos_->Enqueue(item, Priority::kNormal));
  }

  ASSERT_TRUE(WaitFor([&] { return counter_.load() == kItems; }));
  EXPECT_EQ(counter_.load(), kItems);
}

// ---------------------------------------------------------------------------

TEST_F(QosConsumerTest, PauseStopsExecution) {
  qos_->Start();
  qos_->Pause();

  const int kItems = 10;
  for (int i = 0; i < kItems; ++i) {
    auto item = std::make_shared<CountingItem>(counter_);
    qos_->Enqueue(item, Priority::kNormal);
  }

  std::this_thread::sleep_for(50ms);
  EXPECT_EQ(counter_.load(), 0); // nothing executed while paused

  qos_->Resume();
  ASSERT_TRUE(WaitFor([&] { return counter_.load() == kItems; }));
}

// ---------------------------------------------------------------------------

TEST_F(QosConsumerTest, StartStopIdempotent) {
  qos_->Start();
  qos_->Start(); // second call is a no-op
  EXPECT_TRUE(qos_->IsRunning());

  qos_->Stop();
  qos_->Stop(); // second call is a no-op
  EXPECT_FALSE(qos_->IsRunning());
}

// ===========================================================================
// Fixture: EnqueueTask (FunctionItem) tests
// ===========================================================================

class QosTaskTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.num_consumer_threads = 1;
    qos_ = std::make_unique<QOS>(cfg);
    qos_->Start();
  }

  void TearDown() override { qos_->Stop(); }

  std::unique_ptr<QOS> qos_;
};

// ---------------------------------------------------------------------------

TEST_F(QosTaskTest, LambdaExecuted) {
  std::atomic<int> flag{0};
  qos_->EnqueueTask([&flag] { flag.store(1); }, Priority::kHigh, "test-task");
  ASSERT_TRUE(WaitFor([&] { return flag.load() == 1; }));
}

// ---------------------------------------------------------------------------

TEST_F(QosTaskTest, MultipleTasksOrdered) {
  std::vector<int> order;
  std::mutex mu;

  // Use critical priority for deterministic ordering.
  for (int i = 0; i < 5; ++i) {
    int val = i;
    qos_->EnqueueTask(
        [&order, &mu, val] {
          std::lock_guard<std::mutex> lk(mu);
          order.push_back(val);
        },
        Priority::kCritical);
  }

  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lk(mu);
    return order.size() == 5u;
  }));
  // All 5 must have executed (order within same priority is FIFO).
  EXPECT_EQ(order.size(), 5u);
}

// ===========================================================================
// Fixture: Token bucket tests
// ===========================================================================

class TokenBucketTest : public ::testing::Test {};

// ---------------------------------------------------------------------------

TEST_F(TokenBucketTest, InitiallyFull) {
  TokenBucket tb(100.0, 100.0);
  EXPECT_NEAR(tb.CurrentTokens(), 100.0, 1.0);
}

// ---------------------------------------------------------------------------

TEST_F(TokenBucketTest, ConsumeSucceeds) {
  TokenBucket tb(1000.0, 100.0);
  EXPECT_TRUE(tb.TryConsume(50.0));
  EXPECT_NEAR(tb.CurrentTokens(), 50.0, 2.0);
}

// ---------------------------------------------------------------------------

TEST_F(TokenBucketTest, ConsumeFailsWhenEmpty) {
  TokenBucket tb(1.0, 1.0);
  ASSERT_TRUE(tb.TryConsume(1.0));
  EXPECT_FALSE(tb.TryConsume(1.0));
}

// ---------------------------------------------------------------------------

TEST_F(TokenBucketTest, RefillsOverTime) {
  TokenBucket tb(1000.0, 100.0); // 1000 tokens/s
  tb.TryConsume(100.0);          // drain

  std::this_thread::sleep_for(50ms); // 0.05 s × 1000 = 50 tokens expected

  EXPECT_TRUE(tb.TryConsume(20.0));
}

// ---------------------------------------------------------------------------

TEST_F(TokenBucketTest, ResetRestoresFull) {
  TokenBucket tb(100.0, 100.0);
  tb.TryConsume(90.0);
  tb.Reset();
  EXPECT_NEAR(tb.CurrentTokens(), 100.0, 1.0);
}

// ===========================================================================
// Fixture: Statistics tests
// ===========================================================================

class QosStatsTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.num_consumer_threads = 0;
    qos_ = std::make_unique<QOS>(cfg);
  }

  std::unique_ptr<QOS> qos_;
  std::atomic<int> counter_{0};
};

// ---------------------------------------------------------------------------

TEST_F(QosStatsTest, EnqueueCountAccumulates) {
  for (int i = 0; i < 5; ++i) {
    qos_->Enqueue(std::make_shared<CountingItem>(counter_), Priority::kLow);
  }
  EXPECT_EQ(qos_->Stats().TotalEnqueued(), 5u);
}

// ---------------------------------------------------------------------------

TEST_F(QosStatsTest, DequeueCountAccumulates) {
  for (int i = 0; i < 3; ++i) {
    qos_->Enqueue(std::make_shared<CountingItem>(counter_), Priority::kNormal);
  }
  for (int i = 0; i < 3; ++i)
    qos_->TryDequeue();
  EXPECT_EQ(qos_->Stats().TotalDequeued(), 3u);
}

// ---------------------------------------------------------------------------

TEST_F(QosStatsTest, ResetClearsCounters) {
  qos_->Enqueue(std::make_shared<CountingItem>(counter_), Priority::kNormal);
  qos_->ResetStats();
  EXPECT_EQ(qos_->Stats().TotalEnqueued(), 0u);
}

// ---------------------------------------------------------------------------

TEST_F(QosStatsTest, ToStringNonEmpty) {
  qos_->Enqueue(std::make_shared<CountingItem>(counter_), Priority::kHigh);
  std::string s = qos_->Stats().ToString();
  EXPECT_FALSE(s.empty());
  EXPECT_NE(s.find("QosStats"), std::string::npos);
}

// ===========================================================================
// Fixture: concurrent enqueue / dequeue stress test
// ===========================================================================

class QosConcurrencyTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.max_queue_depth = 0; // unlimited
    cfg.num_consumer_threads = 0;
    qos_ = std::make_unique<QOS>(cfg);
  }

  std::unique_ptr<QOS> qos_;
};

// ---------------------------------------------------------------------------

TEST_F(QosConcurrencyTest, ConcurrentEnqueueDequeue) {
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  const int kItems = 2000;

  // 4 producer threads.
  std::vector<std::thread> producers;
  for (int t = 0; t < 4; ++t) {
    producers.emplace_back([&] {
      for (int i = 0; i < kItems / 4; ++i) {
        std::atomic<int> dummy{0};
        auto item = std::make_shared<CountingItem>(dummy);
        if (qos_->Enqueue(item, Priority::kNormal)) {
          produced.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  // 2 consumer threads.
  std::vector<std::thread> consumers;
  std::atomic<bool> stop_consumers{false};
  for (int t = 0; t < 2; ++t) {
    consumers.emplace_back([&] {
      while (!stop_consumers.load(std::memory_order_acquire)) {
        auto item = qos_->TryDequeue();
        if (item)
          consumed.fetch_add(1, std::memory_order_relaxed);
        else
          std::this_thread::yield();
      }
      // Drain remainder.
      while (auto item = qos_->TryDequeue()) {
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto &t : producers)
    t.join();
  stop_consumers.store(true, std::memory_order_release);
  for (auto &t : consumers)
    t.join();

  EXPECT_EQ(produced.load(), consumed.load());
}

// ===========================================================================
// Fixture: Rate-limited QOS tests
// ===========================================================================

class QosRateLimitTest : public ::testing::Test {
protected:
  void SetUp() override {
    QosConfig cfg;
    cfg.max_queue_depth = 0;
    // Allow only 5 tokens/s on Normal priority.
    cfg.rate_limit_per_priority[static_cast<size_t>(Priority::kNormal)] = 5.0;
    cfg.burst_multiplier = 1.0; // burst == rate
    cfg.num_consumer_threads = 0;
    qos_ = std::make_unique<QOS>(cfg);
  }

  std::unique_ptr<QOS> qos_;
  std::atomic<int> counter_{0};
};

// ---------------------------------------------------------------------------

TEST_F(QosRateLimitTest, EnqueueBeyondBurstIsDropped) {
  int accepted = 0;
  for (int i = 0; i < 20; ++i) {
    auto item = std::make_shared<CountingItem>(counter_);
    if (qos_->Enqueue(item, Priority::kNormal))
      ++accepted;
  }
  // At burst=5, only up to 5 can be accepted instantly.
  EXPECT_LE(accepted, 5);
  EXPECT_GE(qos_->Stats().TotalDropped(), 15u);
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new QosTestEnvironment());
  return RUN_ALL_TESTS();
}
