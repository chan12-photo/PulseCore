#include "pulsecore/network/epoll_echo_server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pulsecore::network {
namespace {

constexpr int kListenBacklog = 128;
constexpr std::uint64_t kListenerKey = 0;
constexpr std::uint64_t kWorkerWakeupKey = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kShutdownSignalKey = std::numeric_limits<std::uint64_t>::max() - 1;
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

std::size_t RequirePositiveBudget(std::size_t value, const char* name) {
  if (value == 0) {
    throw std::invalid_argument(std::string(name) + " must be greater than zero");
  }
  return value;
}

sigset_t BuildSignalSet(const std::vector<int>& signals) {
  sigset_t signal_set{};
  if (::sigemptyset(&signal_set) != 0) {
    ThrowLastError("sigemptyset");
  }

  for (const int signal : signals) {
    if (::sigaddset(&signal_set, signal) != 0) {
      ThrowLastError("sigaddset");
    }
  }

  return signal_set;
}

ShutdownSignalMaskState BlockShutdownSignals(const std::vector<int>& signals) {
  ShutdownSignalMaskState state{};
  if (signals.empty()) {
    return state;
  }

  const auto signal_set = BuildSignalSet(signals);
  const int result = ::pthread_sigmask(SIG_BLOCK, &signal_set, &state.previous_mask);
  if (result != 0) {
    throw SyscallError("pthread_sigmask(SIG_BLOCK)", result);
  }

  state.active = true;
  return state;
}

void RestoreSignalMask(ShutdownSignalMaskState& state) noexcept {
  if (!state.active) {
    return;
  }

  (void)::pthread_sigmask(SIG_SETMASK, &state.previous_mask, nullptr);
  state.active = false;
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

UniqueFd CreateWorkerWakeup() {
  UniqueFd wakeup(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
  if (!wakeup) {
    ThrowLastError("eventfd");
  }
  return wakeup;
}

UniqueFd CreateShutdownSignalFd(const std::vector<int>& signals) {
  if (signals.empty()) {
    return UniqueFd{};
  }

  const auto signal_set = BuildSignalSet(signals);
  UniqueFd signal_fd(::signalfd(-1, &signal_set, SFD_NONBLOCK | SFD_CLOEXEC));
  if (!signal_fd) {
    ThrowLastError("signalfd");
  }
  return signal_fd;
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

epoll_event WorkerWakeupEvent() {
  epoll_event event{};
  event.events = EPOLLIN;
  event.data.u64 = kWorkerWakeupKey;
  return event;
}

epoll_event ShutdownSignalEvent() {
  epoll_event event{};
  event.events = EPOLLIN;
  event.data.u64 = kShutdownSignalKey;
  return event;
}

epoll_event ConnectionEvent(ConnectionId id, bool wants_read, bool wants_write) {
  epoll_event event{};
  if (wants_read) {
    event.events |= EPOLLIN | EPOLLRDHUP;
  }
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

}  // namespace

EpollEchoServer::EpollEchoServer(std::uint16_t port, EpollEchoServerOptions options)
    : listener_(CreateListener(port)),
      epoll_(CreateEpoll()),
      signal_mask_state_(BlockShutdownSignals(options.shutdown_signals)),
      worker_wakeup_(CreateWorkerWakeup()),
      shutdown_signal_(CreateShutdownSignalFd(options.shutdown_signals)),
      max_read_bytes_per_event_(
          RequirePositiveBudget(options.max_read_bytes_per_event, "max read bytes per event")),
      max_write_bytes_per_event_(
          RequirePositiveBudget(options.max_write_bytes_per_event, "max write bytes per event")),
      worker_pool_(
          WorkerPoolConfig{.worker_count = options.worker_count,
                           .queue_capacity = options.work_queue_capacity},
          [this](WorkResult result) { StoreCompletedWork(std::move(result)); },
          std::move(options.handler)) {
  port_ = BoundPort(listener_.get());
  AddListenerToEpoll();
  AddWorkerWakeupToEpoll();
  AddShutdownSignalToEpoll();
}

EpollEchoServer::~EpollEchoServer() {
  Stop();
  worker_pool_.Stop();
  RestoreSignalMask(signal_mask_state_);
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

    for (int i = 0; i < ready_count && !stop_requested_.load(std::memory_order_relaxed); ++i) {
      const auto key = events[static_cast<std::size_t>(i)].data.u64;
      if (key == kListenerKey) {
        HandleListenerEvent();
        continue;
      }
      if (key == kWorkerWakeupKey) {
        HandleWorkerWakeup();
        continue;
      }
      if (key == kShutdownSignalKey) {
        HandleShutdownSignal();
        continue;
      }

      HandleConnectionEvent(ConnectionId{key}, events[static_cast<std::size_t>(i)].events);
    }
  }

  CloseAllConnections();
}

void EpollEchoServer::Stop() noexcept {
  stop_requested_.store(true, std::memory_order_relaxed);
  NotifyWorkerWakeup();
}

void EpollEchoServer::AddListenerToEpoll() {
  auto event = ListenerEvent();
  EpollCtl(epoll_.get(), EPOLL_CTL_ADD, listener_.get(), &event, "epoll_ctl(ADD listener)");
}

void EpollEchoServer::AddWorkerWakeupToEpoll() {
  auto event = WorkerWakeupEvent();
  EpollCtl(epoll_.get(), EPOLL_CTL_ADD, worker_wakeup_.get(), &event,
           "epoll_ctl(ADD worker wakeup)");
}

void EpollEchoServer::AddShutdownSignalToEpoll() {
  if (!shutdown_signal_) {
    return;
  }

  auto event = ShutdownSignalEvent();
  EpollCtl(epoll_.get(), EPOLL_CTL_ADD, shutdown_signal_.get(), &event,
           "epoll_ctl(ADD shutdown signal)");
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

    auto [flow_it, inserted] = connection_flows_.emplace(id.value, ConnectionFlow{});
    if (!inserted) {
      throw std::runtime_error("connection flow already exists");
    }

    auto event = ConnectionEvent(id, true, connection->has_pending_output());
    try {
      EpollCtl(epoll_.get(), EPOLL_CTL_ADD, connection->fd(), &event,
               "epoll_ctl(ADD connection)");
      flow_it->second.registered_with_epoll = true;
    } catch (...) {
      connection_flows_.erase(id.value);
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

  auto flow_it = connection_flows_.find(id.value);
  if (flow_it == connection_flows_.end()) {
    RemoveConnection(id);
    return;
  }

  auto& flow = flow_it->second;
  bool should_remove = false;

  if ((events & EPOLLERR) != 0U) {
    should_remove = true;
  }

  if (!should_remove && (events & EPOLLIN) != 0U && !flow.close_after_flush) {
    auto read = connection->ReadAvailable(max_read_bytes_per_event_);
    if (!SubmitWork(id, *connection, read.messages)) {
      should_remove = true;
    }

    if (read.status == ReadAvailableStatus::kProtocolError) {
      should_remove = true;
    } else if (read.status == ReadAvailableStatus::kPeerClosed) {
      flow.close_after_flush = true;
    }
  }

  if (!should_remove && (events & EPOLLOUT) != 0U) {
    const auto write = connection->WriteAvailable(max_write_bytes_per_event_);
    if (write.status == WriteAvailableStatus::kPeerClosed) {
      should_remove = true;
    }
  }

  if (!should_remove && (events & (EPOLLHUP | EPOLLRDHUP)) != 0U) {
    flow.close_after_flush = true;
  }

  if (should_remove) {
    RemoveConnection(id);
    return;
  }

  if (ShouldCloseAfterFlush(*connection, flow)) {
    RemoveConnection(id);
    return;
  }

  UpdateInterest(*connection, flow);
}

void EpollEchoServer::HandleWorkerWakeup() {
  while (true) {
    std::uint64_t counter = 0;
    const auto bytes = ::read(worker_wakeup_.get(), &counter, sizeof(counter));
    if (bytes == static_cast<ssize_t>(sizeof(counter))) {
      continue;
    }
    if (bytes < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR) {
        continue;
      }
      if (IsWouldBlock(saved_errno)) {
        break;
      }
      throw SyscallError("read(eventfd)", saved_errno);
    }
    if (bytes != 0) {
      throw std::runtime_error("read(eventfd): short read");
    }
    break;
  }

  std::deque<WorkResult> completed;
  {
    std::lock_guard lock(completed_work_mutex_);
    completed.swap(completed_work_);
  }

  for (auto& result : completed) {
    HandleCompletedWork(std::move(result));
  }
}

void EpollEchoServer::HandleShutdownSignal() {
  while (true) {
    signalfd_siginfo signal_info{};
    const auto bytes = ::read(shutdown_signal_.get(), &signal_info, sizeof(signal_info));
    if (bytes == static_cast<ssize_t>(sizeof(signal_info))) {
      stop_requested_.store(true, std::memory_order_relaxed);
      continue;
    }
    if (bytes < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR) {
        continue;
      }
      if (IsWouldBlock(saved_errno)) {
        break;
      }
      throw SyscallError("read(signalfd)", saved_errno);
    }
    if (bytes != 0) {
      throw std::runtime_error("read(signalfd): short read");
    }
    break;
  }
}

void EpollEchoServer::HandleCompletedWork(WorkResult result) {
  auto* connection = connections_.Find(result.connection_id);
  if (connection == nullptr) {
    return;
  }

  auto flow_it = connection_flows_.find(result.connection_id.value);
  if (flow_it == connection_flows_.end()) {
    return;
  }

  auto& flow = flow_it->second;
  flow.completed_responses.emplace(result.sequence, std::move(result.response));
  (void)FlushReadyResponses(result.connection_id, *connection, flow);
}

bool EpollEchoServer::SubmitWork(ConnectionId id,
                                 Connection&,
                                 const std::vector<protocol::Message>& messages) {
  auto flow_it = connection_flows_.find(id.value);
  if (flow_it == connection_flows_.end()) {
    return false;
  }

  auto& flow = flow_it->second;
  for (const auto& message : messages) {
    WorkItem item{
        .connection_id = id,
        .sequence = flow.next_request_sequence,
        .request = message,
    };

    if (!worker_pool_.TrySubmit(std::move(item))) {
      return false;
    }
    ++flow.next_request_sequence;
  }

  return true;
}

bool EpollEchoServer::FlushReadyResponses(ConnectionId id,
                                          Connection& connection,
                                          ConnectionFlow& flow) {
  while (true) {
    auto ready = flow.completed_responses.find(flow.next_response_sequence);
    if (ready == flow.completed_responses.end()) {
      break;
    }

    if (!connection.QueueOutput(ready->second)) {
      RemoveConnection(id);
      return false;
    }

    ++flow.next_response_sequence;
    flow.completed_responses.erase(ready);
  }

  if (ShouldCloseAfterFlush(connection, flow)) {
    RemoveConnection(id);
    return false;
  }

  UpdateInterest(connection, flow);
  return true;
}

bool EpollEchoServer::ShouldCloseAfterFlush(const Connection& connection,
                                            const ConnectionFlow& flow) const noexcept {
  return flow.close_after_flush && !connection.has_pending_output() &&
         flow.next_response_sequence >= flow.next_request_sequence;
}

void EpollEchoServer::UpdateInterest(Connection& connection, ConnectionFlow& flow) {
  const bool wants_read = !flow.close_after_flush;
  const bool wants_write = connection.has_pending_output();

  if (!wants_read && !wants_write) {
    if (flow.registered_with_epoll) {
      if (::epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, connection.fd(), nullptr) != 0) {
        const int saved_errno = errno;
        if (saved_errno != ENOENT) {
          throw SyscallError("epoll_ctl(DEL idle connection)", saved_errno);
        }
      }
      flow.registered_with_epoll = false;
    }
    return;
  }

  auto event = ConnectionEvent(connection.id(), wants_read, wants_write);
  if (flow.registered_with_epoll) {
    EpollCtl(epoll_.get(), EPOLL_CTL_MOD, connection.fd(), &event, "epoll_ctl(MOD connection)");
    return;
  }

  EpollCtl(epoll_.get(), EPOLL_CTL_ADD, connection.fd(), &event, "epoll_ctl(ADD connection)");
  flow.registered_with_epoll = true;
}

void EpollEchoServer::RemoveConnection(ConnectionId id) {
  auto* connection = connections_.Find(id);
  if (connection == nullptr) {
    return;
  }

  auto flow_it = connection_flows_.find(id.value);
  const bool registered_with_epoll =
      flow_it == connection_flows_.end() || flow_it->second.registered_with_epoll;

  if (registered_with_epoll) {
    if (::epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, connection->fd(), nullptr) != 0) {
      const int saved_errno = errno;
      if (saved_errno != ENOENT) {
        throw SyscallError("epoll_ctl(DEL connection)", saved_errno);
      }
    }
  }

  connection_flows_.erase(id.value);
  [[maybe_unused]] const bool removed = connections_.Remove(id);
}

void EpollEchoServer::CloseAllConnections() {
  while (!connection_flows_.empty()) {
    RemoveConnection(ConnectionId{connection_flows_.begin()->first});
  }
}

void EpollEchoServer::StoreCompletedWork(WorkResult result) {
  {
    std::lock_guard lock(completed_work_mutex_);
    completed_work_.push_back(std::move(result));
  }
  NotifyWorkerWakeup();
}

void EpollEchoServer::NotifyWorkerWakeup() noexcept {
  if (!worker_wakeup_) {
    return;
  }

  const std::uint64_t counter = 1;
  while (true) {
    const auto bytes = ::write(worker_wakeup_.get(), &counter, sizeof(counter));
    if (bytes == static_cast<ssize_t>(sizeof(counter))) {
      return;
    }
    if (bytes < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR) {
        continue;
      }
      return;
    }
    return;
  }
}

}  // namespace pulsecore::network
