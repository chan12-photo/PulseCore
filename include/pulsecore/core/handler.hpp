#pragma once

#include "pulsecore/protocol/codec.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace pulsecore::core {

constexpr std::uint32_t kMaxWorkIterations = 1'000'000U;

[[nodiscard]] std::vector<protocol::Byte> EncodeWorkRequestPayload(
    std::uint32_t iterations,
    std::span<const protocol::Byte> seed);

[[nodiscard]] protocol::Message HandleRequest(const protocol::Message& request);
[[nodiscard]] protocol::Message HandleOwnedRequest(protocol::Message request);

}  // namespace pulsecore::core
