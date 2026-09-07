#include "pulsecore/network/epoll_echo_server.hpp"

#include "pulsecore/core/handler.hpp"
#include "pulsecore/network/blocking_tcp.hpp"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace pulsecore::network {
namespace {

using namespace std::chrono_literals;

class EpollServerThread {
 public:
  explicit EpollServerThread(EpollEchoServer& server)
      : server_(server),
        thread_([this] {
          try {
            server_.Run();
          } catch (...) {
            error_ = std::current_exception();
          }
        }) {}

  EpollServerThread(const EpollServerThread&) = delete;
  EpollServerThread& operator=(const EpollServerThread&) = delete;

  ~EpollServerThread() noexcept {
    server_.Stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void StopAndJoin() {
    server_.Stop();
    if (thread_.joinable()) {
      thread_.join();
    }
    if (error_ != nullptr) {
      std::rethrow_exception(error_);
    }
  }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
    if (error_ != nullptr) {
      std::rethrow_exception(error_);
    }
  }

 private:
  EpollEchoServer& server_;
  std::thread thread_;
  std::exception_ptr error_;
};

class PromiseReleaseGuard {
 public:
  explicit PromiseReleaseGuard(std::shared_ptr<std::promise<void>> promise)
      : promise_(std::move(promise)) {}

  PromiseReleaseGuard(const PromiseReleaseGuard&) = delete;
  PromiseReleaseGuard& operator=(const PromiseReleaseGuard&) = delete;

  ~PromiseReleaseGuard() noexcept {
    Release();
  }

  void Release() noexcept {
    if (promise_ == nullptr) {
      return;
    }
    try {
      promise_->set_value();
    } catch (...) {
    }
    promise_.reset();
  }

 private:
  std::shared_ptr<std::promise<void>> promise_;
};

class SignalMaskRestorer {
 public:
  explicit SignalMaskRestorer(const sigset_t& mask) : mask_(mask) {}

  SignalMaskRestorer(const SignalMaskRestorer&) = delete;
  SignalMaskRestorer& operator=(const SignalMaskRestorer&) = delete;

  ~SignalMaskRestorer() noexcept {
    (void)::pthread_sigmask(SIG_SETMASK, &mask_, nullptr);
  }

 private:
  sigset_t mask_{};
};

protocol::Message EchoRequest(std::uint64_t request_id, std::vector<protocol::Byte> payload) {
  return protocol::Message{
      .type = protocol::MessageType::kEchoRequest,
      .request_id = request_id,
      .payload = std::move(payload),
  };
}

protocol::Message WorkRequest(std::uint64_t request_id,
                              std::uint32_t iterations,
                              const std::vector<protocol::Byte>& seed) {
  return protocol::Message{
      .type = protocol::MessageType::kWorkRequest,
      .request_id = request_id,
      .payload = core::EncodeWorkRequestPayload(iterations, seed),
  };
}

void SendBytes(int fd, std::span<const protocol::Byte> bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto sent = ::send(fd, bytes.data() + offset, bytes.size() - offset, 0);
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    ASSERT_GT(sent, 0);
    offset += static_cast<std::size_t>(sent);
  }
}

void SetReceiveTimeout(int fd) {
  timeval timeout{};
  timeout.tv_sec = 1;
  ASSERT_EQ(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);
}

bool WaitForPeerClose(int fd) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    std::array<protocol::Byte, 1> byte{};
    const auto received = ::recv(fd, byte.data(), byte.size(), 0);
    if (received == 0 || (received < 0 && errno == ECONNRESET)) {
      return true;
    }
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      std::this_thread::sleep_for(1ms);
      continue;
    }
    return false;
  }
  return false;
}

}  // namespace

TEST(EpollEchoIntegrationTest, EchoesSingleRequestOverTcp) {
  EpollEchoServer server(0);
  EpollServerThread server_thread(server);

  const auto response = SendRequestAndReadResponse("127.0.0.1", server.port(),
                                                  EchoRequest(42, {0x41, 0x42, 0x43}));

  server_thread.StopAndJoin();

  EXPECT_EQ(response.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.request_id, 42U);
  EXPECT_EQ(response.payload, (std::vector<protocol::Byte>{0x41, 0x42, 0x43}));
}

