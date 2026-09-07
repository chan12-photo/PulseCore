#include "pulsecore/network/worker_pool.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace pulsecore::network {
namespace {

std::vector<protocol::Byte> ErrorPayload(std::string_view message) {
  return {message.begin(), message.end()};
}

protocol::Message WorkerHandlerErrorResponse(std::uint64_t request_id) {
  return protocol::Message{
      .type = protocol::MessageType::kErrorResponse,
      .request_id = request_id,
      .payload = ErrorPayload("worker handler failed"),
  };
}

}  // namespace

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
    const auto request_id = item->request.request_id;
    protocol::Message response = WorkerHandlerErrorResponse(request_id);
    try {
      response = handler_(std::move(item->request));
    } catch (const std::exception&) {
      response = WorkerHandlerErrorResponse(request_id);
    } catch (...) {
      response = WorkerHandlerErrorResponse(request_id);
    }

    on_complete_(WorkResult{
        .connection_id = item->connection_id,
        .sequence = item->sequence,
        .response = std::move(response),
    });
  }
}

}  // namespace pulsecore::network
