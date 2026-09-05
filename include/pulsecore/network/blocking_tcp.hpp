#pragma once

#include "pulsecore/common/unique_fd.hpp"
#include "pulsecore/protocol/codec.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace pulsecore::network {

class BlockingEchoServer {
 public:
  explicit BlockingEchoServer(std::uint16_t port);

  [[nodiscard]] std::uint16_t port() const noexcept;

  void ServeOne();

 private:
  UniqueFd listener_;
  std::uint16_t port_{0};
};

enum class ReadMessageStatus {
  kMessage,
  kClosed,
  kProtocolError,
};

struct ReadMessageResult {
  ReadMessageStatus status;
  std::optional<protocol::Message> message;
  std::optional<protocol::ProtocolError> protocol_error;

  [[nodiscard]] static ReadMessageResult Message(protocol::Message message);
  [[nodiscard]] static ReadMessageResult Closed() noexcept;
  [[nodiscard]] static ReadMessageResult ProtocolError(protocol::ProtocolError error) noexcept;
};

[[nodiscard]] UniqueFd ConnectTcp(std::string_view host, std::uint16_t port);

void SendMessage(int fd, const protocol::Message& message);

[[nodiscard]] ReadMessageResult ReadMessage(int fd, protocol::FrameDecoder& decoder);

[[nodiscard]] protocol::Message SendRequestAndReadResponse(std::string_view host,
                                                           std::uint16_t port,
                                                           const protocol::Message& request);

}  // namespace pulsecore::network
