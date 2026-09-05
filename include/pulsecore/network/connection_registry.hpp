#pragma once

#include "pulsecore/common/unique_fd.hpp"
#include "pulsecore/network/connection.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace pulsecore::network {

class ConnectionRegistry {
 public:
  explicit ConnectionRegistry(ConnectionLimits limits = {});

  [[nodiscard]] ConnectionId Add(UniqueFd fd);
  [[nodiscard]] Connection* Find(ConnectionId id) noexcept;
  [[nodiscard]] const Connection* Find(ConnectionId id) const noexcept;
  [[nodiscard]] bool Remove(ConnectionId id) noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

 private:
  ConnectionLimits limits_;
  ConnectionId next_id_{1};
  std::unordered_map<std::uint64_t, Connection> connections_;
};

}  // namespace pulsecore::network
