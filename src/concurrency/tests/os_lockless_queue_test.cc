#include "os_lockless_queue.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using moodycamel::ConcurrentQueue;
using namespace std;

class OsLocklessQueueTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(OsLocklessQueueTest, DefaultConstruction) {
  ConcurrentQueue<int> queue;
  EXPECT_GE(queue.size_approx(), 0);
}

TEST_F(OsLocklessQueueTest, EnqueueDequeueSingle) {
  ConcurrentQueue<int> queue(10);

  EXPECT_TRUE(queue.enqueue(42));

  int value = 0;
  EXPECT_TRUE(queue.try_dequeue(value));
  EXPECT_EQ(value, 42);
}

TEST_F(OsLocklessQueueTest, EnqueueDequeueMultiple) {
  ConcurrentQueue<int> queue(100);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue.enqueue(i));
  }

  for (int i = 0; i < 10; ++i) {
    int value = 0;
    EXPECT_TRUE(queue.try_dequeue(value));
    EXPECT_EQ(value, i);
  }
}

TEST_F(OsLocklessQueueTest, EnqueueMove) {
  ConcurrentQueue<string> queue(10);

  string s = "hello";
  EXPECT_TRUE(queue.enqueue(move(s)));

  string result;
  EXPECT_TRUE(queue.try_dequeue(result));
  EXPECT_EQ(result, "hello");
}

TEST_F(OsLocklessQueueTest, TryEnqueue) {
  ConcurrentQueue<int> queue(2);

  EXPECT_TRUE(queue.try_enqueue(1));
  EXPECT_TRUE(queue.try_enqueue(2));
}

TEST_F(OsLocklessQueueTest, TryDequeueEmpty) {
  ConcurrentQueue<int> queue(10);

  int value = 999;
  EXPECT_FALSE(queue.try_dequeue(value));
  EXPECT_EQ(value, 999);
}

TEST_F(OsLocklessQueueTest, SizeApprox) {
  ConcurrentQueue<int> queue(100);

  EXPECT_EQ(queue.size_approx(), 0);

  queue.enqueue(1);
  queue.enqueue(2);
  queue.enqueue(3);

  EXPECT_EQ(queue.size_approx(), 3);

  int value;
  queue.try_dequeue(value);
  queue.try_dequeue(value);

  EXPECT_EQ(queue.size_approx(), 1);
}

TEST_F(OsLocklessQueueTest, BulkEnqueueDequeue) {
  ConcurrentQueue<int> queue(100);

  vector<int> items = {1, 2, 3, 4, 5};
  EXPECT_TRUE(queue.enqueue_bulk(items.begin(), items.size()));

  vector<int> output(10);
  size_t count = queue.try_dequeue_bulk(output.begin(), 5);

  EXPECT_EQ(count, 5);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(output[i], i + 1);
  }
}

TEST_F(OsLocklessQueueTest, ProducerToken) {
  ConcurrentQueue<int> queue(100);

  moodycamel::ProducerToken producer(queue);

  EXPECT_TRUE(queue.enqueue(producer, 100));
  EXPECT_TRUE(queue.enqueue(producer, 200));

  int value;
  EXPECT_TRUE(queue.try_dequeue(value));
  EXPECT_EQ(value, 100);
  EXPECT_TRUE(queue.try_dequeue(value));
  EXPECT_EQ(value, 200);
}

TEST_F(OsLocklessQueueTest, ConsumerToken) {
  ConcurrentQueue<int> queue(100);

  queue.enqueue(1);
  queue.enqueue(2);
  queue.enqueue(3);

  moodycamel::ConsumerToken consumer(queue);

  int value;
  EXPECT_TRUE(queue.try_dequeue(consumer, value));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(queue.try_dequeue(consumer, value));
  EXPECT_EQ(value, 2);
}

