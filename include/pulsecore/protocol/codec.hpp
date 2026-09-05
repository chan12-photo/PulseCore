#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace pulsecore::protocol {

using Byte = std::uint8_t;

constexpr std::uint32_t kMagic = 0x50554C53U;  // "PULS"
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 20;
constexpr std::uint32_t kMaxPayloadSize = 64U * 1024U;

enum class MessageType : std::uint16_t {
  kEchoRequest = 1,
  kEchoResponse = 2,
  kWorkRequest = 3,
  kWorkResponse = 4,
  kErrorResponse = 5,
};

struct Message {
  MessageType type;
  std::uint64_t request_id;
  std::vector<Byte> payload;
};

enum class ProtocolError {
  kInvalidMagic,
  kUnsupportedVersion,
  kUnknownMessageType,
  kPayloadTooLarge,
  kFrameSizeOverflow,
};

enum class DecodeStatus {
  kNeedMoreData,
  kDecoded,
  kError,
};

struct DecodeResult {
  DecodeStatus status;
  std::optional<Message> message;
  std::optional<ProtocolError> error;
  std::size_t bytes_consumed{0};

  [[nodiscard]] static DecodeResult NeedMoreData() noexcept;
  [[nodiscard]] static DecodeResult Decoded(Message message, std::size_t bytes_consumed);
  [[nodiscard]] static DecodeResult Error(ProtocolError error) noexcept;
};

[[nodiscard]] bool IsKnownMessageType(std::uint16_t type) noexcept;
[[nodiscard]] std::optional<MessageType> ToMessageType(std::uint16_t type) noexcept;

[[nodiscard]] std::optional<std::vector<Byte>> EncodeMessage(const Message& message);
[[nodiscard]] DecodeResult TryDecodeFrame(std::span<const Byte> bytes);

class FrameDecoder {
 public:
  void Append(std::span<const Byte> bytes);

  [[nodiscard]] DecodeResult Next();
  [[nodiscard]] std::size_t BufferedSize() const noexcept;

 private:
  void CompactIfNeeded();

  std::vector<Byte> buffer_;
  std::size_t read_offset_{0};
};

}  // namespace pulsecore::protocol
