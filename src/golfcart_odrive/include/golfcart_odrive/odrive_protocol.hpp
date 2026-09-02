#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace golfcart
{

// ODrive native protocol helpers (ODrive 0.5.6).
// Implements the CRC-32 (IEEE) used by the ODrive framing and the endpoint
// hash used to address properties over the native protocol.

// Compute the ODrive CRC-32 (IEEE 802.3, reflected) over the given bytes.
uint32_t odrive_crc32(const std::vector<uint8_t> & data);

// Compute the ODrive endpoint hash for a property path, e.g. "axis0.encoder.pos_estimate".
uint32_t odrive_endpoint_hash(const std::string & path);

// Build a native-protocol JSON command frame: [0xAA, 0xAA, len_lo, len_hi, crc_lo, crc_hi, payload...]
std::vector<uint8_t> build_json_command(const std::string & json);

}  // namespace golfcart