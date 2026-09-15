#include <cmath>

#include "drive_distance_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

TEST(DriveDistanceMath, GoalAheadEast)
{
  // Facing east (yaw = 0), drive 10 m.
  Pose2D p{0.0, 0.0, 0.0};
  double gx, gy, gt;
  goal_ahead(p, 10.0, gx, gy, gt);
  EXPECT_NEAR(gx, 10.0, 1e-9);
  EXPECT_NEAR(gy, 0.0, 1e-9);
  EXPECT_NEAR(gt, 0.0, 1e-9);
}

TEST(DriveDistanceMath, GoalAheadNorth)
{
  // Facing north (yaw = pi/2), drive 20 m.
  Pose2D p{0.0, 0.0, M_PI / 2.0};
  double gx, gy, gt;
  goal_ahead(p, 20.0, gx, gy, gt);
  EXPECT_NEAR(gx, 0.0, 1e-9);
  EXPECT_NEAR(gy, 20.0, 1e-9);
  EXPECT_NEAR(gt, M_PI / 2.0, 1e-9);
}

TEST(DriveDistanceMath, GoalAheadOffsetPose)
{
  // Pose at (5, 5) facing 45 deg, drive 10 m.
  Pose2D p{5.0, 5.0, M_PI / 4.0};
  double gx, gy, gt;
  goal_ahead(p, 10.0, gx, gy, gt);
  const double s = 10.0 * std::cos(M_PI / 4.0);
  EXPECT_NEAR(gx, 5.0 + s, 1e-9);
  EXPECT_NEAR(gy, 5.0 + s, 1e-9);
  EXPECT_NEAR(gt, M_PI / 4.0, 1e-9);
}

TEST(DriveDistanceMath, GoalAheadZeroDistance)
{
  // Zero distance -> goal at current pose.
  Pose2D p{3.0, 4.0, 1.0};
  double gx, gy, gt;
  goal_ahead(p, 0.0, gx, gy, gt);
  EXPECT_NEAR(gx, 3.0, 1e-9);
  EXPECT_NEAR(gy, 4.0, 1e-9);
  EXPECT_NEAR(gt, 1.0, 1e-9);
}

}  // namespace golfcart