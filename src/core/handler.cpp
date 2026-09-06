#include "pulsecore/core/handler.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace pulsecore::core {
namespace {

std::vector<protocol::Byte> ErrorPayload(std::string_view message) {
  return {message.begin(), message.end()};
}

protocol::Message ErrorResponse(std::uint64_t request_id, std::string_view message) {
  return protocol::Message{
      .type = protocol::MessageType::kErrorResponse,
      .request_id = request_id,
      .payload = ErrorPayload(message),
  };
}

void AppendU32(std::vector<protocol::Byte>& out, std::uint32_t value) {
  out.push_back(static_cast<protocol::Byte>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>(value & 0xFFU));
}

void AppendU64(std::vector<protocol::Byte>& out, std::uint64_t value) {
  out.push_back(static_cast<protocol::Byte>((value >> 56U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 48U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 40U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 32U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<protocol::Byte>(value & 0xFFU));
}

std::uint32_t ReadU32(std::span<const protocol::Byte> bytes) {
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) |
         static_cast<std::uint32_t>(bytes[3]);
}

std::optional<std::vector<protocol::Byte>> ComputeWorkResponsePayload(
    std::span<const protocol::Byte> payload) {
  constexpr std::size_t kIterationBytes = 4;
  constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

  if (payload.size() < kIterationBytes) {
    return std::nullopt;
  }

  const auto iterations = ReadU32(payload.first(kIterationBytes));
  if (iterations > kMaxWorkIterations) {
    return std::nullopt;
  }

  const auto seed = payload.subspan(kIterationBytes);
  std::uint64_t hash = kFnvOffset;
  for (std::uint32_t round = 0; round < iterations; ++round) {
    hash ^= round;
    hash *= kFnvPrime;
    for (const auto byte : seed) {
      hash ^= byte;
      hash *= kFnvPrime;
    }
    hash ^= hash >> 32U;
  }

  std::vector<protocol::Byte> out;
  out.reserve(sizeof(hash));
  AppendU64(out, hash);
  return out;
}

}  // namespace

std::vector<protocol::Byte> EncodeWorkRequestPayload(std::uint32_t iterations,
                                                     std::span<const protocol::Byte> seed) {
  std::vector<protocol::Byte> payload;
  payload.reserve(4U + seed.size());
  AppendU32(payload, iterations);
  payload.insert(payload.end(), seed.begin(), seed.end());
  return payload;
}

protocol::Message HandleRequest(const protocol::Message& request) {
  return HandleOwnedRequest(protocol::Message{
      .type = request.type,
      .request_id = request.request_id,
      .payload = request.payload,
  });
}

protocol::Message HandleOwnedRequest(protocol::Message request) {
  switch (request.type) {
    case protocol::MessageType::kEchoRequest:
      return protocol::Message{
          .type = protocol::MessageType::kEchoResponse,
          .request_id = request.request_id,
          .payload = std::move(request.payload),
      };
    case protocol::MessageType::kWorkRequest:
      if (auto payload = ComputeWorkResponsePayload(request.payload); payload.has_value()) {
        return protocol::Message{
            .type = protocol::MessageType::kWorkResponse,
            .request_id = request.request_id,
            .payload = std::move(*payload),
        };
      }
      return ErrorResponse(request.request_id, "invalid work request payload");
    case protocol::MessageType::kEchoResponse:
    case protocol::MessageType::kWorkResponse:
    case protocol::MessageType::kErrorResponse:
      return protocol::Message{
          .type = protocol::MessageType::kErrorResponse,
          .request_id = request.request_id,
          .payload = ErrorPayload("unexpected response message from client"),
      };
  }

  return ErrorResponse(request.request_id, "unknown request message");
}

}  // namespace pulsecore::core
