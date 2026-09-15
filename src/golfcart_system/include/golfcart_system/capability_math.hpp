// Pure math for hardware capability tracking. No ROS dependencies so it can
// be unit-tested directly with gtest.
//
// A capability is "present" when the sensor's messages are arriving (heartbeat)
// AND reporting valid data. If a sensor stops publishing or reports valid=false
// for a timeout, the capability flips to absent.
//
// This tracker is fed per-sensor "heartbeat" events (a message arrived) and
// "valid" flags, and reports whether each capability is currently present.

#ifndef GOLFCART_SYSTEM__CAPABILITY_MATH_HPP_
#define GOLFCART_SYSTEM__CAPABILITY_MATH_HPP_

#include <cstdint>

namespace golfcart
{

// Capability bit flags (must match CapabilityStatus.msg fields).
constexpr uint32_t CAP_LIDAR_HORIZONTAL = 1u << 0;
constexpr uint32_t CAP_LIDAR_TILTED     = 1u << 1;
constexpr uint32_t CAP_GPS              = 1u << 2;
constexpr uint32_t CAP_IMU              = 1u << 3;
constexpr uint32_t CAP_BATTERY          = 1u << 4;
constexpr uint32_t CAP_CAMERA           = 1u << 5;
constexpr uint32_t CAP_CORAL            = 1u << 6;
constexpr uint32_t CAP_ODRIVE           = 1u << 7;

// Tracks the presence of a single capability over time.
class CapabilityTracker
{
public:
  // Default constructor (zeroed) so the struct can be a member.
  CapabilityTracker()
  : timeout_s_(2.0)
  {
  }

  CapabilityTracker(double timeout_s)
  : timeout_s_(timeout_s)
  {
  }

  // Feed a heartbeat: a message arrived at time `now_s` with the given valid
  // flag. Returns true if the capability is currently present.
  bool update(double now_s, bool valid)
  {
    last_seen_s_ = now_s;
    last_valid_ = valid;
    return present(now_s);
  }

  // Whether the capability is present at time `now_s`.
  bool present(double now_s) const
  {
    // Not seen yet -> absent.
    if (last_seen_s_ < 0.0) {
      return false;
    }
    // Heartbeat too old -> absent.
    if (now_s - last_seen_s_ > timeout_s_) {
      return false;
    }
    // Last message reported invalid -> absent.
    return last_valid_;
  }

  void reset() { last_seen_s_ = -1.0; last_valid_ = false; }

private:
  double timeout_s_;
  double last_seen_s_ = -1.0;
  bool last_valid_ = false;
};

}  // namespace golfcart

#endif  // GOLFCART_SYSTEM__CAPABILITY_MATH_HPP_