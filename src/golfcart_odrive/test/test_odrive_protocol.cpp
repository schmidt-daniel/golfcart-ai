#include <gtest/gtest.h>

#include "golfcart_odrive/odrive_protocol.hpp"

using golfcart::build_json_command;
using golfcart::odrive_crc32;
using golfcart::odrive_endpoint_hash;

TEST(OdriveProtocol, Crc32KnownValue)
{
  // CRC-32 of "123456789" is 0xCBF43926 (standard IEEE check value).
  std::vector<uint8_t> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(odrive_crc32(data), 0xCBF43926u);
}

TEST(OdriveProtocol, Crc32Empty)
{
  std::vector<uint8_t> data;
  EXPECT_EQ(odrive_crc32(data), 0x00000000u);
}

TEST(OdriveProtocol, EndpointHashDeterministic)
{
  const uint32_t h1 = odrive_endpoint_hash("axis0.encoder.pos_estimate");
  const uint32_t h2 = odrive_endpoint_hash("axis0.encoder.pos_estimate");
  EXPECT_EQ(h1, h2);
  EXPECT_NE(h1, odrive_endpoint_hash("axis1.encoder.pos_estimate"));
}

TEST(OdriveProtocol, JsonCommandFrame)
{
  const std::string json = "{\"axis0.requested_state\":8}";
  const auto frame = build_json_command(json);

  // Header: 0xAA 0xAA len_lo len_hi crc_lo crc_hi
  ASSERT_GE(frame.size(), 6u);
  EXPECT_EQ(frame[0], 0xAA);
  EXPECT_EQ(frame[1], 0xAA);
  const uint16_t len = static_cast<uint16_t>(frame[2]) | (static_cast<uint16_t>(frame[3]) << 8);
  EXPECT_EQ(len, json.size());

  // Payload matches the JSON.
  std::vector<uint8_t> payload(frame.begin() + 6, frame.end());
  EXPECT_EQ(payload.size(), json.size());
  EXPECT_EQ(std::string(payload.begin(), payload.end()), json);

  // CRC covers the payload.
  const uint32_t crc = static_cast<uint32_t>(frame[4]) | (static_cast<uint32_t>(frame[5]) << 8);
  EXPECT_EQ(crc, odrive_crc32(payload) & 0xFFFFu);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}