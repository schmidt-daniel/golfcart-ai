#include "golfcart_odrive/odrive_protocol.hpp"

#include <cstring>

namespace golfcart
{

// CRC-32 (IEEE 802.3), reflected, polynomial 0xEDB88320.
// This is the CRC used by the ODrive native protocol framing.
uint32_t odrive_crc32(const std::vector<uint8_t> & data)
{
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
      const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

// ODrive endpoint hash: FNV-1a 32-bit over the path string.
uint32_t odrive_endpoint_hash(const std::string & path)
{
  uint32_t hash = 2166136261u;  // FNV offset basis
  for (char c : path) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;  // FNV prime
  }
  return hash;
}

std::vector<uint8_t> build_json_command(const std::string & json)
{
  std::vector<uint8_t> payload(json.begin(), json.end());
  const uint16_t len = static_cast<uint16_t>(payload.size());
  const uint32_t crc = odrive_crc32(payload);

  std::vector<uint8_t> frame;
  frame.reserve(6 + payload.size());
  frame.push_back(0xAA);
  frame.push_back(0xAA);
  frame.push_back(static_cast<uint8_t>(len & 0xFF));
  frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

}  // namespace golfcart