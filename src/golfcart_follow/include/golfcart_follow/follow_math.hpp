#ifndef GOLFCART_FOLLOW__FOLLOW_MATH_HPP_
#define GOLFCART_FOLLOW__FOLLOW_MATH_HPP_

namespace golfcart
{

// Follow-me speed adaptation.
//
// Vary the follow speed with the target distance so the trolley slows when the
// person is close (avoid crowding) and speeds up when they pull away, instead
// of a fixed speed. The speed ramps linearly between a minimum (at the follow
// distance) and the configured max (at or beyond the full-speed distance).

// Compute the desired follow speed for a given target distance.
//   distance_m        - current distance to the person (m)
//   follow_distance_m - the set following distance (m)
//   min_speed_mps     - speed when at the follow distance (m/s)
//   max_speed_mps     - speed when at/above full_speed_distance_m (m/s)
//   full_speed_distance_m - distance at which max speed is reached (m)
// Returns the desired linear speed (m/s), clamped to [min_speed, max_speed].
// If distance <= follow_distance, returns min_speed (don't crowd the person).
inline double follow_speed(double distance_m, double follow_distance_m,
                           double min_speed_mps, double max_speed_mps,
                           double full_speed_distance_m)
{
  if (distance_m <= follow_distance_m) {
    return min_speed_mps;
  }
  if (full_speed_distance_m <= follow_distance_m) {
    return max_speed_mps;
  }
  const double t = (distance_m - follow_distance_m) /
                   (full_speed_distance_m - follow_distance_m);
  const double speed = min_speed_mps + t * (max_speed_mps - min_speed_mps);
  if (speed < min_speed_mps) {
    return min_speed_mps;
  }
  if (speed > max_speed_mps) {
    return max_speed_mps;
  }
  return speed;
}

}  // namespace golfcart

#endif  // GOLFCART_FOLLOW__FOLLOW_MATH_HPP_