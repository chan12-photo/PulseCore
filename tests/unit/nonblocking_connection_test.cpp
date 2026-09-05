#include "pulsecore/network/connection.hpp"

#include "pulsecore/protocol/codec.hpp"

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace pulsecore::network {
namespace {

struct SocketPair {
  UniqueFd connection_end;
  UniqueFd peer_end;
};

SocketPair MakeSocketPair() {
  std::array<int, 2> fds{-1, -1};
  EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()), 0);

  return SocketPair{
      .connection_end = UniqueFd(fds[0]),
      .peer_end = UniqueFd(fds[1]),
  };
}

protocol::Message EchoRequest(std::uint64_t request_id, std::vector<protocol::Byte> payload) {
  return protocol::Message{
      .type = protocol::MessageType::kEchoRequest,
      .request_id = request_id,
      .payload = std::move(payload),
  };
}

protocol::Message EchoResponse(std::uint64_t request_id, std::vector<protocol::Byte> payload) {
  return protocol::Message{
      .type = protocol::MessageType::kEchoResponse,
      .request_id = request_id,
      .payload = std::move(payload),
  };
}

std::vector<protocol::Byte> EncodeOrDie(const protocol::Message& message) {
  auto encoded = protocol::EncodeMessage(message);
  EXPECT_TRUE(encoded.has_value());
  return *encoded;
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

TEST(NonBlockingConnectionTest, SetNonBlockingMarksFdNonBlocking) {
  auto pair = MakeSocketPair();

  EXPECT_FALSE(IsNonBlocking(pair.connection_end.get()));

  SetNonBlocking(pair.connection_end.get());

  EXPECT_TRUE(IsNonBlocking(pair.connection_end.get()));
}

TEST(NonBlockingConnectionTest, ReadAvailableReturnsWouldBlockWithoutData) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());

  Connection connection(ConnectionId{1}, std::move(pair.connection_end));

  const auto result = connection.ReadAvailable();

  EXPECT_EQ(result.status, ReadAvailableStatus::kWouldBlock);
  EXPECT_TRUE(result.messages.empty());
}

TEST(NonBlockingConnectionTest, ReadsCompleteFrame) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());

  Connection connection(ConnectionId{7}, std::move(pair.connection_end));
  SendBytes(pair.peer_end.get(), EncodeOrDie(EchoRequest(42, {0xAA, 0xBB})));

  const auto result = connection.ReadAvailable();

  ASSERT_EQ(result.status, ReadAvailableStatus::kOk);
  ASSERT_EQ(result.messages.size(), 1U);
  EXPECT_EQ(result.messages[0].type, protocol::MessageType::kEchoRequest);
  EXPECT_EQ(result.messages[0].request_id, 42U);
  EXPECT_EQ(result.messages[0].payload, (std::vector<protocol::Byte>{0xAA, 0xBB}));
}

TEST(NonBlockingConnectionTest, BuffersFragmentedFrameUntilComplete) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());

  Connection connection(ConnectionId{7}, std::move(pair.connection_end));
  const auto encoded = EncodeOrDie(EchoRequest(42, {0xAA, 0xBB, 0xCC}));

  const std::size_t split = protocol::kHeaderSize - 1U;
  SendBytes(pair.peer_end.get(), std::span<const protocol::Byte>(encoded.data(), split));

  auto result = connection.ReadAvailable();

  ASSERT_EQ(result.status, ReadAvailableStatus::kOk);
  EXPECT_TRUE(result.messages.empty());
  EXPECT_EQ(connection.buffered_input_bytes(), split);

  SendBytes(pair.peer_end.get(),
            std::span<const protocol::Byte>(encoded.data() + split, encoded.size() - split));

  result = connection.ReadAvailable();

  ASSERT_EQ(result.status, ReadAvailableStatus::kOk);
  ASSERT_EQ(result.messages.size(), 1U);
  EXPECT_EQ(result.messages[0].request_id, 42U);
  EXPECT_EQ(result.messages[0].payload, (std::vector<protocol::Byte>{0xAA, 0xBB, 0xCC}));
  EXPECT_EQ(connection.buffered_input_bytes(), 0U);
}

