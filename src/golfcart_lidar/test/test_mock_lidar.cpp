#include <gtest/gtest.h>

#include <cmath>

#include "golfcart_lidar/mock_lidar_sensor.hpp"

using golfcart::MockLidarSensor;

TEST(MockLidar, FullScanValid)
{
  MockLidarSensor lidar(360);
  auto scan = lidar.read();
  EXPECT_TRUE(scan.valid);
  EXPECT_EQ(scan.points.size(), 360u);
  for (const auto & p : scan.points) {
    EXPECT_TRUE(p.valid);
    EXPECT_NEAR(p.range_m, 12.0, 1e-6);  // max range, no obstacle
  }
}

TEST(MockLidar, ObstacleAtAngle)
{
  MockLidarSensor lidar(360);
  lidar.set_obstacle(0.0, 1.0);  // obstacle straight ahead at 1 m
  auto scan = lidar.read();
  // Find the point closest to angle 0.
  double min_d = 1e9;
  double range_at_zero = 0.0;
  for (const auto & p : scan.points) {
    double d = std::abs(p.angle_rad);
    d = std::min(d, 2.0 * M_PI - d);
    if (d < min_d) {
      min_d = d;
      range_at_zero = p.range_m;
    }
  }
  EXPECT_NEAR(range_at_zero, 1.0, 1e-6);
}

TEST(MockLidar, ClearObstacle)
{
  MockLidarSensor lidar(360);
  lidar.set_obstacle(0.0, 1.0);
  lidar.clear_obstacle();
  auto scan = lidar.read();
  for (const auto & p : scan.points) {
    EXPECT_NEAR(p.range_m, 12.0, 1e-6);
  }
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}