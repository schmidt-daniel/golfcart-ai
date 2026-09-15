#include <gtest/gtest.h>

#include <cmath>

// Test the slope-state logic used by the hill/rollback node.
// The node computes slope state from pitch with hysteresis.

namespace
{

enum class SlopeState { LEVEL, UPHILL, DOWNHILL };

SlopeState compute_slope(double pitch, double threshold, double hysteresis)
{
  if (pitch > threshold + hysteresis) {
    return SlopeState::UPHILL;
  }
  if (pitch < -threshold - hysteresis) {
    return SlopeState::DOWNHILL;
  }
  return SlopeState::LEVEL;
}

}  // namespace

TEST(HillRollback, LevelGround)
{
  EXPECT_EQ(compute_slope(0.0, 0.1, 0.03), SlopeState::LEVEL);
  EXPECT_EQ(compute_slope(0.05, 0.1, 0.03), SlopeState::LEVEL);
  EXPECT_EQ(compute_slope(-0.05, 0.1, 0.03), SlopeState::LEVEL);
}

TEST(HillRollback, Uphill)
{
  EXPECT_EQ(compute_slope(0.2, 0.1, 0.03), SlopeState::UPHILL);
}

TEST(HillRollback, Downhill)
{
  EXPECT_EQ(compute_slope(-0.2, 0.1, 0.03), SlopeState::DOWNHILL);
}

TEST(HillRollback, Hysteresis)
{
  // Just above threshold -> UPHILL.
  EXPECT_EQ(compute_slope(0.14, 0.1, 0.03), SlopeState::UPHILL);
  // Just below threshold + hysteresis -> LEVEL.
  EXPECT_EQ(compute_slope(0.12, 0.1, 0.03), SlopeState::LEVEL);
}

// --- Brake-hold logic ---
// The brake-hold applies when the cart is stopped (|wheel| <= threshold) on a
// slope. It releases when the operator commands forward (|wheel| > threshold).

namespace
{

// Returns true if the brake-hold should engage.
bool brake_hold(bool enabled, SlopeState slope, double wheel_velocity,
                double hold_threshold)
{
  return enabled && slope != SlopeState::LEVEL &&
         std::abs(wheel_velocity) <= hold_threshold;
}

}  // namespace

TEST(HillRollback, BrakeHoldEngagesWhenStoppedOnSlope)
{
  // Stopped on an uphill -> hold.
  EXPECT_TRUE(brake_hold(true, SlopeState::UPHILL, 0.0, 0.02));
  // Stopped on a downhill -> hold.
  EXPECT_TRUE(brake_hold(true, SlopeState::DOWNHILL, 0.0, 0.02));
}

TEST(HillRollback, BrakeHoldDoesNotEngageOnLevel)
{
  // Level ground -> no hold.
  EXPECT_FALSE(brake_hold(true, SlopeState::LEVEL, 0.0, 0.02));
}

TEST(HillRollback, BrakeHoldReleasesWhenMoving)
{
  // Operator commands forward (wheel above threshold) -> release.
  EXPECT_FALSE(brake_hold(true, SlopeState::UPHILL, 0.1, 0.02));
  // Moving backward -> rollback protection handles it, not brake-hold.
  EXPECT_FALSE(brake_hold(true, SlopeState::UPHILL, -0.1, 0.02));
}

TEST(HillRollback, BrakeHoldCanBeDisabled)
{
  EXPECT_FALSE(brake_hold(false, SlopeState::UPHILL, 0.0, 0.02));
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}