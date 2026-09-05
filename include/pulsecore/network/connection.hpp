#pragma once

#include "pulsecore/common/unique_fd.hpp"
#include "pulsecore/protocol/codec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pulsecore::network {

struct ConnectionId {
  std::uint64_t value{0};

  friend constexpr bool operator==(ConnectionId lhs, ConnectionId rhs) noexcept {
    return lhs.value == rhs.value;
  }
};

struct ConnectionLimits {
  std::size_t max_input_buffer{protocol::kHeaderSize + protocol::kMaxPayloadSize};
  std::size_t max_output_buffer{protocol::kHeaderSize + protocol::kMaxPayloadSize};
};

constexpr std::size_t kDefaultMaxReadBytesPerEvent = 64U * 1024U;
constexpr std::size_t kDefaultMaxWriteBytesPerEvent = 64U * 1024U;

enum class ReadAvailableStatus {
  kOk,
  kWouldBlock,
  kPeerClosed,
  kProtocolError,
};

struct ReadAvailableResult {
  ReadAvailableStatus status;
  std::vector<protocol::Message> messages;
  std::optional<protocol::ProtocolError> protocol_error;

  [[nodiscard]] static ReadAvailableResult Ok(std::vector<protocol::Message> messages);
  [[nodiscard]] static ReadAvailableResult WouldBlock();
  [[nodiscard]] static ReadAvailableResult PeerClosed(std::vector<protocol::Message> messages);
  [[nodiscard]] static ReadAvailableResult ProtocolError(
      protocol::ProtocolError error,
      std::vector<protocol::Message> messages = {});
};

enum class WriteAvailableStatus {
  kOk,
  kWouldBlock,
  kPeerClosed,
};

struct WriteAvailableResult {
  WriteAvailableStatus status;
  std::size_t bytes_written{0};
};

void SetNonBlocking(int fd);
[[nodiscard]] bool IsNonBlocking(int fd);

class Connection {
 public:
  Connection(ConnectionId id, UniqueFd fd, ConnectionLimits limits = {});

  [[nodiscard]] ConnectionId id() const noexcept;
  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] bool has_pending_output() const noexcept;
  [[nodiscard]] std::size_t pending_output_bytes() const noexcept;
  [[nodiscard]] std::size_t buffered_input_bytes() const noexcept;

  [[nodiscard]] ReadAvailableResult ReadAvailable(
      std::size_t max_read_bytes = kDefaultMaxReadBytesPerEvent);
  [[nodiscard]] bool QueueOutput(const protocol::Message& message);
  [[nodiscard]] WriteAvailableResult WriteAvailable(
      std::size_t max_write_bytes = kDefaultMaxWriteBytesPerEvent);

 private:
  std::optional<protocol::ProtocolError> DrainDecoder(std::vector<protocol::Message>& messages);

  ConnectionId id_;
  UniqueFd fd_;
  ConnectionLimits limits_;
  protocol::FrameDecoder decoder_;
  std::vector<protocol::Byte> output_buffer_;
  std::size_t output_offset_{0};
};

}  // namespace pulsecore::network
