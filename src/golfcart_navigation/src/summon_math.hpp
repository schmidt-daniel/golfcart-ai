// Pure math for the Summon feature. No ROS dependencies so it can be
// unit-tested directly with gtest.
//
// Provides:
//  - velocity estimation from a history of lat/lon fixes
//  - predictive (intercept) target computation with lead/ETA coupling
//  - the "hold if approaching" approach-cone check
//
// All geometry is done in a local ENU frame (x = east, y = north) derived
// from lat/lon. The map frame is a rotation of this ENU frame by the map
// origin rotation; callers convert as needed.

#ifndef GOLFCART_NAVIGATION__SUMMON_MATH_HPP_
#define GOLFCART_NAVIGATION__SUMMON_MATH_HPP_

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace golfcart
{

// A lat/lon fix with a timestamp (seconds).
struct Fix
{
  double lat_deg;
  double lon_deg;
  double t_s;
};

// A 2D vector in the local ENU frame (x = east, y = north), meters.
struct Vec2
{
  double x;
  double y;
};

// Estimated operator velocity in the ENU frame.
struct VelocityEstimate
{
  Vec2 v;          // m/s
  double speed;    // m/s
  double heading;  // radians, ENU (0 = east, pi/2 = north)
  bool valid;      // false if not enough samples / too slow to trust
};

// Earth radius (m), used for the equirectangular approximation.
constexpr double kEarthRadiusM = 6371000.0;

// Convert a lat/lon offset (relative to origin) into ENU meters.
inline Vec2 latlon_to_enu(double lat_deg, double lon_deg,
                          double origin_lat_deg, double origin_lon_deg)
{
  const double dlat = (lat_deg - origin_lat_deg) * M_PI / 180.0;
  const double dlon = (lon_deg - origin_lon_deg) * M_PI / 180.0;
  const double cos_lat = std::cos(origin_lat_deg * M_PI / 180.0);
  return {kEarthRadiusM * dlon * cos_lat, kEarthRadiusM * dlat};
}

// Rotate an ENU vector into the map frame (map origin rotation).
inline Vec2 enu_to_map(const Vec2 & v, double origin_rotation_rad)
{
  const double c = std::cos(origin_rotation_rad);
  const double s = std::sin(origin_rotation_rad);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// Estimate operator velocity from a history of fixes (most recent last).
// Uses a least-squares linear fit over the last `window` samples to reject
// GPS noise. Returns valid=false if fewer than 2 samples or the speed is
// below `min_speed_mps` (treat as stationary).
inline VelocityEstimate estimate_velocity(
  const std::deque<Fix> & history, double min_speed_mps = 0.1)
{
  VelocityEstimate est{};
  est.valid = false;
  if (history.size() < 2) {
    return est;
  }

  // Convert to ENU relative to the first sample.
  const Fix & first = history.front();
  std::vector<Vec2> pts;
  std::vector<double> ts;
  pts.reserve(history.size());
  ts.reserve(history.size());
  for (const auto & f : history) {
    pts.push_back(latlon_to_enu(f.lat_deg, f.lon_deg, first.lat_deg, first.lon_deg));
    ts.push_back(f.t_s - first.t_s);
  }

  // Least-squares fit of x(t) and y(t): slope = velocity component.
  // v = (sum((t - tbar)(p - pbar))) / (sum((t - tbar)^2))
  double tbar = 0.0, xbar = 0.0, ybar = 0.0;
  for (size_t i = 0; i < ts.size(); ++i) {
    tbar += ts[i];
    xbar += pts[i].x;
    ybar += pts[i].y;
  }
  tbar /= static_cast<double>(ts.size());
  xbar /= static_cast<double>(pts.size());
  ybar /= static_cast<double>(pts.size());

  double denom = 0.0, numx = 0.0, numy = 0.0;
  for (size_t i = 0; i < ts.size(); ++i) {
    const double dt = ts[i] - tbar;
    denom += dt * dt;
    numx += dt * (pts[i].x - xbar);
    numy += dt * (pts[i].y - ybar);
  }
  if (denom < 1e-9) {
    return est;  // all timestamps identical
  }

  est.v.x = numx / denom;
  est.v.y = numy / denom;
  est.speed = std::hypot(est.v.x, est.v.y);
  est.heading = std::atan2(est.v.y, est.v.x);
  est.valid = est.speed >= min_speed_mps;
  return est;
}

// Compute the predictive (intercept) target.
//   operator_pos: current operator position (ENU)
//   op_vel:       operator velocity (ENU)
//   trolley_pos:  current trolley position (ENU)
//   trolley_speed: nominal trolley speed (m/s) for ETA
//   max_lead_m:   clamp the lead distance
// Iterates a few times to resolve the lead/ETA coupling:
//   target = op_pos + op_vel * ETA,  ETA = |target - trolley_pos| / trolley_speed
// Returns the predicted target (ENU).
inline Vec2 predictive_target(
  const Vec2 & operator_pos, const Vec2 & op_vel,
  const Vec2 & trolley_pos, double trolley_speed,
  double max_lead_m)
{
  Vec2 target = operator_pos;
  const int kIterations = 5;
  for (int i = 0; i < kIterations; ++i) {
    const double dx = target.x - trolley_pos.x;
    const double dy = target.y - trolley_pos.y;
    const double dist = std::hypot(dx, dy);
    const double eta = (trolley_speed > 1e-6) ? dist / trolley_speed : 0.0;
    target.x = operator_pos.x + op_vel.x * eta;
    target.y = operator_pos.y + op_vel.y * eta;
  }

  // Clamp the lead distance.
  const double lead_x = target.x - operator_pos.x;
  const double lead_y = target.y - operator_pos.y;
  const double lead = std::hypot(lead_x, lead_y);
  if (lead > max_lead_m && lead > 1e-9) {
    const double scale = max_lead_m / lead;
    target.x = operator_pos.x + lead_x * scale;
    target.y = operator_pos.y + lead_y * scale;
  }
  return target;
}

// "Hold if approaching" check.
// Returns true if the operator is walking toward the trolley (within the
// approach cone) and will pass close enough to actually reach it.
//   op_vel:        operator velocity (ENU)
//   trolley_to_op: vector from trolley to operator (ENU)
//   approach_cone_deg: half-angle of the approach cone
//   approach_pass_m:   max closest-approach distance to count as "reaching"
inline bool is_approaching(
  const Vec2 & op_vel, const Vec2 & trolley_to_op,
  double approach_cone_deg, double approach_pass_m)
{
  const double speed = std::hypot(op_vel.x, op_vel.y);
  if (speed < 1e-6) {
    return false;  // stationary -> not approaching
  }
  const double dist = std::hypot(trolley_to_op.x, trolley_to_op.y);
  if (dist < 1e-6) {
    return true;  // already at the trolley
  }

  // The operator is approaching if their velocity points toward the trolley,
  // i.e. opposite to trolley_to_op (which points trolley -> operator). So
  // compare op_vel against -trolley_to_op (operator -> trolley direction).
  const double to_trolley_x = -trolley_to_op.x;
  const double to_trolley_y = -trolley_to_op.y;
  const double dot = (op_vel.x * to_trolley_x + op_vel.y * to_trolley_y) /
                     (speed * dist);
  const double angle = std::acos(std::clamp(dot, -1.0, 1.0));
  if (angle > approach_cone_deg * M_PI / 180.0) {
    return false;
  }

  // Closest-approach distance: perpendicular distance from the trolley to the
  // operator's velocity line.
  const double cross = op_vel.x * trolley_to_op.y - op_vel.y * trolley_to_op.x;
  const double closest = std::abs(cross) / speed;
  return closest <= approach_pass_m;
}

}  // namespace golfcart

#endif  // GOLFCART_NAVIGATION__SUMMON_MATH_HPP_