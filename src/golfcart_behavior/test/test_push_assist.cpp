#include <gtest/gtest.h>

#include <cmath>

// Test the push-assist logic used by the push_assist_node.
// The node computes:
//   - effective force = force - slope_comp_gain * sin(pitch)
//   - dead zone + hysteresis around the threshold
//   - proportional assist clamped to max_assist
//   - brake on negative force

namespace
{

// Effective user force after slope compensation.
double effective_force(double force_n, double pitch_rad, double slope_comp_gain)
{
  return force_n - slope_comp_gain * std::sin(pitch_rad);
}

// Dead zone + hysteresis state machine. Returns the new active state.
bool update_active(bool active, double f, double deadzone, double hysteresis)
{
  if (!active && f > deadzone + hysteresis) {
    return true;
  }
  if (active && f < deadzone - hysteresis) {
    return false;
  }
  return active;
}

// Proportional assist, clamped.
double compute_assist(double f, double gain, double max_assist)
{
  double assist = f * gain;
  if (assist > max_assist) {
    assist = max_assist;
  }
  if (assist < 0.0) {
    assist = 0.0;
  }
  return assist;
}

}  // namespace

TEST(PushAssist, EffectiveForceLevelGround)
{
  // No pitch -> effective force = raw force.
  EXPECT_NEAR(effective_force(10.0, 0.0, 0.5), 10.0, 1e-6);
}

TEST(PushAssist, EffectiveForceUphill)
{
  // Uphill (positive pitch): gravity pulls back, so effective force is lower.
  double f = effective_force(10.0, 0.3, 0.5);
  EXPECT_NEAR(f, 10.0 - 0.5 * std::sin(0.3), 1e-6);
  EXPECT_LT(f, 10.0);
}

TEST(PushAssist, EffectiveForceDownhill)
{
  // Downhill (negative pitch): gravity helps, so effective force is higher.
  double f = effective_force(10.0, -0.3, 0.5);
  EXPECT_NEAR(f, 10.0 - 0.5 * std::sin(-0.3), 1e-6);
  EXPECT_GT(f, 10.0);
}

TEST(PushAssist, DeadZoneNoAssist)
{
  // Below the dead zone, assist stays off.
  bool active = false;
  active = update_active(active, 1.0, 2.0, 0.5);
  EXPECT_FALSE(active);
}

TEST(PushAssist, DeadZoneActivates)
{
  // Above dead zone + hysteresis, assist turns on.
  bool active = false;
  active = update_active(active, 3.0, 2.0, 0.5);
  EXPECT_TRUE(active);
}

TEST(PushAssist, HysteresisPreventsFlapping)
{
  // Once active, it stays active until well below the dead zone.
  bool active = true;
  active = update_active(active, 1.8, 2.0, 0.5);  // above deadzone - hysteresis
  EXPECT_TRUE(active);
  active = update_active(active, 1.2, 2.0, 0.5);  // below deadzone - hysteresis
  EXPECT_FALSE(active);
}

TEST(PushAssist, AssistProportionalAndClamped)
{
  // Proportional to force (below the clamp).
  EXPECT_NEAR(compute_assist(2.0, 0.5, 1.0), 1.0, 1e-6);
  // Clamped to max.
  EXPECT_NEAR(compute_assist(10.0, 0.5, 1.0), 1.0, 1e-6);
  // Negative force -> no forward assist.
  EXPECT_NEAR(compute_assist(-5.0, 0.5, 1.0), 0.0, 1e-6);
}