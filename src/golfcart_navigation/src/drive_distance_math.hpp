// Pure math for the Drive-Distance feature. No ROS dependencies so it can be
// unit-tested directly with gtest.
//
// Computes a goal point N meters ahead of the trolley's current pose, in the
// direction of its current heading (yaw).

#ifndef GOLFCART_NAVIGATION__DRIVE_DISTANCE_MATH_HPP_
#define GOLFCART_NAVIGATION__DRIVE_DISTANCE_MATH_HPP_

#include <cmath>

namespace golfcart
{

// A 2D pose in the map frame.
struct Pose2D
{
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;  // heading (rad)
};

// Compute the goal point `distance_m` ahead of `pose` in the heading direction.
// Returns the goal (x, y). The goal heading is preserved (theta = yaw).
inline void goal_ahead(const Pose2D & pose, double distance_m,
                       double & goal_x, double & goal_y, double & goal_theta)
{
  goal_x = pose.x + distance_m * std::cos(pose.yaw);
  goal_y = pose.y + distance_m * std::sin(pose.yaw);
  goal_theta = pose.yaw;
}

}  // namespace golfcart

#endif  // GOLFCART_NAVIGATION__DRIVE_DISTANCE_MATH_HPP_