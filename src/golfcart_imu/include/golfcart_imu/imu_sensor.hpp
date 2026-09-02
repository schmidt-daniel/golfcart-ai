#pragma once

namespace golfcart
{

// Generic IMU abstraction.
// High-level code must not depend on a specific IMU model.
struct ImuSample
{
  double accel_x = 0.0;  // m/s^2
  double accel_y = 0.0;
  double accel_z = 0.0;
  double gyro_x = 0.0;   // rad/s
  double gyro_y = 0.0;
  double gyro_z = 0.0;
  bool valid = false;
};

class ImuSensor
{
public:
  virtual ~ImuSensor() = default;

  // Read the latest IMU sample.
  virtual ImuSample read() = 0;
};

}  // namespace golfcart