TEST_F(OsLocklessQueueTest, ProducerAndConsumerTokens) {
  ConcurrentQueue<int> queue(100);

  moodycamel::ProducerToken producer(queue);
  moodycamel::ConsumerToken consumer(queue);

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(queue.enqueue(producer, i));
  }

  int count = 0;
  int value;
  while (queue.try_dequeue(consumer, value)) {
    ++count;
  }

  EXPECT_EQ(count, 100);
}

TEST_F(OsLocklessQueueTest, IsLockFree) {
  ConcurrentQueue<int> queue;
  EXPECT_TRUE(queue.is_lock_free());
}

TEST_F(OsLocklessQueueTest, MoveConstruction) {
  ConcurrentQueue<int> queue1(10);
  queue1.enqueue(1);
  queue1.enqueue(2);

  ConcurrentQueue<int> queue2 = move(queue1);

  int value;
  EXPECT_TRUE(queue2.try_dequeue(value));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(queue2.try_dequeue(value));
  EXPECT_EQ(value, 2);
}

TEST_F(OsLocklessQueueTest, Swap) {
  ConcurrentQueue<int> queue1(10);
  ConcurrentQueue<int> queue2(10);

  queue1.enqueue(1);
  queue2.enqueue(2);

  queue1.swap(queue2);

  int value;
  EXPECT_TRUE(queue1.try_dequeue(value));
  EXPECT_EQ(value, 2);
  EXPECT_TRUE(queue2.try_dequeue(value));
  EXPECT_EQ(value, 1);
}

TEST_F(OsLocklessQueueTest, MultiProducer) {
  ConcurrentQueue<int> queue(1000);
  const int num_producers = 4;
  const int items_per_producer = 100;

  vector<thread> producers;
  for (int t = 0; t < num_producers; ++t) {
    producers.emplace_back([&queue, t, items_per_producer]() {
      for (int i = 0; i < items_per_producer; ++i) {
        queue.enqueue(t * 1000 + i);
      }
    });
  }

  for (auto &th : producers) {
    th.join();
  }

  EXPECT_EQ(queue.size_approx(),
            static_cast<size_t>(num_producers * items_per_producer));

  atomic<int> count{0};
  vector<thread> consumers;
  for (int t = 0; t < num_producers; ++t) {
    consumers.emplace_back([&queue, &count]() {
      int value;
      while (queue.try_dequeue(value)) {
        count.fetch_add(1);
      }
    });
  }

  for (auto &th : consumers) {
    th.join();
  }

  EXPECT_EQ(count.load(), num_producers * items_per_producer);
}

TEST_F(OsLocklessQueueTest, MultiConsumer) {
  ConcurrentQueue<int> queue(1000);

  for (int i = 0; i < 100; ++i) {
    queue.enqueue(i);
  }

  atomic<int> total{0};
  const int num_consumers = 4;

  vector<thread> consumers;
  for (int t = 0; t < num_consumers; ++t) {
    consumers.emplace_back([&queue, &total]() {
      int value;
      while (queue.try_dequeue(value)) {
        total.fetch_add(1);
      }
    });
  }

  for (auto &th : consumers) {
    th.join();
  }

  EXPECT_EQ(total.load(), 100);
}

TEST_F(OsLocklessQueueTest, StringQueue) {
  ConcurrentQueue<string> queue(10);

  queue.enqueue("hello");
  queue.enqueue("world");
  queue.enqueue("test");

  string value;
  EXPECT_TRUE(queue.try_dequeue(value));
  EXPECT_EQ(value, "hello");
  EXPECT_TRUE(queue.try_dequeue(value));
  EXPECT_EQ(value, "world");
  EXPECT_TRUE(queue.try_dequeue(value));
  EXPECT_EQ(value, "test");
}

TEST_F(OsLocklessQueueTest, EmptyAfterDequeue) {
  ConcurrentQueue<int> queue(10);

  queue.enqueue(1);
  int value;
  EXPECT_TRUE(queue.try_dequeue(value));
  EXPECT_FALSE(queue.try_dequeue(value));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
