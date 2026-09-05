#include "pulsecore/network/connection_registry.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace pulsecore::network {

ConnectionRegistry::ConnectionRegistry(ConnectionLimits limits) : limits_(limits) {}

ConnectionId ConnectionRegistry::Add(UniqueFd fd) {
  if (!fd) {
    throw std::invalid_argument("cannot register invalid connection fd");
  }

  if (next_id_.value == std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("connection id space exhausted");
  }

  SetNonBlocking(fd.get());

  const ConnectionId id = next_id_;
  ++next_id_.value;

  auto [it, inserted] = connections_.try_emplace(id.value, id, std::move(fd), limits_);
  if (!inserted) {
    throw std::runtime_error("duplicate connection id");
  }

  return it->second.id();
}

Connection* ConnectionRegistry::Find(ConnectionId id) noexcept {
  auto it = connections_.find(id.value);
  if (it == connections_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Connection* ConnectionRegistry::Find(ConnectionId id) const noexcept {
  auto it = connections_.find(id.value);
  if (it == connections_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool ConnectionRegistry::Remove(ConnectionId id) noexcept {
  return connections_.erase(id.value) == 1U;
}

std::size_t ConnectionRegistry::size() const noexcept {
  return connections_.size();
}

bool ConnectionRegistry::empty() const noexcept {
  return connections_.empty();
}

}  // namespace pulsecore::network
