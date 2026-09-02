#pragma once

#include <vector>

namespace golfcart
{

// A single LiDAR range reading.
struct LidarPoint
{
  double angle_rad = 0.0;  // angle in the scan plane
  double range_m = 0.0;    // distance to the reading
  bool valid = true;       // false for invalid/missing readings
};

// A full LiDAR scan.
struct LidarScan
{
  std::vector<LidarPoint> points;
  bool valid = false;
};

// Generic LiDAR abstraction.
// High-level code must not depend on a specific LiDAR model.
class LidarSensor
{
public:
  virtual ~LidarSensor() = default;

  // Read the latest scan.
  virtual LidarScan read() = 0;
};

}  // namespace golfcart