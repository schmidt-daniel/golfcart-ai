#pragma once

namespace golfcart
{

// Differential-drive kinematics.
// Converts linear/angular velocity to left/right wheel velocities.
//
//   v_left  = v - (omega * L / 2)
//   v_right = v + (omega * L / 2)
//
// where L is the distance between the drive wheels in meters.
// Sign convention must be validated against the physical trolley.
struct DifferentialDrive
{
  double wheel_separation_m = 0.5;

  void to_wheel_velocities(
    double linear_mps, double angular_radps,
    double & left_mps, double & right_mps) const
  {
    left_mps = linear_mps - (angular_radps * wheel_separation_m / 2.0);
    right_mps = linear_mps + (angular_radps * wheel_separation_m / 2.0);
  }
};

}  // namespace golfcart
