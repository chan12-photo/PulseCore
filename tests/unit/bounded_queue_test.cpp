#include "pulsecore/core/bounded_queue.hpp"

#include <chrono>
#include <future>
#include <optional>

#include <gtest/gtest.h>

namespace pulsecore::core {
namespace {

using namespace std::chrono_literals;

}  // namespace

TEST(BoundedQueueTest, TryPushAndPopPreserveFifoOrder) {
  BoundedQueue<int> queue(2);

  EXPECT_TRUE(queue.TryPush(1));
  EXPECT_TRUE(queue.TryPush(2));

  const auto first = queue.Pop();
  const auto second = queue.Pop();

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, 1);
  EXPECT_EQ(*second, 2);
}

TEST(BoundedQueueTest, TryPushFailsWhenQueueIsFull) {
  BoundedQueue<int> queue(1);

  EXPECT_TRUE(queue.TryPush(1));
  EXPECT_FALSE(queue.TryPush(2));

  const auto item = queue.Pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);
}

TEST(BoundedQueueTest, TryPushFailsAfterClose) {
  BoundedQueue<int> queue(1);

  queue.Close();

  EXPECT_FALSE(queue.TryPush(1));
}

TEST(BoundedQueueTest, PopDrainsQueuedItemsAfterClose) {
  BoundedQueue<int> queue(1);

  EXPECT_TRUE(queue.TryPush(7));
  queue.Close();

  const auto item = queue.Pop();
  const auto done = queue.Pop();

  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 7);
  EXPECT_FALSE(done.has_value());
}

TEST(BoundedQueueTest, CloseWakesBlockedPop) {
  BoundedQueue<int> queue(1);

  auto popped = std::async(std::launch::async, [&queue] { return queue.Pop(); });
  EXPECT_EQ(popped.wait_for(50ms), std::future_status::timeout);

  queue.Close();

  ASSERT_EQ(popped.wait_for(1s), std::future_status::ready);
  EXPECT_FALSE(popped.get().has_value());
}

TEST(BoundedQueueTest, RejectsZeroCapacity) {
  EXPECT_THROW(BoundedQueue<int> queue(0), std::invalid_argument);
}

}  // namespace pulsecore::core
