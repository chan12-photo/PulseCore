#include "pulsecore/network/worker_pool.hpp"

#include <stdexcept>
#include <utility>

namespace pulsecore::network {

WorkerPool::WorkerPool(WorkerPoolConfig config,
                       WorkCompletion on_complete,
                       WorkHandler handler)
    : config_(config),
      queue_(config.queue_capacity),
      on_complete_(std::move(on_complete)),
      handler_(std::move(handler)) {
  if (config_.worker_count == 0) {
    throw std::invalid_argument("worker count must be greater than zero");
  }
  if (!on_complete_) {
    throw std::invalid_argument("worker completion callback must be set");
  }
  if (!handler_) {
    throw std::invalid_argument("worker handler must be set");
  }

  workers_.reserve(config_.worker_count);
  try {
    for (std::size_t i = 0; i < config_.worker_count; ++i) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  } catch (...) {
    Stop();
    throw;
  }
}

WorkerPool::~WorkerPool() {
  Stop();
}

bool WorkerPool::TrySubmit(WorkItem item) {
  return queue_.TryPush(std::move(item));
}

void WorkerPool::Stop() {
  queue_.Close();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

std::size_t WorkerPool::pending_work_count() const {
  return queue_.size();
}

void WorkerPool::WorkerLoop() {
  while (auto item = queue_.Pop()) {
    auto response = handler_(std::move(item->request));
    on_complete_(WorkResult{
        .connection_id = item->connection_id,
        .sequence = item->sequence,
        .response = std::move(response),
    });
  }
}

}  // namespace pulsecore::network
