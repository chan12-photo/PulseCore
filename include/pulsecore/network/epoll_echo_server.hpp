#pragma once

#include "pulsecore/common/unique_fd.hpp"
#include "pulsecore/network/connection_registry.hpp"
#include "pulsecore/network/worker_pool.hpp"
#include "pulsecore/protocol/codec.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>

namespace pulsecore::network {

struct EpollEchoServerOptions {
  std::size_t worker_count{2};
  std::size_t work_queue_capacity{1024};
  WorkHandler handler{::pulsecore::core::HandleRequest};
};

class EpollEchoServer {
 public:
  explicit EpollEchoServer(std::uint16_t port, EpollEchoServerOptions options = {});

  [[nodiscard]] std::uint16_t port() const noexcept;
  [[nodiscard]] std::size_t live_connection_count() const noexcept;

  void Run();
  void Stop() noexcept;

 private:
  struct ConnectionFlow {
    std::uint64_t next_request_sequence{0};
    std::uint64_t next_response_sequence{0};
    std::map<std::uint64_t, protocol::Message> completed_responses;
    bool close_after_flush{false};
    bool registered_with_epoll{false};
  };

  void AddListenerToEpoll();
  void AddWorkerWakeupToEpoll();
  void HandleListenerEvent();
  void HandleConnectionEvent(ConnectionId id, std::uint32_t events);
  void HandleWorkerWakeup();
  void HandleCompletedWork(WorkResult result);
  [[nodiscard]] bool SubmitWork(ConnectionId id,
                                Connection& connection,
                                const std::vector<protocol::Message>& messages);
  [[nodiscard]] bool FlushReadyResponses(ConnectionId id,
                                         Connection& connection,
                                         ConnectionFlow& flow);
  [[nodiscard]] bool ShouldCloseAfterFlush(const Connection& connection,
                                           const ConnectionFlow& flow) const noexcept;
  void UpdateInterest(Connection& connection, ConnectionFlow& flow);
  void RemoveConnection(ConnectionId id);
  void StoreCompletedWork(WorkResult result);
  void NotifyWorkerWakeup() noexcept;

  UniqueFd listener_;
  UniqueFd epoll_;
  UniqueFd worker_wakeup_;
  std::uint16_t port_{0};
  ConnectionRegistry connections_;
  std::unordered_map<std::uint64_t, ConnectionFlow> connection_flows_;
  std::mutex completed_work_mutex_;
  std::deque<WorkResult> completed_work_;
  std::atomic_bool stop_requested_{false};
  WorkerPool worker_pool_;
};

}  // namespace pulsecore::network
