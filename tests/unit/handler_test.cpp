#include "pulsecore/core/handler.hpp"

#include <vector>

#include <gtest/gtest.h>

namespace pulsecore::core {

TEST(HandlerTest, EchoRequestReturnsEchoResponseWithSameRequestIdAndPayload) {
  const auto response = HandleRequest(protocol::Message{
      .type = protocol::MessageType::kEchoRequest,
      .request_id = 42,
      .payload = {0x01, 0x02, 0x03},
  });

  EXPECT_EQ(response.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.request_id, 42U);
  EXPECT_EQ(response.payload, (std::vector<protocol::Byte>{0x01, 0x02, 0x03}));
}

TEST(HandlerTest, WorkRequestReturnsWorkResponseWithSameRequestIdAndPayload) {
  const auto response = HandleRequest(protocol::Message{
      .type = protocol::MessageType::kWorkRequest,
      .request_id = 99,
      .payload = {0xAA},
  });

  EXPECT_EQ(response.type, protocol::MessageType::kWorkResponse);
  EXPECT_EQ(response.request_id, 99U);
  EXPECT_EQ(response.payload, (std::vector<protocol::Byte>{0xAA}));
}

TEST(HandlerTest, ResponseMessageFromClientReturnsErrorResponse) {
  const auto response = HandleRequest(protocol::Message{
      .type = protocol::MessageType::kEchoResponse,
      .request_id = 77,
      .payload = {},
  });

  EXPECT_EQ(response.type, protocol::MessageType::kErrorResponse);
  EXPECT_EQ(response.request_id, 77U);
  EXPECT_FALSE(response.payload.empty());
}

}  // namespace pulsecore::core
