#include "pulsecore/network/epoll_echo_server.hpp"

#include "pulsecore/network/blocking_tcp.hpp"

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <array>
#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace pulsecore::network {
namespace {

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

 private:
  EpollEchoServer& server_;
  std::thread thread_;
  std::exception_ptr error_;
};

protocol::Message EchoRequest(std::uint64_t request_id, std::vector<protocol::Byte> payload) {
  return protocol::Message{
      .type = protocol::MessageType::kEchoRequest,
      .request_id = request_id,
      .payload = std::move(payload),
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