TEST(NonBlockingConnectionTest, ReadsMultipleFramesFromOneReadableEvent) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());

  Connection connection(ConnectionId{7}, std::move(pair.connection_end));
  const auto first = EncodeOrDie(EchoRequest(1, {0x01}));
  const auto second = EncodeOrDie(EchoRequest(2, {0x02, 0x03}));

  SendBytes(pair.peer_end.get(), first);
  SendBytes(pair.peer_end.get(), second);

  const auto result = connection.ReadAvailable();

  ASSERT_EQ(result.status, ReadAvailableStatus::kOk);
  ASSERT_EQ(result.messages.size(), 2U);
  EXPECT_EQ(result.messages[0].request_id, 1U);
  EXPECT_EQ(result.messages[0].payload, (std::vector<protocol::Byte>{0x01}));
  EXPECT_EQ(result.messages[1].request_id, 2U);
  EXPECT_EQ(result.messages[1].payload, (std::vector<protocol::Byte>{0x02, 0x03}));
}

TEST(NonBlockingConnectionTest, ReportsPeerClosed) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());

  Connection connection(ConnectionId{7}, std::move(pair.connection_end));
  pair.peer_end.reset();

  const auto result = connection.ReadAvailable();

  EXPECT_EQ(result.status, ReadAvailableStatus::kPeerClosed);
}

TEST(NonBlockingConnectionTest, ReportsProtocolError) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());

  Connection connection(ConnectionId{7}, std::move(pair.connection_end));
  const std::array<protocol::Byte, protocol::kHeaderSize> invalid_header{};

  SendBytes(pair.peer_end.get(), invalid_header);
  const auto result = connection.ReadAvailable();

  EXPECT_EQ(result.status, ReadAvailableStatus::kProtocolError);
  ASSERT_TRUE(result.protocol_error.has_value());
  EXPECT_EQ(*result.protocol_error, protocol::ProtocolError::kInvalidMagic);
}

TEST(NonBlockingConnectionTest, QueuesAndFlushesOutputFrame) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());
  SetReceiveTimeout(pair.peer_end.get());

  Connection connection(ConnectionId{7}, std::move(pair.connection_end));

  ASSERT_TRUE(connection.QueueOutput(EchoResponse(88, {0x10, 0x20})));
  EXPECT_TRUE(connection.has_pending_output());

  const auto write_result = connection.WriteAvailable();

  EXPECT_EQ(write_result.status, WriteAvailableStatus::kOk);
  EXPECT_FALSE(connection.has_pending_output());

  protocol::FrameDecoder decoder;
  std::array<protocol::Byte, 128> bytes{};
  const auto received = ::recv(pair.peer_end.get(), bytes.data(), bytes.size(), 0);

  ASSERT_GT(received, 0);
  decoder.Append(std::span<const protocol::Byte>(bytes.data(), static_cast<std::size_t>(received)));
  const auto decoded = decoder.Next();

  ASSERT_EQ(decoded.status, protocol::DecodeStatus::kDecoded);
  ASSERT_TRUE(decoded.message.has_value());
  EXPECT_EQ(decoded.message->type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(decoded.message->request_id, 88U);
  EXPECT_EQ(decoded.message->payload, (std::vector<protocol::Byte>{0x10, 0x20}));
}

TEST(NonBlockingConnectionTest, RejectsOutputAboveConfiguredLimit) {
  auto pair = MakeSocketPair();
  SetNonBlocking(pair.connection_end.get());

  ConnectionLimits limits{};
  limits.max_output_buffer = protocol::kHeaderSize + 1U;

  Connection connection(ConnectionId{7}, std::move(pair.connection_end), limits);

  EXPECT_FALSE(connection.QueueOutput(EchoResponse(88, {0x10, 0x20})));
  EXPECT_FALSE(connection.has_pending_output());
}

}  // namespace pulsecore::network
