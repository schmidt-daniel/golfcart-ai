#include <gtest/gtest.h>

#include <cmath>

// Test the steering-assist logic used by steering_assist_node.
// The node computes a steering nudge away from a nearby obstacle:
//   - only active in MANUAL mode
//   - only when the obstacle is within [min_distance, max_distance]
//   - nudge = -sign(angle) * gain * (1/distance), clamped to max_nudge

namespace
{

// Compute the steering nudge. Returns 0 if not active.
double compute_nudge(bool enabled, bool manual, bool valid, double distance, double angle,
                     double gain, double min_d, double max_d, double max_nudge)
{
  if (!enabled || !manual || !valid) {
    return 0.0;
  }
  if (distance > max_d || distance < min_d) {
    return 0.0;
  }
  double strength = gain * (1.0 / distance);
  double nudge = -std::copysign(strength, angle);
  if (nudge > max_nudge) {
    nudge = max_nudge;
  } else if (nudge < -max_nudge) {
    nudge = -max_nudge;
  }
  return nudge;
}

}  // namespace

TEST(SteeringAssist, OnlyInManualMode)
{
  // Not manual -> no nudge.
  EXPECT_NEAR(compute_nudge(true, false, true, 1.0, 0.5, 0.5, 0.5, 2.0, 0.4), 0.0, 1e-6);
  // Manual + valid -> nudge. strength = 0.5*(1/1.0) = 0.5, nudge = -0.5,
  // clamped to max_nudge = 0.4 -> -0.4.
  EXPECT_NEAR(compute_nudge(true, true, true, 1.0, 0.5, 0.5, 0.5, 2.0, 0.4), -0.4, 1e-6);
}

TEST(SteeringAssist, Disabled)
{
  // Steering assist disabled -> no nudge even in manual mode with an obstacle.
  EXPECT_NEAR(compute_nudge(false, true, true, 1.0, 0.5, 0.5, 0.5, 2.0, 0.4), 0.0, 1e-6);
}

TEST(SteeringAssist, NoObstacle)
{
  // Invalid obstacle -> no nudge.
  EXPECT_NEAR(compute_nudge(true, true, false, 1.0, 0.5, 0.5, 0.5, 2.0, 0.4), 0.0, 1e-6);
}

TEST(SteeringAssist, RangeLimit)
{
  // Too far -> no nudge.
  EXPECT_NEAR(compute_nudge(true, true, true, 3.0, 0.5, 0.5, 0.5, 2.0, 0.4), 0.0, 1e-6);
  // Too close -> no nudge.
  EXPECT_NEAR(compute_nudge(true, true, true, 0.2, 0.5, 0.5, 0.5, 2.0, 0.4), 0.0, 1e-6);
}

TEST(SteeringAssist, SteersAway)
{
  // Obstacle on the left (positive angle) -> steer right (negative nudge).
  EXPECT_LT(compute_nudge(true, true, true, 1.0, 0.5, 0.5, 0.5, 2.0, 0.4), 0.0);
  // Obstacle on the right (negative angle) -> steer left (positive nudge).
  EXPECT_GT(compute_nudge(true, true, true, 1.0, -0.5, 0.5, 0.5, 2.0, 0.4), 0.0);
}

TEST(SteeringAssist, Clamped)
{
  // Very close obstacle -> nudge clamped to max.
  EXPECT_NEAR(compute_nudge(true, true, true, 0.6, 0.5, 0.5, 0.5, 2.0, 0.4), -0.4, 1e-6);
}