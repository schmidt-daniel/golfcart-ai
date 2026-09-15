// Pure math for Wheel-Slip / Traction Detection. No ROS dependencies so it can
// be unit-tested directly with gtest.
//
// Provides:
//  - slip ratio: how much the wheels are spinning relative to actual motion
//  - a debounced slip detector (sustained slip before declaring SLIPPING)

#ifndef GOLFCART_CONTROL__SLIP_MATH_HPP_
#define GOLFCART_CONTROL__SLIP_MATH_HPP_

#include <algorithm>
#include <cmath>

namespace golfcart
{

// Slip ratio: (wheel_speed - actual_speed) / wheel_speed.
//   0.0 = no slip (wheels match actual motion)
//   1.0 = fully spinning in place (wheels moving, trolley stationary)
// Guards against divide-by-zero: if wheel_speed is near zero there's nothing
// to slip, so returns 0.
inline double slip_ratio(double wheel_speed_mps, double actual_speed_mps)
{
  if (std::abs(wheel_speed_mps) < 1e-3) {
    return 0.0;
  }
  const double ratio = (wheel_speed_mps - actual_speed_mps) / wheel_speed_mps;
  return std::clamp(ratio, 0.0, 1.0);
}

// A debounced slip detector. Reports slipping=true only after slip persists
// for a sustained time, to avoid false positives from sensor noise.
class SlipDetector
{
public:
  // Default constructor (zeroed) so the struct can be a member; callers should
  // use the parameterized constructor.
  SlipDetector()
  : threshold_(0.3), debounce_s_(0.5)
  {
  }

  // threshold: slip_ratio above which we consider it slipping (0-1).
  // debounce_s: how long slip must persist before declaring SLIPPING.
  SlipDetector(double threshold, double debounce_s)
  : threshold_(threshold), debounce_s_(debounce_s)
  {
  }

  // Feed a new slip ratio and the time step (s). Returns the current
  // slipping state.
  bool update(double ratio, double dt_s)
  {
    if (ratio > threshold_) {
      slip_time_ += std::max(0.0, dt_s);
    } else {
      slip_time_ = 0.0;
    }
    slipping_ = slip_time_ >= debounce_s_;
    return slipping_;
  }

  bool slipping() const { return slipping_; }
  void reset() { slip_time_ = 0.0; slipping_ = false; }

private:
  double threshold_;
  double debounce_s_;
  double slip_time_ = 0.0;
  bool slipping_ = false;
};

}  // namespace golfcart

#endif  // GOLFCART_CONTROL__SLIP_MATH_HPP_