TEST(EpollEchoIntegrationTest, HandlesWorkRequestOverTcp) {
  EpollEchoServer server(0);
  EpollServerThread server_thread(server);
  const std::vector<protocol::Byte> seed{0xAA, 0xBB};

  const auto response =
      SendRequestAndReadResponse("127.0.0.1", server.port(), WorkRequest(99, 3, seed));

  server_thread.StopAndJoin();

  EXPECT_EQ(response.type, protocol::MessageType::kWorkResponse);
  EXPECT_EQ(response.request_id, 99U);
  EXPECT_EQ(response.payload,
            (std::vector<protocol::Byte>{0xA2, 0x89, 0x7E, 0x00, 0x44, 0x62, 0x2F, 0x90}));
}

TEST(EpollEchoIntegrationTest, ReturnsErrorWhenWorkerHandlerThrowsAndKeepsServerRunning) {
  EpollEchoServerOptions options;
  options.handler = [](protocol::Message request) {
    if (request.request_id == 1) {
      throw std::runtime_error("handler failure");
    }
    return core::HandleOwnedRequest(std::move(request));
  };

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  UniqueFd first_client = ConnectTcp("127.0.0.1", server.port());
  SendMessage(first_client.get(), EchoRequest(1, {0x01}));
  protocol::FrameDecoder first_decoder;
  auto first = ReadMessage(first_client.get(), first_decoder);

  const auto second =
      SendRequestAndReadResponse("127.0.0.1", server.port(), EchoRequest(2, {0x02}));

  first_client.reset();
  server_thread.StopAndJoin();

  ASSERT_EQ(first.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(first.message.has_value());
  EXPECT_EQ(first.message->type, protocol::MessageType::kErrorResponse);
  EXPECT_EQ(first.message->request_id, 1U);
  EXPECT_FALSE(first.message->payload.empty());
  EXPECT_EQ(second.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(second.request_id, 2U);
  EXPECT_EQ(second.payload, (std::vector<protocol::Byte>{0x02}));
}

TEST(EpollEchoIntegrationTest, EchoesRequestWithSmallPerEventBudgets) {
  EpollEchoServerOptions options;
  options.max_read_bytes_per_event = 5;
  options.max_write_bytes_per_event = 5;

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  const auto response = SendRequestAndReadResponse("127.0.0.1", server.port(),
                                                  EchoRequest(43, {0x41, 0x42, 0x43}));

  server_thread.StopAndJoin();

  EXPECT_EQ(response.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.request_id, 43U);
  EXPECT_EQ(response.payload, (std::vector<protocol::Byte>{0x41, 0x42, 0x43}));
}

TEST(EpollEchoIntegrationTest, DrainsHalfClosedRequestAcrossReadBudgets) {
  EpollEchoServerOptions options;
  options.max_read_bytes_per_event = 5;

  EpollEchoServer server(0, std::move(options));

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SetReceiveTimeout(client.get());
  SendMessage(client.get(), EchoRequest(9, {0x09}));
  ASSERT_EQ(::shutdown(client.get(), SHUT_WR), 0);

  EpollServerThread server_thread(server);

  protocol::FrameDecoder decoder;
  auto response = ReadMessage(client.get(), decoder);

  client.reset();
  server_thread.StopAndJoin();

  ASSERT_EQ(response.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(response.message.has_value());
  EXPECT_EQ(response.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.message->request_id, 9U);
  EXPECT_EQ(response.message->payload, (std::vector<protocol::Byte>{0x09}));
}

TEST(EpollEchoIntegrationTest, DrainsHalfClosedMaximumFrameWithDefaultReadBudget) {
  EpollEchoServerOptions options;
  options.max_read_bytes_per_event = kDefaultMaxReadBytesPerEvent;

  EpollEchoServer server(0, std::move(options));

  const std::vector<protocol::Byte> payload(protocol::kMaxPayloadSize, 0x5A);
  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SetReceiveTimeout(client.get());
  SendMessage(client.get(), EchoRequest(10, payload));
  ASSERT_EQ(::shutdown(client.get(), SHUT_WR), 0);

  EpollServerThread server_thread(server);

  protocol::FrameDecoder decoder;
  auto response = ReadMessage(client.get(), decoder);

  client.reset();
  server_thread.StopAndJoin();

  ASSERT_EQ(response.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(response.message.has_value());
  EXPECT_EQ(response.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.message->request_id, 10U);
  EXPECT_EQ(response.message->payload, payload);
}

TEST(EpollEchoIntegrationTest, RejectsZeroPerEventBudgets) {
  EpollEchoServerOptions read_options;
  read_options.max_read_bytes_per_event = 0;
  EXPECT_THROW(EpollEchoServer server(0, read_options), std::invalid_argument);

  EpollEchoServerOptions write_options;
  write_options.max_write_bytes_per_event = 0;
  EXPECT_THROW(EpollEchoServer server(0, write_options), std::invalid_argument);

  EpollEchoServerOptions in_flight_options;
  in_flight_options.max_in_flight_requests_per_connection = 0;
  EXPECT_THROW(EpollEchoServer server(0, in_flight_options), std::invalid_argument);

  EpollEchoServerOptions connection_options;
  connection_options.max_connections = 0;
  EXPECT_THROW(EpollEchoServer server(0, connection_options), std::invalid_argument);

  EpollEchoServerOptions input_options;
  input_options.connection_limits.max_input_buffer = 0;
  EXPECT_THROW(EpollEchoServer server(0, input_options), std::invalid_argument);

  EpollEchoServerOptions output_options;
  output_options.connection_limits.max_output_buffer = 0;
  EXPECT_THROW(EpollEchoServer server(0, output_options), std::invalid_argument);
}

TEST(EpollEchoIntegrationTest, HandlesMultipleClients) {
  EpollEchoServer server(0);
  EpollServerThread server_thread(server);

  constexpr std::uint64_t kClientCount = 16;
  std::vector<std::thread> clients;
  std::vector<std::optional<protocol::Message>> responses(kClientCount);

  for (std::uint64_t i = 0; i < kClientCount; ++i) {
    clients.emplace_back([&, i] {
      responses[i] = SendRequestAndReadResponse("127.0.0.1", server.port(),
                                                EchoRequest(100 + i, {static_cast<protocol::Byte>(i)}));
    });
  }

  for (auto& client : clients) {
    client.join();
  }

  server_thread.StopAndJoin();

  for (std::uint64_t i = 0; i < kClientCount; ++i) {
    ASSERT_TRUE(responses[i].has_value());
    EXPECT_EQ(responses[i]->type, protocol::MessageType::kEchoResponse);
    EXPECT_EQ(responses[i]->request_id, 100U + i);
    EXPECT_EQ(responses[i]->payload, (std::vector<protocol::Byte>{static_cast<protocol::Byte>(i)}));
  }
}

TEST(EpollEchoIntegrationTest, ClosesConnectionsAboveConfiguredLimit) {
  EpollEchoServerOptions options;
  options.max_connections = 1;

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  UniqueFd first = ConnectTcp("127.0.0.1", server.port());
  UniqueFd rejected = ConnectTcp("127.0.0.1", server.port());
  SetNonBlocking(rejected.get());

  bool rejected_closed = false;
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    std::array<protocol::Byte, 1> byte{};
    const auto received = ::recv(rejected.get(), byte.data(), byte.size(), 0);
    if (received == 0 || (received < 0 && errno == ECONNRESET)) {
      rejected_closed = true;
      break;
    }
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      std::this_thread::sleep_for(1ms);
      continue;
    }
    FAIL() << "unexpected recv result while waiting for rejected connection close";
  }

  SendMessage(first.get(), EchoRequest(77, {0x77}));
  protocol::FrameDecoder decoder;
  auto response = ReadMessage(first.get(), decoder);

  first.reset();
  rejected.reset();
  server_thread.StopAndJoin();

  EXPECT_TRUE(rejected_closed);
  ASSERT_EQ(response.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(response.message.has_value());
  EXPECT_EQ(response.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.message->request_id, 77U);
  EXPECT_EQ(response.message->payload, (std::vector<protocol::Byte>{0x77}));
}

TEST(EpollEchoIntegrationTest, ClosesConnectionWhenOutputLimitWouldBeExceeded) {
  EpollEchoServerOptions options;
  options.connection_limits.max_output_buffer = protocol::kHeaderSize;

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SetReceiveTimeout(client.get());
  SendMessage(client.get(), EchoRequest(1, {0x01}));

  std::array<protocol::Byte, 1> byte{};
  const auto received = ::recv(client.get(), byte.data(), byte.size(), 0);

  client.reset();
  server_thread.StopAndJoin();

  EXPECT_TRUE(received == 0 || (received < 0 && errno == ECONNRESET));
}

TEST(EpollEchoIntegrationTest, ClosesConnectionWhenInputLimitWouldBeExceeded) {
  EpollEchoServerOptions options;
  options.connection_limits.max_input_buffer = protocol::kHeaderSize - 1U;

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SetReceiveTimeout(client.get());
  SendMessage(client.get(), EchoRequest(1, {}));

  std::array<protocol::Byte, 1> byte{};
  const auto received = ::recv(client.get(), byte.data(), byte.size(), 0);

  client.reset();
  server_thread.StopAndJoin();

  EXPECT_TRUE(received == 0 || (received < 0 && errno == ECONNRESET));
}

TEST(EpollEchoIntegrationTest, HandlesMultipleFramesOnOneConnection) {
  EpollEchoServer server(0);
  EpollServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SendMessage(client.get(), EchoRequest(1, {0x01}));
  SendMessage(client.get(), EchoRequest(2, {0x02, 0x03}));

  protocol::FrameDecoder decoder;
  auto first = ReadMessage(client.get(), decoder);
  auto second = ReadMessage(client.get(), decoder);

  client.reset();
  server_thread.StopAndJoin();

  ASSERT_EQ(first.status, ReadMessageStatus::kMessage);
  ASSERT_EQ(second.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(first.message.has_value());
  ASSERT_TRUE(second.message.has_value());
  EXPECT_EQ(first.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(first.message->request_id, 1U);
  EXPECT_EQ(first.message->payload, (std::vector<protocol::Byte>{0x01}));
  EXPECT_EQ(second.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(second.message->request_id, 2U);
  EXPECT_EQ(second.message->payload, (std::vector<protocol::Byte>{0x02, 0x03}));
}

TEST(EpollEchoIntegrationTest, PreservesResponseOrderWhenWorkersCompleteOutOfOrder) {
  EpollEchoServerOptions options;
  options.worker_count = 2;
  options.work_queue_capacity = 8;
  options.handler = [](protocol::Message request) {
    if (request.request_id == 1) {
      std::this_thread::sleep_for(50ms);
    }
    return core::HandleOwnedRequest(std::move(request));
  };

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SendMessage(client.get(), EchoRequest(1, {0x01}));
  SendMessage(client.get(), EchoRequest(2, {0x02}));

  protocol::FrameDecoder decoder;
  auto first = ReadMessage(client.get(), decoder);
  auto second = ReadMessage(client.get(), decoder);

  client.reset();
  server_thread.StopAndJoin();

  ASSERT_EQ(first.status, ReadMessageStatus::kMessage);
  ASSERT_EQ(second.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(first.message.has_value());
  ASSERT_TRUE(second.message.has_value());
  EXPECT_EQ(first.message->request_id, 1U);
  EXPECT_EQ(second.message->request_id, 2U);
  EXPECT_EQ(first.message->payload, (std::vector<protocol::Byte>{0x01}));
  EXPECT_EQ(second.message->payload, (std::vector<protocol::Byte>{0x02}));
}

TEST(EpollEchoIntegrationTest, DropsWorkerResultForConnectionRemovedBeforeCompletion) {
  auto first_started = std::make_shared<std::promise<void>>();
  auto release_first = std::make_shared<std::promise<void>>();
  PromiseReleaseGuard release_first_guard(release_first);
  auto release_first_future = release_first->get_future().share();
  auto first_started_once = std::make_shared<std::atomic_bool>(false);
  auto first_started_future = first_started->get_future();

  EpollEchoServerOptions options;
  options.worker_count = 1;
  options.work_queue_capacity = 4;
  options.handler = [first_started, release_first_future, first_started_once](
                        protocol::Message request) {
    if (request.request_id == 1) {
      if (!first_started_once->exchange(true)) {
        first_started->set_value();
      }
      release_first_future.wait();
    }
    return core::HandleOwnedRequest(std::move(request));
  };

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  UniqueFd first_client = ConnectTcp("127.0.0.1", server.port());
  SendMessage(first_client.get(), EchoRequest(1, {0x01}));
  ASSERT_EQ(first_started_future.wait_for(1s), std::future_status::ready);

  const std::array<protocol::Byte, protocol::kHeaderSize> invalid_header{};
  SendBytes(first_client.get(), invalid_header);
  SetNonBlocking(first_client.get());
  ASSERT_TRUE(WaitForPeerClose(first_client.get()));
  first_client.reset();

  UniqueFd second_client = ConnectTcp("127.0.0.1", server.port());
  SendMessage(second_client.get(), EchoRequest(2, {0x02}));
  release_first_guard.Release();

  protocol::FrameDecoder decoder;
  auto response = ReadMessage(second_client.get(), decoder);

  second_client.reset();
  server_thread.StopAndJoin();

  ASSERT_EQ(response.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(response.message.has_value());
  EXPECT_EQ(response.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.message->request_id, 2U);
  EXPECT_EQ(response.message->payload, (std::vector<protocol::Byte>{0x02}));
}

TEST(EpollEchoIntegrationTest, RestoresSignalMaskWhenConstructionFails) {
  sigset_t original{};
  ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &original), 0);
  SignalMaskRestorer restore_original(original);

  sigset_t signal_set{};
  ASSERT_EQ(::sigemptyset(&signal_set), 0);
  ASSERT_EQ(::sigaddset(&signal_set, SIGUSR1), 0);
  ASSERT_EQ(::pthread_sigmask(SIG_UNBLOCK, &signal_set, nullptr), 0);

  sigset_t before{};
  ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &before), 0);

  EpollEchoServerOptions options;
  options.shutdown_signals = {SIGUSR1};
  options.max_connections = 0;
  EXPECT_THROW(EpollEchoServer server(0, options), std::invalid_argument);

  sigset_t after{};
  ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &after), 0);
  EXPECT_EQ(::sigismember(&after, SIGUSR1), ::sigismember(&before, SIGUSR1));
}

TEST(EpollEchoIntegrationTest, ClosesConnectionWhenPerConnectionInFlightLimitIsExceeded) {
  EpollEchoServerOptions options;
  options.worker_count = 1;
  options.work_queue_capacity = 8;
  options.max_in_flight_requests_per_connection = 1;
  options.handler = [](protocol::Message request) {
    std::this_thread::sleep_for(50ms);
    return core::HandleOwnedRequest(std::move(request));
  };

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SetReceiveTimeout(client.get());

  const auto first = protocol::EncodeMessage(EchoRequest(1, {0x01}));
  const auto second = protocol::EncodeMessage(EchoRequest(2, {0x02}));
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  std::vector<protocol::Byte> requests;
  requests.insert(requests.end(), first->begin(), first->end());
  requests.insert(requests.end(), second->begin(), second->end());
  SendBytes(client.get(), requests);

  std::array<protocol::Byte, 1> byte{};
  const auto received = ::recv(client.get(), byte.data(), byte.size(), 0);

  client.reset();
  server_thread.StopAndJoin();

  EXPECT_TRUE(received == 0 || (received < 0 && errno == ECONNRESET));
}

TEST(EpollEchoIntegrationTest, FlushesResponseAfterClientHalfClosesWriteSide) {
  EpollEchoServer server(0);
  EpollServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SendMessage(client.get(), EchoRequest(9, {0x09}));
  ASSERT_EQ(::shutdown(client.get(), SHUT_WR), 0);

  protocol::FrameDecoder decoder;
  auto response = ReadMessage(client.get(), decoder);

  client.reset();
  server_thread.StopAndJoin();

  ASSERT_EQ(response.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(response.message.has_value());
  EXPECT_EQ(response.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.message->request_id, 9U);
  EXPECT_EQ(response.message->payload, (std::vector<protocol::Byte>{0x09}));
}

TEST(EpollEchoIntegrationTest, StopsWhenConfiguredShutdownSignalArrives) {
  EpollEchoServerOptions options;
  options.worker_count = 1;
  options.work_queue_capacity = 4;
  options.shutdown_signals = {SIGUSR1};

  EpollEchoServer server(0, std::move(options));
  EpollServerThread server_thread(server);

  ASSERT_EQ(::kill(::getpid(), SIGUSR1), 0);
  server_thread.Join();

  EXPECT_EQ(server.live_connection_count(), 0U);
}

TEST(EpollEchoIntegrationTest, ClosesConnectionForMalformedFrameAndKeepsServerRunning) {
  EpollEchoServer server(0);
  EpollServerThread server_thread(server);

  UniqueFd bad_client = ConnectTcp("127.0.0.1", server.port());
  SetReceiveTimeout(bad_client.get());

  const std::array<protocol::Byte, protocol::kHeaderSize> invalid_header{};
  SendBytes(bad_client.get(), invalid_header);

  std::array<protocol::Byte, 1> byte{};
  const auto received = ::recv(bad_client.get(), byte.data(), byte.size(), 0);
  ASSERT_EQ(received, 0);

  const auto response = SendRequestAndReadResponse("127.0.0.1", server.port(),
                                                  EchoRequest(55, {0x55}));

  server_thread.StopAndJoin();

  EXPECT_EQ(response.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.request_id, 55U);
  EXPECT_EQ(response.payload, (std::vector<protocol::Byte>{0x55}));
}

}  // namespace pulsecore::network
