#include <gtest/gtest.h>

#include "golfcart_control/differential_drive.hpp"

using golfcart::DifferentialDrive;

TEST(DifferentialDrive, StraightLine)
{
  DifferentialDrive d;
  d.wheel_separation_m = 0.5;
  double l = 0.0, r = 0.0;
  d.to_wheel_velocities(1.0, 0.0, l, r);
  EXPECT_NEAR(l, 1.0, 1e-6);
  EXPECT_NEAR(r, 1.0, 1e-6);
}

TEST(DifferentialDrive, TurnInPlace)
{
  DifferentialDrive d;
  d.wheel_separation_m = 0.5;
  double l = 0.0, r = 0.0;
  d.to_wheel_velocities(0.0, 1.0, l, r);
  EXPECT_NEAR(l, -0.25, 1e-6);
  EXPECT_NEAR(r, 0.25, 1e-6);
}

TEST(DifferentialDrive, Combined)
{
  DifferentialDrive d;
  d.wheel_separation_m = 0.5;
  double l = 0.0, r = 0.0;
  d.to_wheel_velocities(1.0, 1.0, l, r);
  EXPECT_NEAR(l, 0.75, 1e-6);
  EXPECT_NEAR(r, 1.25, 1e-6);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
