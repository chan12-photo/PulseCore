#include "pulsecore/network/worker_pool.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace pulsecore::network {
namespace {

using namespace std::chrono_literals;

protocol::Message EchoRequest(std::uint64_t request_id) {
  return protocol::Message{
      .type = protocol::MessageType::kEchoRequest,
      .request_id = request_id,
      .payload = {static_cast<protocol::Byte>(request_id)},
  };
}

}  // namespace

TEST(WorkerPoolTest, ProcessesSubmittedWorkAndInvokesCompletionCallback) {
  std::mutex mutex;
  std::condition_variable completed;
  std::vector<WorkResult> results;

  WorkerPool pool(
      WorkerPoolConfig{.worker_count = 2, .queue_capacity = 4},
      [&](WorkResult result) {
        {
          std::lock_guard lock(mutex);
          results.push_back(std::move(result));
        }
        completed.notify_one();
      });

  EXPECT_TRUE(pool.TrySubmit(WorkItem{
      .connection_id = ConnectionId{7},
      .sequence = 3,
      .request = EchoRequest(42),
  }));

  std::unique_lock lock(mutex);
  ASSERT_TRUE(completed.wait_for(lock, 1s, [&results] { return results.size() == 1; }));
  pool.Stop();

  EXPECT_EQ(results[0].connection_id.value, 7U);
  EXPECT_EQ(results[0].sequence, 3U);
  EXPECT_EQ(results[0].response.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(results[0].response.request_id, 42U);
  EXPECT_EQ(results[0].response.payload, (std::vector<protocol::Byte>{42}));
}

TEST(WorkerPoolTest, RejectsSubmissionsAfterStop) {
  WorkerPool pool(WorkerPoolConfig{.worker_count = 1, .queue_capacity = 1},
                  [](WorkResult) {});

  pool.Stop();

  EXPECT_FALSE(pool.TrySubmit(WorkItem{
      .connection_id = ConnectionId{1},
      .sequence = 0,
      .request = EchoRequest(1),
  }));
}

TEST(WorkerPoolTest, RejectsInvalidConfiguration) {
  EXPECT_THROW(
      WorkerPool(WorkerPoolConfig{.worker_count = 0, .queue_capacity = 1}, [](WorkResult) {}),
      std::invalid_argument);

  EXPECT_THROW(
      WorkerPool(WorkerPoolConfig{.worker_count = 1, .queue_capacity = 0}, [](WorkResult) {}),
      std::invalid_argument);

  EXPECT_THROW(WorkerPool(WorkerPoolConfig{.worker_count = 1, .queue_capacity = 1}, nullptr),
               std::invalid_argument);
}

}  // namespace pulsecore::network
