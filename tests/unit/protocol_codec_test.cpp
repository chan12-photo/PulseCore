#include "pulsecore/protocol/codec.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace pulsecore::protocol {
namespace {

std::vector<Byte> MakeHeader(std::uint32_t magic,
                             std::uint16_t version,
                             std::uint16_t type,
                             std::uint32_t payload_size,
                             std::uint64_t request_id) {
  return {
      static_cast<Byte>((magic >> 24U) & 0xFFU),
      static_cast<Byte>((magic >> 16U) & 0xFFU),
      static_cast<Byte>((magic >> 8U) & 0xFFU),
      static_cast<Byte>(magic & 0xFFU),
      static_cast<Byte>((version >> 8U) & 0xFFU),
      static_cast<Byte>(version & 0xFFU),
      static_cast<Byte>((type >> 8U) & 0xFFU),
      static_cast<Byte>(type & 0xFFU),
      static_cast<Byte>((payload_size >> 24U) & 0xFFU),
      static_cast<Byte>((payload_size >> 16U) & 0xFFU),
      static_cast<Byte>((payload_size >> 8U) & 0xFFU),
      static_cast<Byte>(payload_size & 0xFFU),
      static_cast<Byte>((request_id >> 56U) & 0xFFU),
      static_cast<Byte>((request_id >> 48U) & 0xFFU),
      static_cast<Byte>((request_id >> 40U) & 0xFFU),
      static_cast<Byte>((request_id >> 32U) & 0xFFU),
      static_cast<Byte>((request_id >> 24U) & 0xFFU),
      static_cast<Byte>((request_id >> 16U) & 0xFFU),
      static_cast<Byte>((request_id >> 8U) & 0xFFU),
      static_cast<Byte>(request_id & 0xFFU),
  };
}

std::vector<Byte> EncodedEcho(std::uint64_t request_id, std::vector<Byte> payload) {
  auto encoded = EncodeMessage(Message{
      .type = MessageType::kEchoRequest,
      .request_id = request_id,
      .payload = std::move(payload),
  });
  EXPECT_TRUE(encoded.has_value());
  return *encoded;
}

}  // namespace

TEST(ProtocolCodecTest, EncodesHeaderAsGoldenBigEndianBytes) {
  const auto encoded = EncodeMessage(Message{
      .type = MessageType::kEchoRequest,
      .request_id = 0x0102030405060708ULL,
      .payload = {0x41, 0x42},
  });

  ASSERT_TRUE(encoded.has_value());

  const std::vector<Byte> expected{
      0x50, 0x55, 0x4C, 0x53,
      0x00, 0x01,
      0x00, 0x01,
      0x00, 0x00, 0x00, 0x02,
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x41, 0x42,
  };

  EXPECT_EQ(*encoded, expected);
}

TEST(ProtocolCodecTest, DecodesEncodedFrame) {
  const auto encoded = EncodedEcho(42, {0xAA, 0xBB, 0xCC});

  const auto result = TryDecodeFrame(encoded);

  ASSERT_EQ(result.status, DecodeStatus::kDecoded);
  ASSERT_TRUE(result.message.has_value());
  EXPECT_EQ(result.bytes_consumed, encoded.size());
  EXPECT_EQ(result.message->type, MessageType::kEchoRequest);
  EXPECT_EQ(result.message->request_id, 42U);
  EXPECT_EQ(result.message->payload, (std::vector<Byte>{0xAA, 0xBB, 0xCC}));
}

TEST(ProtocolCodecTest, AllowsZeroPayloadFrame) {
  const auto encoded = EncodedEcho(7, {});

  const auto result = TryDecodeFrame(encoded);

  ASSERT_EQ(result.status, DecodeStatus::kDecoded);
  ASSERT_TRUE(result.message.has_value());
  EXPECT_TRUE(result.message->payload.empty());
}

TEST(ProtocolCodecTest, AllowsMaximumPayloadFrame) {
  std::vector<Byte> payload(kMaxPayloadSize, 0x5A);
  const auto encoded = EncodeMessage(Message{
      .type = MessageType::kEchoRequest,
      .request_id = 1,
      .payload = payload,
  });

  ASSERT_TRUE(encoded.has_value());
  const auto result = TryDecodeFrame(*encoded);

  ASSERT_EQ(result.status, DecodeStatus::kDecoded);
  ASSERT_TRUE(result.message.has_value());
  EXPECT_EQ(result.message->payload.size(), kMaxPayloadSize);
  EXPECT_TRUE(std::all_of(result.message->payload.begin(), result.message->payload.end(),
                          [](Byte value) { return value == 0x5A; }));
}

TEST(ProtocolCodecTest, EncodeRejectsOversizedPayload) {
  const auto encoded = EncodeMessage(Message{
      .type = MessageType::kEchoRequest,
      .request_id = 1,
      .payload = std::vector<Byte>(kMaxPayloadSize + 1U, 0x00),
  });

  EXPECT_FALSE(encoded.has_value());
}

