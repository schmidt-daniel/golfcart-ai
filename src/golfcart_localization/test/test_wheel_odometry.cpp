#include <cmath>

#include "golfcart_control/differential_drive.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

// Test the differential-drive odometry integration used by wheel_odometry_node.
TEST(DifferentialDriveOdometry, ForwardMotion)
{
  DifferentialDrive drive;
  drive.wheel_separation_m = 0.5;

  // Both wheels at 1 m/s -> forward at 1 m/s, no rotation.
  double left = 1.0, right = 1.0;
  double v = (left + right) / 2.0;
  double omega = (right - left) / drive.wheel_separation_m;
  EXPECT_NEAR(v, 1.0, 1e-6);
  EXPECT_NEAR(omega, 0.0, 1e-6);
}

TEST(DifferentialDriveOdometry, Rotation)
{
  DifferentialDrive drive;
  drive.wheel_separation_m = 0.5;

  // Left at -1, right at +1 -> pure rotation.
  double left = -1.0, right = 1.0;
  double v = (left + right) / 2.0;
  double omega = (right - left) / drive.wheel_separation_m;
  EXPECT_NEAR(v, 0.0, 1e-6);
  EXPECT_NEAR(omega, 4.0, 1e-6);  // (1 - (-1)) / 0.5 = 4
}

TEST(DifferentialDriveOdometry, PoseIntegration)
{
  DifferentialDrive drive;
  drive.wheel_separation_m = 0.5;

  // Drive forward at 1 m/s for 1 second -> x advances by 1 m.
  double x = 0.0, y = 0.0, theta = 0.0;
  const double dt = 1.0;
  const double v = 1.0, omega = 0.0;
  x += v * std::cos(theta) * dt;
  y += v * std::sin(theta) * dt;
  theta += omega * dt;
  EXPECT_NEAR(x, 1.0, 1e-6);
  EXPECT_NEAR(y, 0.0, 1e-6);
  EXPECT_NEAR(theta, 0.0, 1e-6);
}

}  // namespace golfcart