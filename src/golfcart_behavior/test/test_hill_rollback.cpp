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

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}