TEST(ProtocolCodecTest, NeedsMoreDataForPartialHeader) {
  const std::vector<Byte> partial_header(kHeaderSize - 1U, 0x00);

  const auto result = TryDecodeFrame(partial_header);

  EXPECT_EQ(result.status, DecodeStatus::kNeedMoreData);
  EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(ProtocolCodecTest, NeedsMoreDataForPartialPayload) {
  auto bytes = MakeHeader(kMagic, kVersion, static_cast<std::uint16_t>(MessageType::kEchoRequest),
                          3, 9);
  bytes.push_back(0xAA);
  bytes.push_back(0xBB);

  const auto result = TryDecodeFrame(bytes);

  EXPECT_EQ(result.status, DecodeStatus::kNeedMoreData);
  EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(ProtocolCodecTest, RejectsInvalidMagic) {
  const auto bytes = MakeHeader(0xDEADBEEFU, kVersion,
                                static_cast<std::uint16_t>(MessageType::kEchoRequest), 0, 1);

  const auto result = TryDecodeFrame(bytes);

  EXPECT_EQ(result.status, DecodeStatus::kError);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(*result.error, ProtocolError::kInvalidMagic);
}

TEST(ProtocolCodecTest, RejectsUnsupportedVersion) {
  const auto bytes = MakeHeader(kMagic, kVersion + 1U,
                                static_cast<std::uint16_t>(MessageType::kEchoRequest), 0, 1);

  const auto result = TryDecodeFrame(bytes);

  EXPECT_EQ(result.status, DecodeStatus::kError);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(*result.error, ProtocolError::kUnsupportedVersion);
}

TEST(ProtocolCodecTest, RejectsUnknownMessageType) {
  const auto bytes = MakeHeader(kMagic, kVersion, 0xFFFFU, 0, 1);

  const auto result = TryDecodeFrame(bytes);

  EXPECT_EQ(result.status, DecodeStatus::kError);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(*result.error, ProtocolError::kUnknownMessageType);
}

TEST(ProtocolCodecTest, RejectsOversizedPayloadLength) {
  const auto bytes = MakeHeader(kMagic, kVersion,
                                static_cast<std::uint16_t>(MessageType::kEchoRequest),
                                kMaxPayloadSize + 1U, 1);

  const auto result = TryDecodeFrame(bytes);

  EXPECT_EQ(result.status, DecodeStatus::kError);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(*result.error, ProtocolError::kPayloadTooLarge);
}

TEST(FrameDecoderTest, HandlesEverySplitPosition) {
  const auto encoded = EncodedEcho(99, {0x10, 0x20, 0x30, 0x40});

  for (std::size_t split = 0; split <= encoded.size(); ++split) {
    FrameDecoder decoder;

    decoder.Append(std::span<const Byte>(encoded.data(), split));
    auto result = decoder.Next();

    if (split == encoded.size()) {
      ASSERT_EQ(result.status, DecodeStatus::kDecoded) << "split=" << split;
      ASSERT_TRUE(result.message.has_value()) << "split=" << split;
      EXPECT_EQ(result.message->request_id, 99U) << "split=" << split;
      EXPECT_EQ(result.message->payload, (std::vector<Byte>{0x10, 0x20, 0x30, 0x40}))
          << "split=" << split;
      EXPECT_EQ(decoder.BufferedSize(), 0U) << "split=" << split;
      continue;
    }

    EXPECT_EQ(result.status, DecodeStatus::kNeedMoreData) << "split=" << split;

    decoder.Append(std::span<const Byte>(encoded.data() + split, encoded.size() - split));
    result = decoder.Next();

    ASSERT_EQ(result.status, DecodeStatus::kDecoded) << "split=" << split;
    ASSERT_TRUE(result.message.has_value()) << "split=" << split;
    EXPECT_EQ(result.message->request_id, 99U) << "split=" << split;
    EXPECT_EQ(result.message->payload, (std::vector<Byte>{0x10, 0x20, 0x30, 0x40}))
        << "split=" << split;
    EXPECT_EQ(decoder.BufferedSize(), 0U) << "split=" << split;
  }
}

TEST(FrameDecoderTest, HandlesMultipleFramesInOneBuffer) {
  const auto first = EncodedEcho(1, {0x01});
  const auto second = EncodedEcho(2, {0x02, 0x03});

  std::vector<Byte> bytes;
  bytes.insert(bytes.end(), first.begin(), first.end());
  bytes.insert(bytes.end(), second.begin(), second.end());

  FrameDecoder decoder;
  decoder.Append(bytes);

  auto result = decoder.Next();
  ASSERT_EQ(result.status, DecodeStatus::kDecoded);
  ASSERT_TRUE(result.message.has_value());
  EXPECT_EQ(result.message->request_id, 1U);
  EXPECT_EQ(result.message->payload, (std::vector<Byte>{0x01}));

  result = decoder.Next();
  ASSERT_EQ(result.status, DecodeStatus::kDecoded);
  ASSERT_TRUE(result.message.has_value());
  EXPECT_EQ(result.message->request_id, 2U);
  EXPECT_EQ(result.message->payload, (std::vector<Byte>{0x02, 0x03}));
  EXPECT_EQ(decoder.BufferedSize(), 0U);

  result = decoder.Next();
  EXPECT_EQ(result.status, DecodeStatus::kNeedMoreData);
}

}  // namespace pulsecore::protocol
