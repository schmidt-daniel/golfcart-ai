#include <gtest/gtest.h>

#include "golfcart_control/differential_drive.hpp"

// Verify the differential-drive math used by the Motion Controller.
// (Safety state machine logic is exercised via integration tests.)
using golfcart::DifferentialDrive;

TEST(DifferentialDrive, WheelSeparationScalesTurn)
{
  DifferentialDrive d;
  d.wheel_separation_m = 1.0;
  double l = 0.0, r = 0.0;
  d.to_wheel_velocities(0.0, 2.0, l, r);
  EXPECT_NEAR(l, -1.0, 1e-6);
  EXPECT_NEAR(r, 1.0, 1e-6);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
