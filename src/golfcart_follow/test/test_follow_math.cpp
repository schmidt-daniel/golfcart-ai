#include "golfcart_follow/follow_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

// Test the follow-me speed adaptation math.

TEST(FollowSpeed, AtFollowDistanceUsesMin)
{
  // At/below the follow distance, use the minimum speed (don't crowd).
  EXPECT_NEAR(follow_speed(1.5, 1.5, 0.2, 0.8, 3.0), 0.2, 1e-9);
  EXPECT_NEAR(follow_speed(1.0, 1.5, 0.2, 0.8, 3.0), 0.2, 1e-9);
}

TEST(FollowSpeed, AtFullSpeedDistanceUsesMax)
{
  // At/above the full-speed distance, use the max speed.
  EXPECT_NEAR(follow_speed(3.0, 1.5, 0.2, 0.8, 3.0), 0.8, 1e-9);
  EXPECT_NEAR(follow_speed(5.0, 1.5, 0.2, 0.8, 3.0), 0.8, 1e-9);
}

TEST(FollowSpeed, RampsLinearlyBetween)
{
  // Halfway between follow and full-speed distance -> halfway between speeds.
  EXPECT_NEAR(follow_speed(2.25, 1.5, 0.2, 0.8, 3.0), 0.5, 1e-9);
  // Quarter of the way -> quarter of the speed range.
  EXPECT_NEAR(follow_speed(1.875, 1.5, 0.2, 0.8, 3.0), 0.35, 1e-9);
}

TEST(FollowSpeed, DegenerateFullSpeedDistance)
{
  // If full_speed_distance <= follow_distance, always max (for distance >
  // follow_distance). At the follow distance we still don't crowd (min).
  EXPECT_NEAR(follow_speed(2.0, 1.5, 0.2, 0.8, 1.0), 0.8, 1e-9);
  EXPECT_NEAR(follow_speed(2.0, 1.5, 0.2, 0.8, 1.5), 0.8, 1e-9);
  EXPECT_NEAR(follow_speed(1.5, 1.5, 0.2, 0.8, 1.0), 0.2, 1e-9);
}

}  // namespace golfcart