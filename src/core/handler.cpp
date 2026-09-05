#include "pulsecore/core/handler.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace pulsecore::core {
namespace {

std::vector<protocol::Byte> ErrorPayload(std::string_view message) {
  return {message.begin(), message.end()};
}

}  // namespace

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
      return protocol::Message{
          .type = protocol::MessageType::kWorkResponse,
          .request_id = request.request_id,
          .payload = std::move(request.payload),
      };
    case protocol::MessageType::kEchoResponse:
    case protocol::MessageType::kWorkResponse:
    case protocol::MessageType::kErrorResponse:
      return protocol::Message{
          .type = protocol::MessageType::kErrorResponse,
          .request_id = request.request_id,
          .payload = ErrorPayload("unexpected response message from client"),
      };
  }

  return protocol::Message{
      .type = protocol::MessageType::kErrorResponse,
      .request_id = request.request_id,
      .payload = ErrorPayload("unknown request message"),
  };
}

}  // namespace pulsecore::core
