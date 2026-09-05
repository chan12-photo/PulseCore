#include "pulsecore/protocol/codec.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace pulsecore::protocol {
namespace {

void AppendU16(std::vector<Byte>& out, std::uint16_t value) {
  out.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<Byte>(value & 0xFFU));
}

void AppendU32(std::vector<Byte>& out, std::uint32_t value) {
  out.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<Byte>(value & 0xFFU));
}

void AppendU64(std::vector<Byte>& out, std::uint64_t value) {
  out.push_back(static_cast<Byte>((value >> 56U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 48U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 40U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 32U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<Byte>(value & 0xFFU));
}

std::uint16_t ReadU16(std::span<const Byte> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[offset + 1]));
}

std::uint32_t ReadU32(std::span<const Byte> bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint64_t ReadU64(std::span<const Byte> bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(bytes[offset]) << 56U) |
         (static_cast<std::uint64_t>(bytes[offset + 1]) << 48U) |
         (static_cast<std::uint64_t>(bytes[offset + 2]) << 40U) |
         (static_cast<std::uint64_t>(bytes[offset + 3]) << 32U) |
         (static_cast<std::uint64_t>(bytes[offset + 4]) << 24U) |
         (static_cast<std::uint64_t>(bytes[offset + 5]) << 16U) |
         (static_cast<std::uint64_t>(bytes[offset + 6]) << 8U) |
         static_cast<std::uint64_t>(bytes[offset + 7]);
}

}  // namespace

DecodeResult DecodeResult::NeedMoreData() noexcept {
  return DecodeResult{.status = DecodeStatus::kNeedMoreData};
}

DecodeResult DecodeResult::Decoded(Message message, std::size_t bytes_consumed) {
  return DecodeResult{
      .status = DecodeStatus::kDecoded,
      .message = std::move(message),
      .bytes_consumed = bytes_consumed,
  };
}

DecodeResult DecodeResult::Error(ProtocolError error) noexcept {
  return DecodeResult{.status = DecodeStatus::kError, .error = error};
}

bool IsKnownMessageType(std::uint16_t type) noexcept {
  switch (static_cast<MessageType>(type)) {
    case MessageType::kEchoRequest:
    case MessageType::kEchoResponse:
    case MessageType::kWorkRequest:
    case MessageType::kWorkResponse:
    case MessageType::kErrorResponse:
      return true;
  }
  return false;
}

std::optional<MessageType> ToMessageType(std::uint16_t type) noexcept {
  if (!IsKnownMessageType(type)) {
    return std::nullopt;
  }
  return static_cast<MessageType>(type);
}

std::optional<std::vector<Byte>> EncodeMessage(const Message& message) {
  if (message.payload.size() > kMaxPayloadSize) {
    return std::nullopt;
  }

  const auto raw_type = static_cast<std::uint16_t>(message.type);
  if (!IsKnownMessageType(raw_type)) {
    return std::nullopt;
  }

  std::vector<Byte> out;
  out.reserve(kHeaderSize + message.payload.size());

  AppendU32(out, kMagic);
  AppendU16(out, kVersion);
  AppendU16(out, raw_type);
  AppendU32(out, static_cast<std::uint32_t>(message.payload.size()));
  AppendU64(out, message.request_id);
  out.insert(out.end(), message.payload.begin(), message.payload.end());

  return out;
}

DecodeResult TryDecodeFrame(std::span<const Byte> bytes) {
  if (bytes.size() < kHeaderSize) {
    return DecodeResult::NeedMoreData();
  }

  const auto magic = ReadU32(bytes, 0);
  if (magic != kMagic) {
    return DecodeResult::Error(ProtocolError::kInvalidMagic);
  }

  const auto version = ReadU16(bytes, 4);
  if (version != kVersion) {
    return DecodeResult::Error(ProtocolError::kUnsupportedVersion);
  }

  const auto raw_type = ReadU16(bytes, 6);
  const auto message_type = ToMessageType(raw_type);
  if (!message_type.has_value()) {
    return DecodeResult::Error(ProtocolError::kUnknownMessageType);
  }

  const auto payload_size = ReadU32(bytes, 8);
  if (payload_size > kMaxPayloadSize) {
    return DecodeResult::Error(ProtocolError::kPayloadTooLarge);
  }

  const auto max_size = std::numeric_limits<std::size_t>::max();
  if (static_cast<std::size_t>(payload_size) > max_size - kHeaderSize) {
    return DecodeResult::Error(ProtocolError::kFrameSizeOverflow);
  }

  const auto frame_size = kHeaderSize + static_cast<std::size_t>(payload_size);
  if (bytes.size() < frame_size) {
    return DecodeResult::NeedMoreData();
  }

  const auto request_id = ReadU64(bytes, 12);
  Message message{
      .type = *message_type,
      .request_id = request_id,
      .payload = std::vector<Byte>(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(frame_size)),
  };

  return DecodeResult::Decoded(std::move(message), frame_size);
}

void FrameDecoder::Append(std::span<const Byte> bytes) {
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

DecodeResult FrameDecoder::Next() {
  const auto pending = std::span<const Byte>(buffer_).subspan(read_offset_);
  auto result = TryDecodeFrame(pending);
  if (result.status == DecodeStatus::kDecoded) {
    read_offset_ += result.bytes_consumed;
    CompactIfNeeded();
  }
  return result;
}

std::size_t FrameDecoder::BufferedSize() const noexcept {
  return buffer_.size() - read_offset_;
}

void FrameDecoder::CompactIfNeeded() {
  if (read_offset_ == 0) {
    return;
  }

  if (read_offset_ == buffer_.size()) {
    buffer_.clear();
    read_offset_ = 0;
    return;
  }

  if (read_offset_ >= buffer_.size() / 2U) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_));
    read_offset_ = 0;
  }
}

}  // namespace pulsecore::protocol
