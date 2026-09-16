#ifndef GOLFCART_CONTROL__SAFETY_MATH_HPP_
#define GOLFCART_CONTROL__SAFETY_MATH_HPP_

namespace golfcart
{

// Obstacle-aware slowdown factor.
//
// Scales the max linear speed down as the nearest obstacle approaches, so the
// trolley can slow to maneuver around it (in autonomous modes). The factor
// ramps linearly from a minimum (at distance 0) to 1.0 (at the start distance).

// Compute the speed factor for a given obstacle distance.
//   distance_m          - nearest obstacle distance (m)
//   start_m             - distance at which slowing begins (m)
//   min_factor          - slowest speed factor (0-1)
// Returns a factor in [min_factor, 1.0]. If distance >= start_m or distance
// <= 0, returns 1.0 (no slowdown).
inline double obstacle_slowdown_factor(double distance_m, double start_m,
                                       double min_factor)
{
  if (start_m <= 0.0 || distance_m >= start_m || distance_m <= 0.0) {
    return 1.0;
  }
  const double t = distance_m / start_m;
  const double factor = min_factor + t * (1.0 - min_factor);
  if (factor < min_factor) {
    return min_factor;
  }
  if (factor > 1.0) {
    return 1.0;
  }
  return factor;
}

}  // namespace golfcart

#endif  // GOLFCART_CONTROL__SAFETY_MATH_HPP_