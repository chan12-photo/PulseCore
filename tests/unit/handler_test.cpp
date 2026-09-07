#include "pulsecore/core/handler.hpp"

#include <cstddef>
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

TEST(HandlerTest, EncodeWorkRequestPayloadPrefixesBigEndianIterationCount) {
  const std::vector<protocol::Byte> seed{0xAA, 0xBB};

  const auto payload = EncodeWorkRequestPayload(0x01020304U, seed);

  EXPECT_EQ(payload, (std::vector<protocol::Byte>{0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB}));
}

TEST(HandlerTest, WorkRequestReturnsDeterministicDigest) {
  const std::vector<protocol::Byte> seed{0xAA, 0xBB};

  const auto response = HandleRequest(protocol::Message{
      .type = protocol::MessageType::kWorkRequest,
      .request_id = 99,
      .payload = EncodeWorkRequestPayload(3, seed),
  });

  EXPECT_EQ(response.type, protocol::MessageType::kWorkResponse);
  EXPECT_EQ(response.request_id, 99U);
  EXPECT_EQ(response.payload,
            (std::vector<protocol::Byte>{0xA2, 0x89, 0x7E, 0x00, 0x44, 0x62, 0x2F, 0x90}));
}

TEST(HandlerTest, WorkRequestRejectsMissingIterationPrefix) {
  const auto response = HandleRequest(protocol::Message{
      .type = protocol::MessageType::kWorkRequest,
      .request_id = 100,
      .payload = {0xAA},
  });

  EXPECT_EQ(response.type, protocol::MessageType::kErrorResponse);
  EXPECT_EQ(response.request_id, 100U);
  EXPECT_FALSE(response.payload.empty());
}

TEST(HandlerTest, WorkRequestRejectsExcessiveIterations) {
  const auto response = HandleRequest(protocol::Message{
      .type = protocol::MessageType::kWorkRequest,
      .request_id = 101,
      .payload = EncodeWorkRequestPayload(kMaxWorkIterations + 1U,
                                           std::vector<protocol::Byte>{}),
  });

  EXPECT_EQ(response.type, protocol::MessageType::kErrorResponse);
  EXPECT_EQ(response.request_id, 101U);
  EXPECT_FALSE(response.payload.empty());
}

TEST(HandlerTest, WorkRequestRejectsExcessiveTotalWorkUnits) {
  const std::size_t seed_size =
      static_cast<std::size_t>(kMaxWorkUnits / kMaxWorkIterations) + 1U;

  const auto response = HandleRequest(protocol::Message{
      .type = protocol::MessageType::kWorkRequest,
      .request_id = 102,
      .payload = EncodeWorkRequestPayload(kMaxWorkIterations,
                                           std::vector<protocol::Byte>(seed_size, 0xAA)),
  });

  EXPECT_EQ(response.type, protocol::MessageType::kErrorResponse);
  EXPECT_EQ(response.request_id, 102U);
  EXPECT_FALSE(response.payload.empty());
}

TEST(HandlerTest, OwnedEchoRequestReturnsEchoResponseWithSameRequestIdAndPayload) {
  auto response = HandleOwnedRequest(protocol::Message{
      .type = protocol::MessageType::kEchoRequest,
      .request_id = 123,
      .payload = {0x10, 0x20},
  });

  EXPECT_EQ(response.type, protocol::MessageType::kEchoResponse);
  EXPECT_EQ(response.request_id, 123U);
  EXPECT_EQ(response.payload, (std::vector<protocol::Byte>{0x10, 0x20}));
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
