// Pure math for the Dead-Reckoning fallback. No ROS dependencies so it can be
// unit-tested directly with gtest.
//
// Provides:
//  - a bounded dead-reckoning budget (time and distance)
//  - integration of distance travelled from pose deltas
//  - the budget-exceeded decision
//
// The budget bounds how far the trolley can be from its true position before
// stopping (dead-reckoning accumulates drift).

#ifndef GOLFCART_LOCALIZATION__DEAD_RECKONING_MATH_HPP_
#define GOLFCART_LOCALIZATION__DEAD_RECKONING_MATH_HPP_

#include <cmath>

namespace golfcart
{

// A 2D pose in the map/odom frame (meters, radians).
struct Pose2D
{
  double x;
  double y;
  double yaw;
};

// The dead-reckoning budget state.
class DeadReckoningBudget
{
public:
  // Default constructor (zero budget) so the struct can be a member; callers
  // should use the parameterized constructor or configure().
  DeadReckoningBudget()
  : max_time_s_(0.0), max_distance_m_(0.0)
  {
    reset();
  }

  // max_time_s: max seconds to drive on dead reckoning.
  // max_distance_m: max meters to drive on dead reckoning.
  DeadReckoningBudget(double max_time_s, double max_distance_m)
  : max_time_s_(max_time_s), max_distance_m_(max_distance_m)
  {
    reset();
  }

  // Reset the budget (e.g. GPS recovered, or entering a fresh DR session).
  void reset()
  {
    used_time_s_ = 0.0;
    used_distance_m_ = 0.0;
    last_pose_valid_ = false;
  }

  // Feed a new fused pose. Integrates the distance travelled since the last
  // pose. dt_s is the time since the last pose.
  void update(const Pose2D & pose, double dt_s)
  {
    if (last_pose_valid_) {
      const double dx = pose.x - last_pose_.x;
      const double dy = pose.y - last_pose_.y;
      used_distance_m_ += std::hypot(dx, dy);
    }
    used_time_s_ += std::max(0.0, dt_s);
    last_pose_ = pose;
    last_pose_valid_ = true;
  }

  // True once either budget is exhausted.
  bool exceeded() const
  {
    return used_time_s_ >= max_time_s_ || used_distance_m_ >= max_distance_m_;
  }

  double used_time_s() const { return used_time_s_; }
  double used_distance_m() const { return used_distance_m_; }
  double remaining_time_s() const { return std::max(0.0, max_time_s_ - used_time_s_); }
  double remaining_distance_m() const { return std::max(0.0, max_distance_m_ - used_distance_m_); }
  double max_time_s() const { return max_time_s_; }
  double max_distance_m() const { return max_distance_m_; }

private:
  double max_time_s_;
  double max_distance_m_;
  double used_time_s_ = 0.0;
  double used_distance_m_ = 0.0;
  Pose2D last_pose_{0.0, 0.0, 0.0};
  bool last_pose_valid_ = false;
};

}  // namespace golfcart

#endif  // GOLFCART_LOCALIZATION__DEAD_RECKONING_MATH_HPP_