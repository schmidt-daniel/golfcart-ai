// Pure math for Person Re-ID. No ROS dependencies so it can be unit-tested
// directly with gtest.
//
// Re-acquires the follow-me operator after a target loss. The operator is the
// person who was being followed and reappears within a search window and a
// spatial gate (near the last-known position). This is a continuity-based
// re-acquire (works with LiDAR), not appearance recognition.

#ifndef GOLFCART_FOLLOW__REID_MATH_HPP_
#define GOLFCART_FOLLOW__REID_MATH_HPP_

#include <cmath>

namespace golfcart
{

// Re-ID state machine.
enum ReIdState
{
  REID_IDLE = 0,
  REID_SEARCHING = 1,
  REID_REACQUIRED = 2,
  REID_TIMEOUT = 3,
};

// Tracks the re-acquisition of a lost follow target.
class ReIdTracker
{
public:
  // Default constructor (zeroed) so the struct can be a member.
  ReIdTracker()
  : search_timeout_s_(30.0), max_gap_m_(3.0)
  {
  }

  ReIdTracker(double search_timeout_s, double max_gap_m)
  : search_timeout_s_(search_timeout_s), max_gap_m_(max_gap_m)
  {
  }

  // Begin searching for the lost operator at time `now_s` and last-known
  // position (lx, ly).
  void begin_search(double now_s, double lx, double ly)
  {
    state_ = REID_SEARCHING;
    search_start_s_ = now_s;
    last_x_ = lx;
    last_y_ = ly;
  }

  // Feed a candidate person position (x, y) at time `now_s`. Returns the new
  // state. Re-acquires if the candidate is within max_gap_m of the last-known
  // position and within the search timeout.
  ReIdState update(double now_s, double x, double y)
  {
    if (state_ != REID_SEARCHING) {
      return state_;
    }
    // Timeout: give up searching.
    if (now_s - search_start_s_ > search_timeout_s_) {
      state_ = REID_TIMEOUT;
      return state_;
    }
    // Spatial gate: candidate near the last-known position -> re-acquired.
    const double dx = x - last_x_;
    const double dy = y - last_y_;
    if (std::hypot(dx, dy) <= max_gap_m_) {
      state_ = REID_REACQUIRED;
      return state_;
    }
    return state_;
  }

  // Whether the tracker is currently searching.
  bool searching() const { return state_ == REID_SEARCHING; }

  // Whether the operator was re-acquired.
  bool reacquired() const { return state_ == REID_REACQUIRED; }

  void reset() { state_ = REID_IDLE; }

  ReIdState state() const { return state_; }

private:
  double search_timeout_s_;
  double max_gap_m_;
  double search_start_s_ = 0.0;
  double last_x_ = 0.0;
  double last_y_ = 0.0;
  ReIdState state_ = REID_IDLE;
};

}  // namespace golfcart

#endif  // GOLFCART_FOLLOW__REID_MATH_HPP_