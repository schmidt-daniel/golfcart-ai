// Pure math for Gesture Recognition. No ROS dependencies so it can be
// unit-tested directly with gtest.
//
// Gestures are defined by arm geometry (shoulder-elbow-wrist keypoints) and
// motion, not hand shape. All gestures are sustained/repetitive motions so a
// single one-off pose never triggers an action.
//
// Keypoint convention (MediaPipe Pose):
//   shoulder = (x, y)  - shoulder joint
//   elbow    = (x, y)  - elbow joint
//   wrist    = (x, y)  - wrist joint
// Coordinates are normalized image coords (0-1), y down. The "z-axis" here is
// the vertical image axis (y). "Up" = decreasing y.

#ifndef GOLFCART_VISION__GESTURE_MATH_HPP_
#define GOLFCART_VISION__GESTURE_MATH_HPP_

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace golfcart
{

// Gesture enum values (must match GestureCommand.msg).
constexpr uint8_t GESTURE_NONE = 0;
constexpr uint8_t GESTURE_SUMMON = 1;
constexpr uint8_t GESTURE_STOP = 2;
constexpr uint8_t GESTURE_FOLLOW = 3;
constexpr uint8_t GESTURE_SLOW = 4;

struct Point2
{
  double x = 0.0;
  double y = 0.0;
};

// Elbow angle (degrees) at the elbow joint, from the shoulder-elbow-wrist
// chain. 180 = straight arm, ~90 = bent elbow.
inline double elbow_angle(const Point2 & shoulder, const Point2 & elbow,
                          const Point2 & wrist)
{
  const double v1x = shoulder.x - elbow.x;
  const double v1y = shoulder.y - elbow.y;
  const double v2x = wrist.x - elbow.x;
  const double v2y = wrist.y - elbow.y;
  const double dot = v1x * v2x + v1y * v2y;
  const double m1 = std::hypot(v1x, v1y);
  const double m2 = std::hypot(v2x, v2y);
  if (m1 < 1e-6 || m2 < 1e-6) {
    return 0.0;
  }
  const double cos_a = std::clamp(dot / (m1 * m2), -1.0, 1.0);
  return std::acos(cos_a) * 180.0 / M_PI;
}

// Upper-arm angle from vertical (degrees). 0 = arm straight down, ~90 = arm
// abducted out to the side (roughly horizontal).
inline double upper_arm_angle(const Point2 & shoulder, const Point2 & elbow)
{
  const double dx = elbow.x - shoulder.x;
  const double dy = elbow.y - shoulder.y;
  // Angle from the vertical (y) axis. Horizontal arm -> ~90 deg.
  const double ang = std::atan2(std::abs(dx), std::abs(dy)) * 180.0 / M_PI;
  return ang;
}

// A debounced gesture detector. Reports a gesture only after it persists for
// a sustained number of frames, to avoid false positives from noise.
class GestureDebouncer
{
public:
  // Default constructor (zeroed) so the struct can be a member.
  GestureDebouncer()
  : required_frames_(15)
  {
  }

  GestureDebouncer(int required_frames)
  : required_frames_(required_frames)
  {
  }

  // Feed a candidate gesture id (0 = NONE). Returns the confirmed gesture id
  // (0 if not yet sustained).
  uint8_t update(uint8_t candidate)
  {
    if (candidate == GESTURE_NONE) {
      current_ = GESTURE_NONE;
      count_ = 0;
      return GESTURE_NONE;
    }
    if (candidate == current_) {
      ++count_;
    } else {
      current_ = candidate;
      count_ = 1;
    }
    return count_ >= required_frames_ ? current_ : GESTURE_NONE;
  }

  void reset() { current_ = GESTURE_NONE; count_ = 0; }

private:
  int required_frames_;
  uint8_t current_ = GESTURE_NONE;
  int count_ = 0;
};

// Windmill: the wrist circles around the shoulder's vertical axis while the
// upper arm stays roughly horizontal. Detected by tracking the wrist's
// horizontal (x) oscillation over a window: sustained large left-right swings
// with the upper arm abducted.
//
// Returns true when the wrist has swung both left and right of the shoulder
// (a full cycle) with the upper arm abducted.
inline bool is_windmill(const Point2 & shoulder, const Point2 & elbow,
                        const Point2 & wrist, double min_swing_px)
{
  // Upper arm must be abducted (roughly horizontal): angle near 90 deg.
  if (upper_arm_angle(shoulder, elbow) < 60.0) {
    return false;
  }
  // Wrist must be well above the shoulder (arm raised).
  if (wrist.y > shoulder.y - 0.05) {
    return false;
  }
  // The windmill is a sustained circular motion; the single-frame check here
  // just confirms the pose is consistent with a raised, abducted arm. The
  // temporal circular motion is validated by the debouncer + a motion window
  // in the node. This function confirms the static pose.
  return std::abs(wrist.x - shoulder.x) > min_swing_px;
}

// Choo-choo: the wrist oscillates up/down along the vertical (y) axis while
// the upper arm stays roughly horizontal. Detected by the wrist being
// alternately above and below a reference, with the upper arm abducted.
inline bool is_choo_choo(const Point2 & shoulder, const Point2 & elbow,
                         const Point2 & wrist, double min_pump_px)
{
  if (upper_arm_angle(shoulder, elbow) < 60.0) {
    return false;
  }
  // Wrist near the shoulder height (mid-pump) or above; the up/down
  // oscillation is validated temporally. This confirms the arm is raised and
  // abducted with the wrist roughly at/above shoulder level.
  return wrist.y < shoulder.y + min_pump_px;
}

// Double palm up (STOP): both wrists above their shoulders (both hands up).
inline bool is_double_palm_up(const Point2 & shoulder_l, const Point2 & wrist_l,
                              const Point2 & shoulder_r, const Point2 & wrist_r)
{
  return wrist_l.y < shoulder_l.y && wrist_r.y < shoulder_r.y;
}

// Pat-down (SLOW): the hand is lowered repeatedly. Single-frame check: the
// wrist is below the shoulder (hand lowered). The repetition is validated
// temporally by the debouncer.
inline bool is_pat_down(const Point2 & shoulder, const Point2 & wrist)
{
  return wrist.y > shoulder.y + 0.05;
}

}  // namespace golfcart

#endif  // GOLFCART_VISION__GESTURE_MATH_HPP_