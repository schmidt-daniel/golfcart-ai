#pragma once

#include <cstdint>

namespace golfcart
{

// Generic GPS abstraction.
// High-level code must not depend on a specific GPS module.
struct GpsSample
{
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double altitude_m = 0.0;
  double speed_mps = 0.0;
  double heading_rad = 0.0;
  bool valid = false;
  uint8_t fix_type = 0;
  uint8_t satellites = 0;
  double hdop = 0.0;
  double vdop = 0.0;
  double pdop = 0.0;
};

class GpsSensor
{
public:
  virtual ~GpsSensor() = default;

  // Read the latest GPS fix.
  virtual GpsSample read() = 0;
};

}  // namespace golfcart