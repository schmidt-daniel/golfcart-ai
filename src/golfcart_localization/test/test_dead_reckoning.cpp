#include <cmath>

#include "dead_reckoning_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

// Test the dead-reckoning budget math used by dead_reckoning_node.

TEST(DeadReckoningBudget, FreshBudgetNotExceeded)
{
  DeadReckoningBudget b(30.0, 50.0);
  EXPECT_FALSE(b.exceeded());
  EXPECT_DOUBLE_EQ(b.remaining_time_s(), 30.0);
  EXPECT_DOUBLE_EQ(b.remaining_distance_m(), 50.0);
}

TEST(DeadReckoningBudget, TimeBudgetExceeded)
{
  DeadReckoningBudget b(10.0, 100.0);
  // Move 1 m/s for 11 s -> time budget (10 s) exceeded, distance (10 m) not.
  // The first update() establishes the reference pose (no distance).
  for (int i = 0; i < 11; ++i) {
    b.update(Pose2D{i * 1.0, 0.0, 0.0}, 1.0);
  }
  EXPECT_TRUE(b.exceeded());
  EXPECT_NEAR(b.used_time_s(), 11.0, 1e-9);
  EXPECT_NEAR(b.used_distance_m(), 10.0, 1e-9);
  EXPECT_DOUBLE_EQ(b.remaining_time_s(), 0.0);
}

TEST(DeadReckoningBudget, DistanceBudgetExceeded)
{
  DeadReckoningBudget b(100.0, 10.0);
  // Move 2 m/s for 6 s -> distance (10 m) exceeded, time (6 s) not.
  // The first update() establishes the reference pose (no distance).
  for (int i = 0; i < 6; ++i) {
    b.update(Pose2D{i * 2.0, 0.0, 0.0}, 1.0);
  }
  EXPECT_TRUE(b.exceeded());
  EXPECT_NEAR(b.used_distance_m(), 10.0, 1e-9);
  EXPECT_DOUBLE_EQ(b.remaining_distance_m(), 0.0);
}

TEST(DeadReckoningBudget, ResetClearsCounters)
{
  DeadReckoningBudget b(10.0, 10.0);
  b.update(Pose2D{5.0, 0.0, 0.0}, 5.0);
  EXPECT_TRUE(b.used_time_s() > 0.0);
  b.reset();
  EXPECT_FALSE(b.exceeded());
  EXPECT_DOUBLE_EQ(b.used_time_s(), 0.0);
  EXPECT_DOUBLE_EQ(b.used_distance_m(), 0.0);
}

TEST(DeadReckoningBudget, StationaryDoesNotConsumeDistance)
{
  DeadReckoningBudget b(10.0, 10.0);
  // Same pose repeatedly -> no distance, only time.
  for (int i = 0; i < 5; ++i) {
    b.update(Pose2D{3.0, 4.0, 0.0}, 1.0);
  }
  EXPECT_NEAR(b.used_distance_m(), 0.0, 1e-9);
  EXPECT_NEAR(b.used_time_s(), 5.0, 1e-9);
  EXPECT_FALSE(b.exceeded());
}

TEST(DeadReckoningBudget, DiagonalDistance)
{
  DeadReckoningBudget b(100.0, 100.0);
  // Move (3,4) -> 5 m in one step.
  b.update(Pose2D{0.0, 0.0, 0.0}, 1.0);
  b.update(Pose2D{3.0, 4.0, 0.0}, 1.0);
  EXPECT_NEAR(b.used_distance_m(), 5.0, 1e-9);
}

}  // namespace golfcart