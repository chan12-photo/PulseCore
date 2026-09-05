#include "pulsecore/network/epoll_echo_server.hpp"

#include "pulsecore/core/handler.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pulsecore::network {
namespace {

constexpr int kListenBacklog = 128;
constexpr std::uint64_t kListenerKey = 0;
constexpr int kEpollWaitTimeoutMs = 50;
constexpr std::size_t kMaxEpollEvents = 64;
constexpr int kMaxAcceptsPerEvent = 64;

std::runtime_error SyscallError(const char* operation, int error) {
  return std::runtime_error(std::string(operation) + ": " + std::strerror(error));
}

void ThrowLastError(const char* operation) {
  const int saved_errno = errno;
  throw SyscallError(operation, saved_errno);
}

bool IsWouldBlock(int error) noexcept {
  return error == EAGAIN || error == EWOULDBLOCK;
}

sockaddr_in LoopbackAddress(std::uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  return address;
}

std::uint16_t BoundPort(int fd) {
  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    ThrowLastError("getsockname");
  }
  return ntohs(address.sin_port);
}

UniqueFd CreateListener(std::uint16_t port) {
  UniqueFd listener(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!listener) {
    ThrowLastError("socket");
  }

  int reuse_address = 1;
  if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) != 0) {
    ThrowLastError("setsockopt(SO_REUSEADDR)");
  }

  const auto address = LoopbackAddress(port);
  if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    ThrowLastError("bind");
  }

  if (::listen(listener.get(), kListenBacklog) != 0) {
    ThrowLastError("listen");
  }

  return listener;
}

UniqueFd CreateEpoll() {
  UniqueFd epoll(::epoll_create1(EPOLL_CLOEXEC));
  if (!epoll) {
    ThrowLastError("epoll_create1");
  }
  return epoll;
}

void EpollCtl(int epoll_fd, int operation, int fd, epoll_event* event, const char* operation_name) {
  if (::epoll_ctl(epoll_fd, operation, fd, event) != 0) {
    ThrowLastError(operation_name);
  }
}

epoll_event ListenerEvent() {
  epoll_event event{};
  event.events = EPOLLIN;
  event.data.u64 = kListenerKey;
  return event;
}

epoll_event ConnectionEvent(ConnectionId id, bool wants_write) {
  epoll_event event{};
  event.events = EPOLLIN | EPOLLRDHUP;
  if (wants_write) {
    event.events |= EPOLLOUT;
  }
  event.data.u64 = id.value;
  return event;
}

UniqueFd AcceptNonBlocking(int listener_fd) {
  while (true) {
    UniqueFd client(::accept4(listener_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
    if (client) {
      return client;
    }

    const int saved_errno = errno;
    if (saved_errno == EINTR) {
      continue;
    }
    if (IsWouldBlock(saved_errno)) {
      return UniqueFd{};
    }
    throw SyscallError("accept4", saved_errno);
  }
}

bool QueueResponses(Connection& connection, const std::vector<protocol::Message>& messages) {
  for (const auto& message : messages) {
    if (!connection.QueueOutput(core::HandleRequest(message))) {
      return false;
    }
  }
  return true;
}

}  // namespace

EpollEchoServer::EpollEchoServer(std::uint16_t port)
    : listener_(CreateListener(port)), epoll_(CreateEpoll()) {
  port_ = BoundPort(listener_.get());
  AddListenerToEpoll();
}

std::uint16_t EpollEchoServer::port() const noexcept {
  return port_;
}

std::size_t EpollEchoServer::live_connection_count() const noexcept {
  return connections_.size();
}

void EpollEchoServer::Run() {
  std::array<epoll_event, kMaxEpollEvents> events{};

  while (!stop_requested_.load(std::memory_order_relaxed)) {
    const int ready_count =
        ::epoll_wait(epoll_.get(), events.data(), static_cast<int>(events.size()),
                     kEpollWaitTimeoutMs);

    if (ready_count < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR) {
        continue;
      }
      throw SyscallError("epoll_wait", saved_errno);
    }

    for (int i = 0; i < ready_count; ++i) {
      if (events[static_cast<std::size_t>(i)].data.u64 == kListenerKey) {
        HandleListenerEvent();
        continue;
      }

      HandleConnectionEvent(ConnectionId{events[static_cast<std::size_t>(i)].data.u64},
                            events[static_cast<std::size_t>(i)].events);
    }
  }
}

void EpollEchoServer::Stop() noexcept {
  stop_requested_.store(true, std::memory_order_relaxed);
}

void EpollEchoServer::AddListenerToEpoll() {
  auto event = ListenerEvent();
  EpollCtl(epoll_.get(), EPOLL_CTL_ADD, listener_.get(), &event, "epoll_ctl(ADD listener)");
}

void EpollEchoServer::HandleListenerEvent() {
  for (int accepted = 0; accepted < kMaxAcceptsPerEvent; ++accepted) {
    UniqueFd client = AcceptNonBlocking(listener_.get());
    if (!client) {
      return;
    }

    const auto id = connections_.Add(std::move(client));
    auto* connection = connections_.Find(id);
    if (connection == nullptr) {
      throw std::runtime_error("registered connection cannot be found");
    }

    auto event = ConnectionEvent(id, connection->has_pending_output());
    try {
      EpollCtl(epoll_.get(), EPOLL_CTL_ADD, connection->fd(), &event,
               "epoll_ctl(ADD connection)");
    } catch (...) {
      [[maybe_unused]] const bool removed = connections_.Remove(id);
      throw;
    }
  }
}

void EpollEchoServer::HandleConnectionEvent(ConnectionId id, std::uint32_t events) {
  auto* connection = connections_.Find(id);
  if (connection == nullptr) {
    return;
  }

  bool should_remove = false;

  if ((events & EPOLLIN) != 0U) {
    auto read = connection->ReadAvailable();
    if (!QueueResponses(*connection, read.messages)) {
      should_remove = true;
    }

    if (read.status == ReadAvailableStatus::kPeerClosed ||
        read.status == ReadAvailableStatus::kProtocolError) {
      should_remove = true;
    }
  }

  if (!should_remove && (events & EPOLLOUT) != 0U) {
    const auto write = connection->WriteAvailable();
    if (write.status == WriteAvailableStatus::kPeerClosed) {
      should_remove = true;
    }
  }

  if (!should_remove && (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
    should_remove = true;
  }

  if (should_remove) {
    RemoveConnection(id);
    return;
  }

  UpdateInterest(*connection);
}

void EpollEchoServer::UpdateInterest(Connection& connection) {
  auto event = ConnectionEvent(connection.id(), connection.has_pending_output());
  EpollCtl(epoll_.get(), EPOLL_CTL_MOD, connection.fd(), &event, "epoll_ctl(MOD connection)");
}

void EpollEchoServer::RemoveConnection(ConnectionId id) {
  auto* connection = connections_.Find(id);
  if (connection == nullptr) {
    return;
  }

  if (::epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, connection->fd(), nullptr) != 0) {
    const int saved_errno = errno;
    if (saved_errno != ENOENT) {
      throw SyscallError("epoll_ctl(DEL connection)", saved_errno);
    }
  }

  [[maybe_unused]] const bool removed = connections_.Remove(id);
}

}  // namespace pulsecore::network
