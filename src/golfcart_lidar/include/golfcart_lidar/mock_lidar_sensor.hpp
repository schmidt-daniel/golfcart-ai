#pragma once

#include "golfcart_lidar/lidar_sensor.hpp"

#include <mutex>

namespace golfcart
{

// Mock LiDAR for hardware-free testing.
// Simulates a scan with a configurable obstacle at a given angle/distance.
class MockLidarSensor : public LidarSensor
{
public:
  explicit MockLidarSensor(int num_points = 360);

  LidarScan read() override;

  // Test hooks: place an obstacle at angle_rad / distance_m.
  void set_obstacle(double angle_rad, double distance_m);
  void clear_obstacle();

private:
  mutable std::mutex mutex_;
  int num_points_;
  bool has_obstacle_ = false;
  double obstacle_angle_ = 0.0;
  double obstacle_distance_ = 0.0;
};

}  // namespace golfcart