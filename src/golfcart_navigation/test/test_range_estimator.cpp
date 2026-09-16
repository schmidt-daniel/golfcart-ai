#include <cmath>
#include <string>

#include "range_estimator_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

// Test the range-estimator energy model, learning, and warning states.

TEST(RangeEstimator, SlopeBucketing)
{
  EXPECT_EQ(slope_to_bucket(-5.0), BUCKET_DOWNHILL);
  EXPECT_EQ(slope_to_bucket(0.0), BUCKET_FLAT);
  EXPECT_EQ(slope_to_bucket(5.0), BUCKET_UPHILL);
  EXPECT_EQ(slope_to_bucket(-1.0), BUCKET_FLAT);
  EXPECT_EQ(slope_to_bucket(1.0), BUCKET_FLAT);
}

TEST(RangeEstimator, DefaultModel)
{
  EnergyModel m(0.02, 0.3);
  EXPECT_NEAR(m.flat_wh_per_m(), 0.02, 1e-9);
  EXPECT_NEAR(m.wh_per_m(0.0), 0.02, 1e-9);
  EXPECT_NEAR(m.wh_per_m(5.0), 0.02, 1e-9);  // all buckets start at default
}

TEST(RangeEstimator, LearnUpdatesBucket)
{
  EnergyModel m(0.02, 0.3);
  // Measure 0.1 Wh over 10 m on flat ground -> 0.01 Wh/m.
  m.learn(0.0, 0.1, 10.0);
  // EMA: 0.7*0.02 + 0.3*0.01 = 0.014 + 0.003 = 0.017
  EXPECT_NEAR(m.flat_wh_per_m(), 0.017, 1e-9);
  // Uphill bucket unchanged.
  EXPECT_NEAR(m.wh_per_m(5.0), 0.02, 1e-9);
}

TEST(RangeEstimator, LearnIsBucketed)
{
  EnergyModel m(0.02, 0.5);
  // Uphill measurement only affects the uphill bucket.
  m.learn(5.0, 0.3, 10.0);  // 0.03 Wh/m uphill
  EXPECT_NEAR(m.wh_per_m(5.0), 0.025, 1e-9);  // 0.5*0.02 + 0.5*0.03
  EXPECT_NEAR(m.flat_wh_per_m(), 0.02, 1e-9);  // flat unchanged
}

TEST(RangeEstimator, LearnIgnoresZeroDistance)
{
  EnergyModel m(0.02, 0.5);
  m.learn(0.0, 0.1, 0.0);
  EXPECT_NEAR(m.flat_wh_per_m(), 0.02, 1e-9);
}

TEST(RangeEstimator, LearnClampsBadMeasurement)
{
  EnergyModel m(0.02, 1.0);  // alpha=1 -> fully adopts measurement
  // Absurdly high measurement (10 Wh/m) is clamped to 0.5.
  m.learn(0.0, 100.0, 10.0);
  EXPECT_NEAR(m.flat_wh_per_m(), 0.5, 1e-9);
}

TEST(RangeEstimator, EstimateRange)
{
  EnergyModel m(0.02, 0.3);
  // 100 Wh usable / 0.02 Wh/m = 5000 m.
  EXPECT_NEAR(estimate_range_m(m, 100.0, 0.0), 5000.0, 1e-9);
}

TEST(RangeEstimator, WarningStates)
{
  // OK: range covers remaining + return + margin.
  EXPECT_EQ(range_state(200.0, 50.0, 50.0, 50.0), "OK");
  // CAUTION: covers remaining + return but not the margin.
  EXPECT_EQ(range_state(110.0, 50.0, 50.0, 50.0), "CAUTION");
  // CRITICAL: does not cover remaining + return.
  EXPECT_EQ(range_state(90.0, 50.0, 50.0, 50.0), "CRITICAL");
  // Boundary: exactly remaining + return -> CAUTION (no margin).
  EXPECT_EQ(range_state(100.0, 50.0, 50.0, 50.0), "CAUTION");
}

TEST(RangeEstimator, SerializeRoundTrip)
{
  EnergyModel m(0.02, 0.3);
  m.learn(0.0, 0.1, 10.0);  // flat -> 0.017
  const std::string s = m.serialize();
  // 3 comma-separated values.
  EXPECT_EQ(std::count(s.begin(), s.end(), ','), 2);

  EnergyModel m2(0.02, 0.3);
  EXPECT_TRUE(m2.deserialize(s));
  EXPECT_NEAR(m2.flat_wh_per_m(), m.flat_wh_per_m(), 1e-9);
  EXPECT_NEAR(m2.wh_per_m(5.0), m.wh_per_m(5.0), 1e-9);
  EXPECT_NEAR(m2.wh_per_m(-5.0), m.wh_per_m(-5.0), 1e-9);
}

TEST(RangeEstimator, DeserializeRejectsBadInput)
{
  EnergyModel m(0.02, 0.3);
  EXPECT_FALSE(m.deserialize("not-a-number"));
  EXPECT_FALSE(m.deserialize("0.02,0.02"));  // only 2 values
  EXPECT_FALSE(m.deserialize(""));
  // Model unchanged after a failed deserialize.
  EXPECT_NEAR(m.flat_wh_per_m(), 0.02, 1e-9);
}

TEST(RangeEstimator, DeserializeClamps)
{
  EnergyModel m(0.02, 0.3);
  // Absurdly high value is clamped to 0.5.
  EXPECT_TRUE(m.deserialize("0.02,99.0,0.02"));
  EXPECT_NEAR(m.flat_wh_per_m(), 0.5, 1e-9);
}

TEST(RangeEstimator, HolesRemainingUnknown)
{
  // Range available but no hole data -> unknown (-1).
  EXPECT_EQ(estimate_holes_remaining(100.0, 0.0, {}), -1);
}

TEST(RangeEstimator, HolesRemainingZeroRange)
{
  // Zero range -> 0 holes.
  EXPECT_EQ(estimate_holes_remaining(0.0, 100.0, {200.0}), 0);
}

TEST(RangeEstimator, HolesRemainingCurrentOnly)
{
  // Range covers the current hole but not the next.
  EXPECT_EQ(estimate_holes_remaining(150.0, 100.0, {200.0}), 1);
}

TEST(RangeEstimator, HolesRemainingMultiple)
{
  // Range covers current (100) + 2 more holes (200 each).
  EXPECT_EQ(estimate_holes_remaining(600.0, 100.0, {200.0, 200.0, 200.0}), 3);
}

TEST(RangeEstimator, HolesRemainingStopsWhenShort)
{
  // Range covers current (100) + 1 hole (200), not the 3rd (200).
  EXPECT_EQ(estimate_holes_remaining(350.0, 100.0, {200.0, 200.0}), 2);
}

}  // namespace golfcart