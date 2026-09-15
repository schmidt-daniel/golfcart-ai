#include <cmath>

#include "golfcart_vision/gesture_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

// Test the gesture math used by gesture_recognition_node.

// Helper: a point.
Point2 P(double x, double y) { return Point2{x, y}; }

TEST(GestureMath, ElbowAngleStraight)
{
  // Straight arm: shoulder above elbow above wrist (y down).
  EXPECT_NEAR(elbow_angle(P(0, 0), P(0, 1), P(0, 2)), 180.0, 1e-6);
}

TEST(GestureMath, ElbowAngleBent)
{
  // Bent elbow ~90 deg: shoulder up, wrist to the side.
  EXPECT_NEAR(elbow_angle(P(0, 0), P(0, 1), P(1, 1)), 90.0, 1e-6);
}

TEST(GestureMath, UpperArmAngleHorizontal)
{
  // Arm abducted out to the side -> ~90 deg from vertical.
  EXPECT_NEAR(upper_arm_angle(P(0, 0), P(1, 0)), 90.0, 1e-6);
}

TEST(GestureMath, UpperArmAngleDown)
{
  // Arm straight down -> 0 deg from vertical.
  EXPECT_NEAR(upper_arm_angle(P(0, 0), P(0, 1)), 0.0, 1e-6);
}

TEST(GestureMath, WindmillRequiresAbductedArm)
{
  // Arm not abducted (straight down) -> not a windmill.
  EXPECT_FALSE(is_windmill(P(0, 0), P(0, 1), P(0, 0), 0.05));
}

TEST(GestureMath, WindmillRaisedArm)
{
  // Abducted arm, wrist raised and swung to the side -> windmill pose.
  EXPECT_TRUE(is_windmill(P(0.5, 0.5), P(0.8, 0.5), P(0.9, 0.3), 0.05));
}

TEST(GestureMath, ChooChooRequiresAbductedArm)
{
  EXPECT_FALSE(is_choo_choo(P(0, 0), P(0, 1), P(0, 0.5), 0.05));
}

TEST(GestureMath, ChooChooRaisedArm)
{
  // Abducted arm, wrist at/above shoulder -> choo-choo pose.
  EXPECT_TRUE(is_choo_choo(P(0.5, 0.5), P(0.8, 0.5), P(0.8, 0.4), 0.05));
}

TEST(GestureMath, DoublePalmUp)
{
  // Both wrists above their shoulders -> STOP.
  EXPECT_TRUE(is_double_palm_up(P(0.4, 0.5), P(0.4, 0.3),
                                P(0.6, 0.5), P(0.6, 0.3)));
}

TEST(GestureMath, DoublePalmUpOneHandDown)
{
  // One hand down -> not STOP.
  EXPECT_FALSE(is_double_palm_up(P(0.4, 0.5), P(0.4, 0.3),
                                 P(0.6, 0.5), P(0.6, 0.7)));
}

TEST(GestureMath, PatDown)
{
  // Hand lowered below shoulder -> SLOW pose.
  EXPECT_TRUE(is_pat_down(P(0.5, 0.5), P(0.5, 0.7)));
}

TEST(GestureMath, PatDownHandUp)
{
  // Hand raised -> not SLOW.
  EXPECT_FALSE(is_pat_down(P(0.5, 0.5), P(0.5, 0.3)));
}

TEST(GestureDebouncer, RequiresSustainedFrames)
{
  GestureDebouncer d(3);
  // Brief gesture -> not confirmed.
  EXPECT_EQ(d.update(GESTURE_SUMMON), GESTURE_NONE);
  EXPECT_EQ(d.update(GESTURE_SUMMON), GESTURE_NONE);
  // Sustained -> confirmed on the 3rd frame.
  EXPECT_EQ(d.update(GESTURE_SUMMON), GESTURE_SUMMON);
}

TEST(GestureDebouncer, ResetOnNone)
{
  GestureDebouncer d(3);
  d.update(GESTURE_SUMMON);
  d.update(GESTURE_SUMMON);
  // A NONE resets the count.
  EXPECT_EQ(d.update(GESTURE_NONE), GESTURE_NONE);
  EXPECT_EQ(d.update(GESTURE_SUMMON), GESTURE_NONE);
}

TEST(GestureDebouncer, ResetClears)
{
  GestureDebouncer d(2);
  d.update(GESTURE_STOP);
  d.update(GESTURE_STOP);
  EXPECT_EQ(d.update(GESTURE_STOP), GESTURE_STOP);
  d.reset();
  EXPECT_EQ(d.update(GESTURE_STOP), GESTURE_NONE);
}

}  // namespace golfcart