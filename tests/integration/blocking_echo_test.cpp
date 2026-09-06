#include "pulsecore/network/blocking_tcp.hpp"

#include "pulsecore/core/handler.hpp"

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace pulsecore::network {
namespace {

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

class ServerThread {
 public:
  explicit ServerThread(BlockingEchoServer& server)
      : thread_([this, &server] {
          try {
            server.ServeOne();
          } catch (...) {
            error_ = std::current_exception();
          }
        }) {}

  ServerThread(const ServerThread&) = delete;
  ServerThread& operator=(const ServerThread&) = delete;

  ~ServerThread() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void JoinAndRethrow() {
    thread_.join();
    if (error_ != nullptr) {
      std::rethrow_exception(error_);
    }
  }

 private:
  std::thread thread_;
  std::exception_ptr error_;
};

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

TEST(BlockingEchoIntegrationTest, EchoesSingleRequestOverTcp) {
  BlockingEchoServer server(0);

  ServerThread server_thread(server);

  const auto response = SendRequestAndReadResponse("127.0.0.1", server.port(),
                                                  EchoRequest(42, {0x41, 0x42, 0x43}));

  server_thread.JoinAndRethrow();

  EXPECT_EQ(response.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.request_id, 42U);
  EXPECT_EQ(response.payload, (std::vector<protocol::Byte>{0x41, 0x42, 0x43}));
}

TEST(BlockingEchoIntegrationTest, HandlesWorkRequestOverTcp) {
  BlockingEchoServer server(0);

  ServerThread server_thread(server);
  const std::vector<protocol::Byte> seed{0xAA, 0xBB};

  const auto response =
      SendRequestAndReadResponse("127.0.0.1", server.port(), WorkRequest(99, 3, seed));

  server_thread.JoinAndRethrow();

  EXPECT_EQ(response.type, protocol::MessageType::kWorkResponse);
  EXPECT_EQ(response.request_id, 99U);
  EXPECT_EQ(response.payload,
            (std::vector<protocol::Byte>{0xA2, 0x89, 0x7E, 0x00, 0x44, 0x62, 0x2F, 0x90}));
}

TEST(BlockingEchoIntegrationTest, HandlesMultipleFramesOnOneConnection) {
  BlockingEchoServer server(0);

  ServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SendMessage(client.get(), EchoRequest(1, {0x01}));
  SendMessage(client.get(), EchoRequest(2, {0x02, 0x03}));

  protocol::FrameDecoder decoder;
  auto first = ReadMessage(client.get(), decoder);
  auto second = ReadMessage(client.get(), decoder);

  client.reset();
  server_thread.JoinAndRethrow();

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

TEST(BlockingEchoIntegrationTest, ReturnsErrorResponseForUnexpectedResponseMessage) {
  BlockingEchoServer server(0);

  ServerThread server_thread(server);

  const auto response = SendRequestAndReadResponse(
      "127.0.0.1", server.port(),
      protocol::Message{
          .type = protocol::MessageType::kEchoResponse,
          .request_id = 77,
          .payload = {},
      });

  server_thread.JoinAndRethrow();

  EXPECT_EQ(response.type, protocol::MessageType::kErrorResponse);
  EXPECT_EQ(response.request_id, 77U);
  EXPECT_FALSE(response.payload.empty());
}

TEST(BlockingEchoIntegrationTest, HandlesFrameWrittenInTwoChunks) {
  BlockingEchoServer server(0);

  ServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  const auto encoded = protocol::EncodeMessage(EchoRequest(123, {0x11, 0x22, 0x33}));
  ASSERT_TRUE(encoded.has_value());

  const std::size_t split = protocol::kHeaderSize - 3U;
  SendBytes(client.get(), std::span<const protocol::Byte>(encoded->data(), split));
  SendBytes(client.get(),
            std::span<const protocol::Byte>(encoded->data() + split, encoded->size() - split));

  protocol::FrameDecoder decoder;
  auto response = ReadMessage(client.get(), decoder);

  client.reset();
  server_thread.JoinAndRethrow();

  ASSERT_EQ(response.status, ReadMessageStatus::kMessage);
  ASSERT_TRUE(response.message.has_value());
  EXPECT_EQ(response.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.message->request_id, 123U);
  EXPECT_EQ(response.message->payload, (std::vector<protocol::Byte>{0x11, 0x22, 0x33}));
}

TEST(BlockingEchoIntegrationTest, ClosesConnectionForMalformedFrame) {
  BlockingEchoServer server(0);

  ServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  SetReceiveTimeout(client.get());

  const std::array<protocol::Byte, protocol::kHeaderSize> invalid_header{};
  SendBytes(client.get(), invalid_header);

  std::array<protocol::Byte, 1> byte{};
  const auto received = ::recv(client.get(), byte.data(), byte.size(), 0);

  server_thread.JoinAndRethrow();

  EXPECT_EQ(received, 0);
}

TEST(BlockingEchoIntegrationTest, ReturnsWhenClientDisconnectsWithoutSendingFrame) {
  BlockingEchoServer server(0);

  ServerThread server_thread(server);

  UniqueFd client = ConnectTcp("127.0.0.1", server.port());
  client.reset();

  server_thread.JoinAndRethrow();
}

}  // namespace pulsecore::network
