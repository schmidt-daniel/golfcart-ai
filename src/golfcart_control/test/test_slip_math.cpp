#include <cmath>

#include "golfcart_control/slip_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

// Test the wheel-slip math used by wheel_slip_node.

TEST(SlipMath, NoSlip)
{
  // Wheels match actual motion -> ratio 0.
  EXPECT_NEAR(slip_ratio(1.0, 1.0), 0.0, 1e-9);
  EXPECT_NEAR(slip_ratio(0.5, 0.5), 0.0, 1e-9);
}

TEST(SlipMath, PartialSlip)
{
  // Wheels at 2 m/s, actual 1 m/s -> ratio 0.5.
  EXPECT_NEAR(slip_ratio(2.0, 1.0), 0.5, 1e-9);
}

TEST(SlipMath, FullSpinInPlace)
{
  // Wheels spinning, trolley stationary -> ratio 1.0.
  EXPECT_NEAR(slip_ratio(1.0, 0.0), 1.0, 1e-9);
}

TEST(SlipMath, DivideByZeroGuard)
{
  // Wheels not moving -> no slip (avoid divide-by-zero).
  EXPECT_NEAR(slip_ratio(0.0, 0.0), 0.0, 1e-9);
  EXPECT_NEAR(slip_ratio(0.0, 1.0), 0.0, 1e-9);
}

TEST(SlipMath, ClampedToUnit)
{
  // Actual faster than wheels (e.g. braking) -> clamp to 0, not negative.
  EXPECT_NEAR(slip_ratio(1.0, 2.0), 0.0, 1e-9);
}

TEST(SlipDetector, DebounceRequiresSustainedSlip)
{
  SlipDetector d(0.3, 0.5);
  // Brief slip (0.2 s) below debounce -> not slipping.
  EXPECT_FALSE(d.update(0.8, 0.2));
  // Reset on no slip.
  EXPECT_FALSE(d.update(0.0, 0.1));
  // Sustained slip (0.3 + 0.3 = 0.6 s) >= 0.5 -> slipping.
  EXPECT_FALSE(d.update(0.8, 0.3));
  EXPECT_TRUE(d.update(0.8, 0.3));
}

TEST(SlipDetector, NoSlipNeverTriggers)
{
  SlipDetector d(0.3, 0.5);
  for (int i = 0; i < 100; ++i) {
    EXPECT_FALSE(d.update(0.1, 0.1));
  }
}

TEST(SlipDetector, ResetClears)
{
  SlipDetector d(0.3, 0.5);
  d.update(0.8, 0.6);
  EXPECT_TRUE(d.slipping());
  d.reset();
  EXPECT_FALSE(d.slipping());
}

}  // namespace golfcart