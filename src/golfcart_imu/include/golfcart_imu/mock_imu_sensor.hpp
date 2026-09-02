#pragma once

#include "golfcart_imu/imu_sensor.hpp"

#include <mutex>

namespace golfcart
{

// Mock IMU for hardware-free testing.
// Simulates a stationary IMU with gravity along -Z, optionally with a
// configurable roll/pitch tilt.
class MockImuSensor : public ImuSensor
{
public:
  explicit MockImuSensor(double roll_rad = 0.0, double pitch_rad = 0.0);

  ImuSample read() override;

  // Test hooks.
  void set_tilt(double roll_rad, double pitch_rad);

private:
  mutable std::mutex mutex_;
  double roll_rad_;
  double pitch_rad_;
};

}  // namespace golfcart