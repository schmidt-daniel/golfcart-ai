#include <cmath>
#include <deque>

#include "gtest/gtest.h"

#include "summon_math.hpp"

namespace golfcart
{

// ---- latlon_to_enu ----
TEST(SummonMath, LatLonToEnu)
{
  // At the origin, offset is zero.
  auto p = latlon_to_enu(51.5, -0.12, 51.5, -0.12);
  EXPECT_NEAR(p.x, 0.0, 1e-6);
  EXPECT_NEAR(p.y, 0.0, 1e-6);

  // 1 degree of latitude ~ 111 km north.
  p = latlon_to_enu(52.5, -0.12, 51.5, -0.12);
  EXPECT_NEAR(p.y, 111195.0, 100.0);
  EXPECT_NEAR(p.x, 0.0, 1e-3);
}

// ---- estimate_velocity ----
TEST(SummonMath, EstimateVelocityConstantMotion)
{
  // Operator moving east at ~1 m/s, sampled at 1 Hz for 5 s.
  std::deque<Fix> history;
  const double lat0 = 51.5, lon0 = -0.12;
  for (int i = 0; i < 5; ++i) {
    // 1 m east ~ 1/111195 deg lon at this latitude.
    const double lon = lon0 + i * 1.0 / (111195.0 * std::cos(lat0 * M_PI / 180.0));
    history.push_back({lat0, lon, static_cast<double>(i)});
  }
  auto est = estimate_velocity(history);
  EXPECT_TRUE(est.valid);
  EXPECT_NEAR(est.speed, 1.0, 0.1);
  EXPECT_NEAR(est.v.x, 1.0, 0.1);
  EXPECT_NEAR(est.v.y, 0.0, 0.1);
}

TEST(SummonMath, EstimateVelocityStationary)
{
  std::deque<Fix> history;
  for (int i = 0; i < 5; ++i) {
    history.push_back({51.5, -0.12, static_cast<double>(i)});
  }
  auto est = estimate_velocity(history);
  EXPECT_FALSE(est.valid);  // speed below min_speed_mps
}

TEST(SummonMath, EstimateVelocityTooFewSamples)
{
  std::deque<Fix> history;
  history.push_back({51.5, -0.12, 0.0});
  auto est = estimate_velocity(history);
  EXPECT_FALSE(est.valid);
}

// ---- predictive_target ----
TEST(SummonMath, PredictiveTargetLeads)
{
  // Operator at origin moving east at 1 m/s. Trolley 10 m west.
  const Vec2 op{0.0, 0.0};
  const Vec2 op_vel{1.0, 0.0};
  const Vec2 trolley{-10.0, 0.0};
  const double trolley_speed = 2.0;
  const double max_lead = 10.0;

  auto target = predictive_target(op, op_vel, trolley, trolley_speed, max_lead);
  // ETA ~ 10/2 = 5 s, lead ~ 5 m east. Target should be east of the operator.
  EXPECT_GT(target.x, 0.0);
  EXPECT_NEAR(target.y, 0.0, 1e-6);
  // Lead clamped to max_lead.
  EXPECT_LE(std::hypot(target.x - op.x, target.y - op.y), max_lead + 1e-6);
}

TEST(SummonMath, PredictiveTargetClampsLead)
{
  const Vec2 op{0.0, 0.0};
  const Vec2 op_vel{5.0, 0.0};  // fast operator
  const Vec2 trolley{-100.0, 0.0};
  const double trolley_speed = 1.0;
  const double max_lead = 10.0;

  auto target = predictive_target(op, op_vel, trolley, trolley_speed, max_lead);
  const double lead = std::hypot(target.x - op.x, target.y - op.y);
  EXPECT_LE(lead, max_lead + 1e-6);
}

// ---- is_approaching ----
TEST(SummonMath, IsApproachingDirect)
{
  // trolley_to_op = operator_pos - trolley_pos points from trolley to operator.
  // Operator at origin, trolley east of operator at (10, 0), so trolley_to_op
  // points west (-x). Operator walks east (toward the trolley).
  const Vec2 op_vel{1.0, 0.0};
  const Vec2 trolley_to_op{-10.0, 0.0};
  EXPECT_TRUE(is_approaching(op_vel, trolley_to_op, 25.0, 2.0));
}

TEST(SummonMath, IsApproachingMovingAway)
{
  // Operator at origin, trolley west at (-10, 0), so trolley_to_op points east.
  // Operator walks east (away from the trolley).
  const Vec2 op_vel{1.0, 0.0};
  const Vec2 trolley_to_op{10.0, 0.0};
  EXPECT_FALSE(is_approaching(op_vel, trolley_to_op, 25.0, 2.0));
}

TEST(SummonMath, IsApproachingStationary)
{
  const Vec2 op_vel{0.0, 0.0};
  const Vec2 trolley_to_op{-10.0, 0.0};
  EXPECT_FALSE(is_approaching(op_vel, trolley_to_op, 25.0, 2.0));
}

TEST(SummonMath, IsApproachingPassesBeside)
{
  // Operator moving east, trolley 5 m north of the path -> will pass beside.
  const Vec2 op_vel{1.0, 0.0};
  const Vec2 trolley_to_op{-10.0, 5.0};
  // Closest approach = 5 m > approach_pass_m (2 m) -> not approaching.
  EXPECT_FALSE(is_approaching(op_vel, trolley_to_op, 25.0, 2.0));
}

}  // namespace golfcart