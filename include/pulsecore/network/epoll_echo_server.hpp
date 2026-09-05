#pragma once

#include "pulsecore/common/unique_fd.hpp"
#include "pulsecore/network/connection_registry.hpp"

#include <atomic>
#include <cstdint>

namespace pulsecore::network {

class EpollEchoServer {
 public:
  explicit EpollEchoServer(std::uint16_t port);

  [[nodiscard]] std::uint16_t port() const noexcept;
  [[nodiscard]] std::size_t live_connection_count() const noexcept;

  void Run();
  void Stop() noexcept;

 private:
  void AddListenerToEpoll();
  void HandleListenerEvent();
  void HandleConnectionEvent(ConnectionId id, std::uint32_t events);
  void UpdateInterest(Connection& connection);
  void RemoveConnection(ConnectionId id);

  UniqueFd listener_;
  UniqueFd epoll_;
  std::uint16_t port_{0};
  ConnectionRegistry connections_;
  std::atomic_bool stop_requested_{false};
};

}  // namespace pulsecore::network
