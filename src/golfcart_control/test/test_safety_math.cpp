#include "golfcart_control/safety_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

// Test the obstacle-aware slowdown factor.

TEST(ObstacleSlowdown, NoSlowdownFarAway)
{
  // At/above the start distance, no slowdown.
  EXPECT_NEAR(obstacle_slowdown_factor(2.0, 2.0, 0.3), 1.0, 1e-9);
  EXPECT_NEAR(obstacle_slowdown_factor(5.0, 2.0, 0.3), 1.0, 1e-9);
}

TEST(ObstacleSlowdown, NoSlowdownInvalidDistance)
{
  // Distance <= 0 -> no slowdown.
  EXPECT_NEAR(obstacle_slowdown_factor(0.0, 2.0, 0.3), 1.0, 1e-9);
  EXPECT_NEAR(obstacle_slowdown_factor(-1.0, 2.0, 0.3), 1.0, 1e-9);
}

TEST(ObstacleSlowdown, MinFactorAtZero)
{
  // As distance -> 0, factor -> min_factor.
  EXPECT_NEAR(obstacle_slowdown_factor(0.01, 2.0, 0.3), 0.3035, 1e-6);
}

TEST(ObstacleSlowdown, RampsLinearly)
{
  // Halfway to the start distance -> halfway between min and 1.0.
  EXPECT_NEAR(obstacle_slowdown_factor(1.0, 2.0, 0.3), 0.65, 1e-9);
  // Quarter of the way -> quarter of the range.
  EXPECT_NEAR(obstacle_slowdown_factor(0.5, 2.0, 0.3), 0.475, 1e-9);
}

TEST(ObstacleSlowdown, DegenerateStart)
{
  // start_m <= 0 -> no slowdown.
  EXPECT_NEAR(obstacle_slowdown_factor(1.0, 0.0, 0.3), 1.0, 1e-9);
  EXPECT_NEAR(obstacle_slowdown_factor(1.0, -1.0, 0.3), 1.0, 1e-9);
}

}  // namespace golfcart