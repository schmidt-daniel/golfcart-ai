#pragma once

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
};

class GpsSensor
{
public:
  virtual ~GpsSensor() = default;

  // Read the latest GPS fix.
  virtual GpsSample read() = 0;
};

}  // namespace golfcart