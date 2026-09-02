#include "golfcart_lidar/mock_lidar_sensor.hpp"

#include <cmath>

namespace golfcart
{

MockLidarSensor::MockLidarSensor(int num_points)
  : num_points_(num_points)
{
}

LidarScan MockLidarSensor::read()
{
  std::lock_guard<std::mutex> lock(mutex_);
  LidarScan scan;
  scan.valid = true;
  scan.points.reserve(num_points_);

  const double max_range = 12.0;  // FHL-LD19P nominal range
  for (int i = 0; i < num_points_; ++i) {
    const double angle = 2.0 * M_PI * i / num_points_;
    LidarPoint p;
    p.angle_rad = angle;
    p.range_m = max_range;
    p.valid = true;

    // If an obstacle is set, place it at the matching angle.
    if (has_obstacle_) {
      double d = std::abs(angle - obstacle_angle_);
      // Wrap-around handling.
      d = std::min(d, 2.0 * M_PI - d);
      if (d < 0.05) {  // ~3 degrees
        p.range_m = obstacle_distance_;
      }
    }
    scan.points.push_back(p);
  }
  return scan;
}

void MockLidarSensor::set_obstacle(double angle_rad, double distance_m)
{
  std::lock_guard<std::mutex> lock(mutex_);
  has_obstacle_ = true;
  obstacle_angle_ = angle_rad;
  obstacle_distance_ = distance_m;
}

void MockLidarSensor::clear_obstacle()
{
  std::lock_guard<std::mutex> lock(mutex_);
  has_obstacle_ = false;
}

}  // namespace golfcart