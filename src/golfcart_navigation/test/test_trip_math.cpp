#include "trip_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

TEST(TripMath, InactiveByDefault)
{
  TripTracker t;
  EXPECT_FALSE(t.active());
  EXPECT_EQ(t.distance_m(), 0.0);
  EXPECT_EQ(t.energy_wh(), 0.0);
}

TEST(TripMath, StartActivates)
{
  TripTracker t;
  t.start(0.0);
  EXPECT_TRUE(t.active());
}

TEST(TripMath, AccumulateDistanceAndEnergy)
{
  TripTracker t;
  t.start(0.0);
  t.accumulate(10.0, 0.2);
  t.accumulate(5.0, 0.1);
  EXPECT_NEAR(t.distance_m(), 15.0, 1e-9);
  EXPECT_NEAR(t.energy_wh(), 0.3, 1e-9);
}

TEST(TripMath, AccumulateIgnoresWhenInactive)
{
  TripTracker t;
  t.accumulate(10.0, 0.2);
  EXPECT_EQ(t.distance_m(), 0.0);
  EXPECT_EQ(t.energy_wh(), 0.0);
}

TEST(TripMath, AccumulateIgnoresNegative)
{
  TripTracker t;
  t.start(0.0);
  t.accumulate(-5.0, -0.1);
  EXPECT_EQ(t.distance_m(), 0.0);
  EXPECT_EQ(t.energy_wh(), 0.0);
}

TEST(TripMath, EndReturnsDuration)
{
  TripTracker t;
  t.start(10.0);
  t.accumulate(20.0, 0.4);
  const double dur = t.end(40.0);
  EXPECT_NEAR(dur, 30.0, 1e-9);
  EXPECT_FALSE(t.active());
}

TEST(TripMath, AvgSpeed)
{
  TripTracker t;
  t.start(0.0);
  t.accumulate(100.0, 1.0);
  const double dur = t.end(50.0);
  EXPECT_NEAR(t.avg_speed_mps(dur), 2.0, 1e-9);
}

TEST(TripMath, AvgSpeedZeroDuration)
{
  TripTracker t;
  EXPECT_EQ(t.avg_speed_mps(0.0), 0.0);
}

}  // namespace golfcart