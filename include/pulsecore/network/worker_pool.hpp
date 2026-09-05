#pragma once

#include "pulsecore/core/bounded_queue.hpp"
#include "pulsecore/core/handler.hpp"
#include "pulsecore/network/connection.hpp"
#include "pulsecore/protocol/codec.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace pulsecore::network {

struct WorkItem {
  ConnectionId connection_id;
  std::uint64_t sequence;
  protocol::Message request;
};

struct WorkResult {
  ConnectionId connection_id;
  std::uint64_t sequence;
  protocol::Message response;
};

struct WorkerPoolConfig {
  std::size_t worker_count{2};
  std::size_t queue_capacity{1024};
};

using WorkHandler = std::function<protocol::Message(const protocol::Message&)>;
using WorkCompletion = std::function<void(WorkResult)>;

class WorkerPool {
 public:
  WorkerPool(WorkerPoolConfig config,
             WorkCompletion on_complete,
             WorkHandler handler = ::pulsecore::core::HandleRequest);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  [[nodiscard]] bool TrySubmit(WorkItem item);
  void Stop();

  [[nodiscard]] std::size_t pending_work_count() const;

 private:
  void WorkerLoop();

  WorkerPoolConfig config_;
  core::BoundedQueue<WorkItem> queue_;
  WorkCompletion on_complete_;
  WorkHandler handler_;
  std::vector<std::thread> workers_;
};

}  // namespace pulsecore::network
