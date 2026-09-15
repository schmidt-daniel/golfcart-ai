#include <cmath>
#include <memory>

#include "golfcart_msgs/msg/gesture_command.hpp"
#include "golfcart_vision/gesture_math.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Gesture Recognition node.
// Consumes body keypoints (shoulder/elbow/wrist) and classifies the motion
// into a gesture, publishing a GestureCommand on /gesture/command.
//
// The keypoint source is abstracted: in production this is MediaPipe Pose
// running on the Coral NPU over /camera/image; for testing it can be fed
// synthetic keypoints. This node implements the gesture classification +
// debounce logic (the pure math is in gesture_math.hpp).
//
// Publishes:
//   /gesture/command  (golfcart_msgs/GestureCommand)
//     gesture = NONE/WINDMILL/STOP/FOLLOW/SLOW
//
// Gestures are advisory — the gesture_controller_node maps them to actions.
class GestureRecognitionNode : public rclcpp::Node
{
public:
  GestureRecognitionNode()
  : Node("gesture_recognition_node")
  {
    confidence_threshold_ = declare_parameter<double>("confidence_threshold", 0.7);
    const int debounce_frames = declare_parameter<int>("debounce_frames", 15);
    const double rate_hz = declare_parameter<double>("publish_rate_hz", 10.0);

    debouncer_ = GestureDebouncer(debounce_frames);

    command_pub_ = create_publisher<golfcart_msgs::msg::GestureCommand>(
      "gesture/command", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { update(); });
  }

  // Feed a set of keypoints (normalized 0-1 image coords, y down) and a
  // confidence. Exposed for testing; the production source is MediaPipe Pose.
  void feed_keypoints(const Point2 & shoulder_l, const Point2 & elbow_l,
                      const Point2 & wrist_l, const Point2 & shoulder_r,
                      const Point2 & elbow_r, const Point2 & wrist_r,
                      double confidence)
  {
    last_ = classify(shoulder_l, elbow_l, wrist_l, shoulder_r, elbow_r, wrist_r);
    confidence_ = confidence;
    have_ = true;
  }

private:
  uint8_t classify(const Point2 & shoulder_l, const Point2 & elbow_l,
                   const Point2 & wrist_l, const Point2 & shoulder_r,
                   const Point2 & elbow_r, const Point2 & wrist_r)
  {
    // STOP: both hands up.
    if (is_double_palm_up(shoulder_l, wrist_l, shoulder_r, wrist_r)) {
      return GESTURE_STOP;
    }
    // SLOW: hand lowered (use the right arm; fall back to left).
    if (is_pat_down(shoulder_r, wrist_r) || is_pat_down(shoulder_l, wrist_l)) {
      return GESTURE_SLOW;
    }
    // FOLLOW: choo-choo (right arm, fall back to left).
    if (is_choo_choo(shoulder_r, elbow_r, wrist_r, 0.05) ||
        is_choo_choo(shoulder_l, elbow_l, wrist_l, 0.05)) {
      return GESTURE_FOLLOW;
    }
    // SUMMON: windmill (right arm, fall back to left).
    if (is_windmill(shoulder_r, elbow_r, wrist_r, 0.05) ||
        is_windmill(shoulder_l, elbow_l, wrist_l, 0.05)) {
      return GESTURE_SUMMON;
    }
    return GESTURE_NONE;
  }

  void update()
  {
    golfcart_msgs::msg::GestureCommand msg;
    msg.stamp = now();
    msg.confidence = 0.0f;
    msg.gesture = GESTURE_NONE;

    if (!have_) {
      command_pub_->publish(msg);
      return;
    }

    // Only accept above the confidence threshold.
    const uint8_t candidate = confidence_ >= confidence_threshold_ ? last_ : GESTURE_NONE;
    msg.gesture = debouncer_.update(candidate);
    msg.confidence = static_cast<float>(confidence_);
    command_pub_->publish(msg);
  }

  double confidence_threshold_ = 0.7;
  GestureDebouncer debouncer_;

  bool have_ = false;
  uint8_t last_ = GESTURE_NONE;
  double confidence_ = 0.0;

  rclcpp::Publisher<golfcart_msgs::msg::GestureCommand>::SharedPtr command_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::GestureRecognitionNode>());
  rclcpp::shutdown();
  return 0;
}