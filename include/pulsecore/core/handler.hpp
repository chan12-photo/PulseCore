#pragma once

#include "pulsecore/protocol/codec.hpp"

namespace pulsecore::core {

[[nodiscard]] protocol::Message HandleRequest(const protocol::Message& request);
[[nodiscard]] protocol::Message HandleOwnedRequest(protocol::Message request);

}  // namespace pulsecore::core
