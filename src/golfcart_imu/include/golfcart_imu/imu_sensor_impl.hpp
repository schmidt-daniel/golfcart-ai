#pragma once

#include "golfcart_imu/imu_sensor.hpp"

#include <string>

namespace golfcart
{

// IMU-backed ImuSensor.
//
// NOTE: This is a scaffold. The actual IMU (e.g. MPU-6050 / BNO055) I2C
// communication must be implemented before use on hardware.
class ImuSensorImpl : public ImuSensor
{
public:
  explicit ImuSensorImpl(const std::string & device = "/dev/i2c-1");

  ImuSample read() override;

private:
  std::string device_;
};

}  // namespace golfcart