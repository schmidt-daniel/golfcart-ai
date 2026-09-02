#pragma once

#include "golfcart_gps/gps_sensor.hpp"

#include <mutex>

namespace golfcart
{

// Mock GPS for hardware-free testing.
// Simulates a fixed position, optionally with a configurable offset.
class MockGpsSensor : public GpsSensor
{
public:
  explicit MockGpsSensor(
    double latitude_deg = 51.5, double longitude_deg = -0.12, double altitude_m = 0.0);

  GpsSample read() override;

  // Test hooks.
  void set_position(double latitude_deg, double longitude_deg);

private:
  mutable std::mutex mutex_;
  double latitude_deg_;
  double longitude_deg_;
  double altitude_m_;
};

}  // namespace golfcart