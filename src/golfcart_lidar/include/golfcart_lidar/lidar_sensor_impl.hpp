#pragma once

#include "golfcart_lidar/lidar_sensor.hpp"

#include <string>

namespace golfcart
{

// LiDAR-backed LidarSensor.
//
// NOTE: This is a scaffold. The actual FHL-LD19P driver (serial/UART) must be
// implemented before use on hardware.
class LidarSensorImpl : public LidarSensor
{
public:
  explicit LidarSensorImpl(const std::string & device = "/dev/ttyUSB0");

  LidarScan read() override;

private:
  std::string device_;
};

}  // namespace golfcart